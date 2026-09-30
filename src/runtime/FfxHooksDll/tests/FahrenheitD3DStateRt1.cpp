// Jarvis-HOOK: actual D3D11 WARP state isolation; no game or injection.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/FahrenheitD3DState.h")
#include "../hooks/FahrenheitD3DState.h"
#define HAS_PEER_STATE 1
#endif
static int checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
template<class T> static void Release(T*& p){if(p){p->Release();p=nullptr;}}
#ifdef HAS_PEER_STATE
using State=FfxHooks::Coexistence::D3DState;
static void ExceptionalDraw(State* state,ID3D11DeviceContext* context){
    if(!state->Begin(context))return;
    __try {context->ClearState();RaiseException(0xE1234567,0,0,nullptr);}
    __finally {state->End();}
}
static bool CatchDraw(State* state,ID3D11DeviceContext* context){
    __try {ExceptionalDraw(state,context);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
static void Run(UINT flags){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,&level,1,
        D3D11_SDK_VERSION,&device,nullptr,&context);
    Check(SUCCEEDED(hr)&&device&&context,"WARP device is created for the actual test");
    if(!device||!context)return;
    ID3D11Texture2D* texture[2]{};ID3D11RenderTargetView* rtv[2]{};
    D3D11_TEXTURE2D_DESC description{};description.Width=32;description.Height=32;
    description.MipLevels=1;description.ArraySize=1;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count=1;description.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(unsigned i=0;i<2;++i){
        Check(SUCCEEDED(device->CreateTexture2D(&description,nullptr,&texture[i])),"peer render target texture");
        Check(texture[i]&&SUCCEEDED(device->CreateRenderTargetView(texture[i],nullptr,&rtv[i])),"peer render target view");
    }
    ID3D11Buffer* buffer=nullptr;D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Check(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&buffer)),"peer constant buffer");
    ID3D11SamplerState* sampler=nullptr;D3D11_SAMPLER_DESC sd{};
    sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc=D3D11_COMPARISON_NEVER;sd.MaxLOD=D3D11_FLOAT32_MAX;
    Check(SUCCEEDED(device->CreateSamplerState(&sd,&sampler)),"peer sampler");
    context->OMSetRenderTargets(2,rtv,nullptr);context->VSSetConstantBuffers(5,1,&buffer);
    context->PSSetSamplers(3,1,&sampler);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT ports[2]={{1,2,20,20,0,1},{3,4,16,16,0,1}};
    context->RSSetViewports(2,ports);
    State state;
    Check(!state.Begin(nullptr),"null context has no side effects");
    Check(state.Begin(context),"peer state is isolated using its immediate context");
    ID3D11RenderTargetView* observed[2]{};context->OMGetRenderTargets(2,observed,nullptr);
    Check(!observed[0]&&!observed[1],"overlay starts without peer render targets");
    Release(observed[0]);Release(observed[1]);
    Check(!state.Begin(context)&&!state.Reset(),"nested activation and active reset cannot discard peer state");
    context->OMSetRenderTargets(1,rtv,nullptr);context->ClearState();
    state.End();state.End();
    context->OMGetRenderTargets(2,observed,nullptr);
    Check(observed[0]==rtv[0]&&observed[1]==rtv[1],"both peer render targets survive native draw");
    Release(observed[0]);Release(observed[1]);
    ID3D11Buffer* cb=nullptr;context->VSGetConstantBuffers(5,1,&cb);
    Check(cb==buffer,"peer constant buffers survive native ClearState");Release(cb);
    ID3D11SamplerState* ss=nullptr;context->PSGetSamplers(3,1,&ss);
    Check(ss==sampler,"peer sampler slots survive");Release(ss);
    D3D11_VIEWPORT actual[2]{};UINT count=2;context->RSGetViewports(&count,actual);
    Check(count==2&&std::memcmp(actual,ports,sizeof(ports))==0,"all peer viewports are restored");
    Check(CatchDraw(&state,context),"test exercises structured-exception cleanup");
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
    Check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"peer state is restored even after a native exception");
    Check(state.Reset()&&state.Begin(context),"resize reset allows a fresh context state");state.End();
    Check(state.Reset(),"idle context-state resources can be released");
    context->ClearState();Release(buffer);Release(sampler);
    for(unsigned i=0;i<2;++i){Release(rtv[i]);Release(texture[i]);}
    Release(context);Release(device);
}
static void RunResize(){
    const wchar_t* className=L"JarvisFahrenheitResizeRt1";
    WNDCLASSW windowClass{};windowClass.lpfnWndProc=DefWindowProcW;
    windowClass.hInstance=GetModuleHandleW(nullptr);windowClass.lpszClassName=className;
    const ATOM registered=RegisterClassW(&windowClass);
    Check(registered!=0,"isolated resize window class is registered");
    if(!registered)return;
    HWND window=CreateWindowExW(0,className,L"Fahrenheit resize RT1",WS_OVERLAPPEDWINDOW,
        0,0,128,128,nullptr,nullptr,windowClass.hInstance,nullptr);
    Check(window!=nullptr,"hidden test window exists without launching a game");
    if(!window){UnregisterClassW(className,windowClass.hInstance);return;}
    IDXGISwapChain* chain=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    DXGI_SWAP_CHAIN_DESC description{};description.BufferDesc.Width=64;description.BufferDesc.Height=64;
    description.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
    description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.BufferCount=2;
    description.OutputWindow=window;description.Windowed=TRUE;description.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    const HRESULT created=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,
        &level,1,D3D11_SDK_VERSION,&description,&chain,&device,nullptr,&context);
    if(FAILED(created)){
        DWORD session=MAXDWORD;ProcessIdToSessionId(GetCurrentProcessId(),&session);
        std::printf("WARP swapchain HRESULT=0x%08lX session=%lu\n",static_cast<unsigned long>(created),session);
    }
    Check(SUCCEEDED(created)&&chain&&device&&context,"actual WARP swapchain is available for ResizeBuffers");
    if(chain&&device&&context){
        State state;
        for(unsigned pass=0;pass<4;++pass){
            ID3D11Texture2D* buffer=nullptr;ID3D11RenderTargetView* view=nullptr;
            const HRESULT acquired=chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&buffer));
            Check(SUCCEEDED(acquired)&&buffer,"current backbuffer is acquired after each resize");
            const HRESULT viewed=buffer?device->CreateRenderTargetView(buffer,nullptr,&view):E_FAIL;
            Check(SUCCEEDED(viewed)&&view,"current backbuffer view is created");
            if(!buffer||!view){Release(view);Release(buffer);break;}
            const UINT width=48+pass*16,height=32+pass*16;
            Check(chain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0)==DXGI_ERROR_INVALID_CALL,
                "negative control: retained backbuffer references reject actual resize");
            const bool entered=state.Begin(context);
            Check(entered,"overlay context state accepts the current backbuffer");
            if(entered){context->OMSetRenderTargets(1,&view,nullptr);state.End();}
            Release(view);Release(buffer);
            // Do not Reset here: End must also clear indirect references held
            // by the cached isolated state before the next frame or resize.
            const HRESULT resized=chain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0);
            Check(SUCCEEDED(resized),"cached overlay state does not retain a backbuffer across resize");
            DXGI_SWAP_CHAIN_DESC actual{};
            Check(SUCCEEDED(chain->GetDesc(&actual))&&actual.BufferDesc.Width==width&&actual.BufferDesc.Height==height,
                "successful resize publishes the requested dimensions");
            Check(state.Reset(),"resize cleanup retires cached context resources");
        }
        state.Reset();context->ClearState();
    }
    Release(context);Release(device);Release(chain);
    DestroyWindow(window);UnregisterClassW(className,windowClass.hInstance);
}
#endif
int main(int argc,char** argv){
    if(argc>2||(argc==2&&std::strcmp(argv[1],"--swapchain")!=0)){
        std::puts("Usage: fahrenheit-d3d-state-rt1 [--swapchain]");return 2;
    }
#ifdef HAS_PEER_STATE
    Run(0);Run(D3D11_CREATE_DEVICE_SINGLETHREADED);
    if(argc==2)RunResize();
#else
    Check(false,"native renderer lacks complete peer pipeline isolation");
#endif
    std::printf("Fahrenheit D3D state RT1: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
