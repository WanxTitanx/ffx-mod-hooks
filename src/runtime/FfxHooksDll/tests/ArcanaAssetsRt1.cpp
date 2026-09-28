#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../hooks/ArcanaAssets.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
template<class T> static void Release(T*& p){if(p){p->Release();p=nullptr;}}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;
    const bool started=Assets::Start(argv[1],nullptr);Check(started,"bounded asset worker starts from its own package directory");
    if(!started){std::printf("ArcanaAssetsRt1 %u/%u passed\n",checks-failures,checks);return 1;}
    NativeUi::Images images;images.generation=7;images.count=1;images.quads[0]={0,0,0,1,1};Assets::Publish(images);
    for(unsigned i=0;i<500&&!Assets::Decoded(0);++i)Sleep(10);
    Check(Assets::Decoded(0)&&Assets::Decoded(78),"selected PNG and icon decode without I/O in draw callbacks");
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    const HRESULT created=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    Check(SUCCEEDED(created),"isolated software D3D device is available");if(!device||!context)return 2;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=96;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target=nullptr;ID3D11RenderTargetView* rtv=nullptr;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target))&&SUCCEEDED(device->CreateRenderTargetView(target,nullptr,&rtv)),"private render target created");
    const float black[4]={0,0,0,0};context->ClearRenderTargetView(rtv,black);
    D3D11_VIEWPORT before{3,4,20,30,0,1};context->RSSetViewports(1,&before);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    Assets::Publish(images);
    Check(Assets::Render(device,context,rtv,64,96),"actual selected card uploads and renders through the shared renderer");
    D3D11_VIEWPORT after{};UINT count=1;context->RSGetViewports(&count,&after);D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
    Check(count==1&&!std::memcmp(&before,&after,sizeof(before))&&topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"viewport and topology are restored to their native owner");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"readback texture created");context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE mapped{};const bool read=SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped));
    bool colored=false;if(read){const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+48*mapped.RowPitch+32*4;colored=pixel[0]+pixel[1]+pixel[2]>40&&pixel[3]>200;context->Unmap(staging,0);}
    Check(colored,"GPU readback contains card pixels rather than an empty or placeholder panel");
    Assets::Publish({});Check(!Assets::Render(device,context,rtv,64,96),"closed native menu cannot draw a stale preview");
    Assets::Publish(images);Check(!Assets::Render(device,context,rtv,64,96),"late image requests from a closed generation cannot reopen the preview");
    images.generation=8;Assets::Publish(images);Check(Assets::Render(device,context,rtv,64,96),"a new native menu generation can reuse the bounded image cache");
    Assets::Stop();Check(!Assets::Render(device,context,rtv,64,96),"stop closes render admission without waiting on the worker");
    Release(staging);Release(rtv);Release(target);Release(context);Release(device);
    std::printf("ArcanaAssetsRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
