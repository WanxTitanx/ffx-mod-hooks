#include "ArcanaAssets.h"
#include "ArcanaCore.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <process.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace FfxHooks::Arcana::Assets {
namespace {
std::atomic<bool> everStarted{false};
std::atomic<unsigned> renderOwner{0};
struct Pixels {unsigned id=80,width=0,height=0;std::vector<unsigned char> bytes;};
struct Shaders {std::vector<unsigned char> vertex,pixel;};
struct Shared {
    std::atomic<bool> enabled{false},attempted{false},forceHidden{false};
    std::mutex mutex;std::filesystem::path root;HANDLE wake=nullptr;
    NativeUi::Images frame;int selected=-1;std::uint64_t newest=0,publishedAt=0;bool closed=false;
    std::array<std::shared_ptr<const Pixels>,3> pixels{};
    std::shared_ptr<const Shaders> shaders;void(*log)(const char*)=nullptr;
};
// Retained, bounded process-lifetime state: stop must not wait in DLL detach.
Shared& Data(){static Shared* state=new Shared;return *state;}
bool Valid(const NativeUi::Images& frame){
    if(!frame.count)return true;
    if(!frame.generation||frame.count>frame.quads.size())return false;
    for(unsigned i=0;i<frame.count;++i){const auto& q=frame.quads[i];
        if(q.resource>=80||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.width)||!std::isfinite(q.height)||
           q.x<0||q.y<0||q.width<=0||q.height<=0||q.x+q.width>1.001f||q.y+q.height>1.001f)return false;
    }
    return true;
}
std::shared_ptr<const Shaders> Compile(){
    constexpr char source[]=
        "cbuffer Rect:register(b0){float4 rect;} "
        "struct V{float4 pos:SV_POSITION;float2 uv:TEXCOORD;}; "
        "V vs(uint id:SV_VertexID){float2 p[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};"
        "V o;o.uv=p[id];o.pos=float4((rect.xy+p[id]*rect.zw)*float2(2,-2)+float2(-1,1),0,1);return o;} "
        "Texture2D art:register(t0);SamplerState sampleArt:register(s0);"
        "float4 ps(V input):SV_TARGET{return art.Sample(sampleArt,input.uv);}";
    ComPtr<ID3DBlob> vs,ps,error;
    if(FAILED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error)))return {};
    error.Reset();
    if(FAILED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error)))return {};
    auto result=std::make_shared<Shaders>();
    const auto* a=static_cast<const unsigned char*>(vs->GetBufferPointer());result->vertex.assign(a,a+vs->GetBufferSize());
    const auto* b=static_cast<const unsigned char*>(ps->GetBufferPointer());result->pixel.assign(b,b+ps->GetBufferSize());
    return result;
}
std::shared_ptr<const Pixels> Decode(IWICImagingFactory* factory,unsigned id){
    auto& data=Data();std::filesystem::path path;
    if(id<kCardCount)path=data.root/L"cards"/FindCard(id)->asset;
    else path=data.root/L"shared"/(id==78?L"tarot-icon-v1.png":L"card-back-v1.png");
    std::error_code error;const auto bytes=std::filesystem::file_size(path,error);
    if(error||bytes==0||bytes>8*1024*1024)return {};
    ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> convert;
    if(FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||
       FAILED(decoder->GetFrame(0,&frame)))return {};
    UINT width=0,height=0;
    if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>1536||height>1536)return {};
    if(id<kCardCount&&(width!=1024||height!=1536))return {};
    if(FAILED(factory->CreateFormatConverter(&convert))||
       FAILED(convert->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    auto result=std::make_shared<Pixels>();result->id=id;result->width=width;result->height=height;
    result->bytes.resize(static_cast<std::size_t>(width)*height*4);
    if(FAILED(convert->CopyPixels(nullptr,width*4,static_cast<UINT>(result->bytes.size()),result->bytes.data())))return {};
    return result;
}
unsigned __stdcall Worker(void*){
    auto& data=Data();const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized))return 0;
    try {
        ComPtr<IWICImagingFactory> factory;
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))){
            auto shaders=Compile();auto icon=Decode(factory.Get(),78);auto back=Decode(factory.Get(),79);
            {std::lock_guard<std::mutex> lock(data.mutex);data.shaders=std::move(shaders);data.pixels[1]=std::move(icon);data.pixels[2]=std::move(back);}
            int last=-2;
            while(data.enabled.load()){
                int requested=-1;{std::lock_guard<std::mutex> lock(data.mutex);requested=data.selected;}
                if(requested>=0&&requested!=last){
                    auto pixels=Decode(factory.Get(),static_cast<unsigned>(requested));
                    {std::lock_guard<std::mutex> lock(data.mutex);if(data.selected==requested)data.pixels[0]=std::move(pixels);}
                    last=requested;continue;
                }
                WaitForSingleObject(data.wake,250);
            }
        }
    }catch(...){if(data.log)data.log("[ffx-hooks] Arcana art worker failed; native text remains available\n");}
    {std::lock_guard<std::mutex> lock(data.mutex);data.pixels={};data.shaders.reset();}
    CoUninitialize();return 0;
}
struct GpuImage {unsigned id=80;std::shared_ptr<const Pixels> source;ComPtr<ID3D11ShaderResourceView> view;};
struct Gpu {
    bool ready=false;
    ComPtr<ID3D11Device> device;ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11Buffer> constant;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;std::array<GpuImage,3> images{};
};
Gpu& Graphics(){static Gpu* gpu=new Gpu;return *gpu;}
bool Initialize(Gpu& gpu,ID3D11Device* device,const Shaders& shaders){
    if(gpu.device.Get()==device&&gpu.ready)return true;
    gpu={};gpu.device=device;
    ComPtr<ID3D11Device1> modern;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&modern))))return false;
    const D3D_FEATURE_LEVEL level=device->GetFeatureLevel();
    if(FAILED(modern->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&gpu.state))||
       FAILED(device->CreateVertexShader(shaders.vertex.data(),shaders.vertex.size(),nullptr,&gpu.vertex))||
       FAILED(device->CreatePixelShader(shaders.pixel.data(),shaders.pixel.size(),nullptr,&gpu.pixel)))return false;
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(device->CreateBuffer(&buffer,nullptr,&gpu.constant)))return false;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sampler,&gpu.sampler)))return false;
    D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&blend,&gpu.blend)))return false;
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    if(FAILED(device->CreateRasterizerState(&raster,&gpu.raster)))return false;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    gpu.ready=SUCCEEDED(device->CreateDepthStencilState(&depth,&gpu.depth));
    return gpu.ready;
}
bool Upload(Gpu& gpu,unsigned slot,const std::shared_ptr<const Pixels>& pixels){
    auto& image=gpu.images[slot];if(!pixels)return false;
    if(image.source==pixels&&image.view)return true;
    image={};D3D11_TEXTURE2D_DESC texture{};texture.Width=pixels->width;texture.Height=pixels->height;
    texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=1;
    texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels->bytes.data();initial.SysMemPitch=pixels->width*4;
    ComPtr<ID3D11Texture2D> object;
    if(FAILED(gpu.device->CreateTexture2D(&texture,&initial,&object))||FAILED(gpu.device->CreateShaderResourceView(object.Get(),nullptr,&image.view)))return false;
    image.id=pixels->id;image.source=pixels;return true;
}
struct RestoreState {
    ID3D11DeviceContext1* context=nullptr;ComPtr<ID3DDeviceContextState> previous;
    ~RestoreState(){
        if(context&&previous){
            // Do not retain the game's back buffer inside our inactive state:
            // that would prevent a later ResizeBuffers call from succeeding.
            context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);
        }
    }
};
}
bool Start(const std::filesystem::path& root,void(*log)(const char*)){
    auto& data=Data();everStarted=true;if(data.attempted.exchange(true))return data.enabled.load();
    std::error_code directoryError;
    if(root.empty()||!std::filesystem::is_directory(root,directoryError)||directoryError)return false;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    data.root=root;data.log=log;data.wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!data.wake)return false;
    data.enabled=true;const auto thread=_beginthreadex(nullptr,0,Worker,nullptr,0,nullptr);
    if(!thread){data.enabled=false;return false;}
    CloseHandle(reinterpret_cast<HANDLE>(thread));return true;
}
void Publish(const NativeUi::Images& frame) noexcept {
    if(!everStarted.load())return;
    auto& data=Data();if(!data.enabled.load())return;
    try {
        if(!frame.count)data.forceHidden=true;
        std::unique_lock<std::mutex> lock(data.mutex,std::try_to_lock);
        if(!lock.owns_lock())return;
        if(!Valid(frame)){data.frame={};data.selected=-1;return;}
        if(frame.count&&(frame.generation<data.newest||((data.closed||data.forceHidden.load())&&frame.generation<=data.newest)))return;
        if(frame.count){data.newest=frame.generation;data.closed=false;data.forceHidden=false;}else data.closed=true;
        data.frame=frame;data.publishedAt=GetTickCount64();data.selected=-1;
        for(unsigned i=0;i<frame.count;++i)if(frame.quads[i].resource<kCardCount)data.selected=static_cast<int>(frame.quads[i].resource);
        SetEvent(data.wake);
    }catch(...){}
}
void Stop() noexcept {if(!everStarted.load())return;auto& data=Data();data.enabled=false;if(data.wake)SetEvent(data.wake);}
bool Decoded(unsigned resource) noexcept {
    if(!everStarted.load())return false;
    auto& data=Data();
    try {std::lock_guard<std::mutex> lock(data.mutex);for(const auto& image:data.pixels)if(image&&image->id==resource)return true;}catch(...){}
    return false;
}
bool Render(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height) noexcept {
    if(!everStarted.load())return false;
    unsigned expected=0;const unsigned thread=GetCurrentThreadId();
    if(!renderOwner.compare_exchange_strong(expected,thread)&&expected!=thread)return false;
    auto& data=Data();auto& gpu=Graphics();
    if(!data.enabled.load()){gpu={};return false;}
    if(!device||!context||!target||!width||!height)return false;
    try {
        NativeUi::Images frame;std::array<std::shared_ptr<const Pixels>,3> pixels;std::shared_ptr<const Shaders> shaders;
        {std::lock_guard<std::mutex> lock(data.mutex);if(GetTickCount64()-data.publishedAt>500)return false;frame=data.frame;pixels=data.pixels;shaders=data.shaders;}
        if(data.forceHidden.load()||!frame.count||!Valid(frame)||!shaders)return false;
        ComPtr<ID3D11DeviceContext1> modern;if(FAILED(context->QueryInterface(IID_PPV_ARGS(&modern)))||!Initialize(gpu,device,*shaders))return false;
        for(unsigned i=0;i<pixels.size();++i)Upload(gpu,i,pixels[i]);
        RestoreState restore;restore.context=modern.Get();modern->SwapDeviceContextState(gpu.state.Get(),&restore.previous);
        if(!restore.previous)return false;
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};context->RSSetViewports(1,&viewport);
        context->OMSetRenderTargets(1,&target,nullptr);context->OMSetBlendState(gpu.blend.Get(),nullptr,0xFFFFFFFFu);context->OMSetDepthStencilState(gpu.depth.Get(),0);
        context->RSSetState(gpu.raster.Get());context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(gpu.vertex.Get(),nullptr,0);context->PSSetShader(gpu.pixel.Get(),nullptr,0);
        context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        ID3D11Buffer* constant=gpu.constant.Get();ID3D11SamplerState* sampler=gpu.sampler.Get();
        context->VSSetConstantBuffers(0,1,&constant);context->PSSetSamplers(0,1,&sampler);
        bool rendered=false;
        for(unsigned i=0;i<frame.count;++i){
            const auto& quad=frame.quads[i];ID3D11ShaderResourceView* view=nullptr;
            for(auto& image:gpu.images)if(image.id==quad.resource)view=image.view.Get();
            if(!view)continue;
            const float rect[]={quad.x,quad.y,quad.width,quad.height};context->UpdateSubresource(constant,0,nullptr,rect,0,0);
            context->PSSetShaderResources(0,1,&view);context->Draw(6,0);rendered=true;
        }
        return rendered;
    }catch(...){return false;}
}
void Present(void* pointer) noexcept {
    if(!everStarted.load())return;
    if(renderOwner.load()&&renderOwner.load()!=GetCurrentThreadId())return;
    if(!Data().enabled.load()){Graphics()={};return;}
    try {
        auto* swap=static_cast<IDXGISwapChain*>(pointer);if(!swap)return;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> target;
        if(FAILED(swap->GetDevice(IID_PPV_ARGS(&device)))||FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back))))return;
        device->GetImmediateContext(&context);D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
        if(FAILED(device->CreateRenderTargetView(back.Get(),nullptr,&target)))return;
        Render(device.Get(),context.Get(),target.Get(),desc.Width,desc.Height);
    }catch(...){}
}
}
