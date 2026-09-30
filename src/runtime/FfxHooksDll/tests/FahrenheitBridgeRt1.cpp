// Jarvis-HOOK: in-process ABI/lifecycle test, without a game image or rendering.
#include "../hooks/FahrenheitBridge.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <future>
#if __has_include("../hooks/FahrenheitBridge.cpp")
namespace C=FfxHooks::Coexistence;
static int checks=0,failures=0,scheduled=0,frames=0,resizes=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static void Schedule() noexcept {++scheduled;}
static void Frame(void*,std::uint32_t flags) noexcept {++frames;Check(C::peerInputCapture.load()==flags,"capture ownership reaches native frame");}
static void Resize(void*) noexcept {++resizes;}
static unsigned ExportCount(const char* name){
    const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    const auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if(!rva)return 0;
    const auto directory=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+rva);
    const auto names=reinterpret_cast<const DWORD*>(base+directory->AddressOfNames);
    unsigned matches=0;
    for(DWORD i=0;i<directory->NumberOfNames;++i)
        if(std::strcmp(reinterpret_cast<const char*>(base+names[i]),name)==0)++matches;
    return matches;
}
int main(){
    C::BridgeStatusV1 status{};
    Check(FfxHooks_FahrenheitQueryV1(nullptr,sizeof(status))==0,"null query is rejected");
    Check(FfxHooks_FahrenheitQueryV1(&status,sizeof(status)-1)==0,"short query cannot partially overwrite its output");
    Check(FfxHooks_FahrenheitQueryV1(&status,sizeof(status))==1&&status.abi==1&&status.size==24,"query returns versioned native layout");
    Check(!C::RegisterCallbacks(nullptr),"missing callback table is rejected");
    static const C::BridgeCallbacks callbacks{Schedule,Frame,Resize};
    Check(C::RegisterCallbacks(&callbacks)&&C::RegisterCallbacks(&callbacks),"same permanent callback table is idempotent");
    static const C::BridgeCallbacks second{Schedule,Frame,Resize};
    Check(!C::RegisterCallbacks(&second),"another callback table cannot steal ownership");
    Check(!FfxHooks_FahrenheitReadyV1(0,3)&&!FfxHooks_FahrenheitReadyV1(1,7),"invalid ABI and save capability are rejected");
    C::runtime.Observe(true);
    Check(FfxHooks_FahrenheitReadyV1(1,3)==1&&scheduled==1,"peer-ready schedules one native startup");
    Check(FfxHooks_FahrenheitReadyV1(1,3)==1&&scheduled==1,"repeated ready callback does not spawn workers");
    void* chain=reinterpret_cast<void*>(0x1234);
    Check(!FfxHooks_FahrenheitFrameV1(chain,0)&&frames==0,"frame is rejected until full startup completes");
    Check(C::runtime.TryStart()&&C::runtime.Finish(true),"native bootstrap completes");
    using BeginResize=std::uint32_t(__cdecl*)(void*);
    using EndResize=int(__cdecl*)(std::uint32_t);
    const auto beginResize=reinterpret_cast<BeginResize>(GetProcAddress(GetModuleHandleW(nullptr),"FfxHooks_FahrenheitResizeBeginV1"));
    const auto endResize=reinterpret_cast<EndResize>(GetProcAddress(GetModuleHandleW(nullptr),"FfxHooks_FahrenheitResizeEndV1"));
    for(const char* name:{"FfxHooks_FahrenheitQueryV1","FfxHooks_FahrenheitReadyV1","FfxHooks_FahrenheitFrameV1",
        "FfxHooks_FahrenheitResizeBeginV1","FfxHooks_FahrenheitResizeEndV1"})
        Check(ExportCount(name)==1,"one unambiguous undecorated C ABI export per entrypoint");
    Check(ExportCount("FfxHooks_FahrenheitResizeV1")==0,"unsafe one-shot resize export has been retired");
    if(beginResize&&endResize){
        const auto early=beginResize(chain);
        Check(early!=0&&resizes==0,"resize before the first frame owns a scope without releasing uncreated resources");
        int skipped=1;std::thread racingFrame([&]{skipped=FfxHooks_FahrenheitFrameV1(chain,0);});racingFrame.join();
        Check(skipped==0&&frames==0,"blocked first frame does not run during resize");
        Check(endResize(early)==1,"early resize releases its scope");
    }
    Check(!FfxHooks_FahrenheitFrameV1(nullptr,0)&&!FfxHooks_FahrenheitFrameV1(chain,8),"invalid frame arguments cannot bind the render owner");
    Check(FfxHooks_FahrenheitFrameV1(chain,3)==1&&frames==1,"valid frame enters once");
    int foreign=1;std::thread thread([&]{foreign=FfxHooks_FahrenheitFrameV1(chain,0);});thread.join();
    Check(foreign==0&&frames==1,"cross-thread frame is rejected");
    Check(!FfxHooks_FahrenheitFrameV1(reinterpret_cast<void*>(0x5678),0),"another swapchain cannot overwrite the render owner");
    Check(FfxHooks_FahrenheitQueryV1(&status,sizeof(status))==1&&status.frames==1&&status.managedSaveCompatible==0,"render success never claims save compatibility");
    Check(beginResize&&endResize,"resize scope exports cover the actual downstream ResizeBuffers call");
    if(beginResize&&endResize){
        std::promise<std::uint32_t> begun; std::promise<void> finish;
        auto begunFuture=begun.get_future(); auto finishFuture=finish.get_future();
        int ended=0;
        std::thread resizeThread([&]{
            const auto ticket=beginResize(chain);
            begun.set_value(ticket);finishFuture.wait();
            ended=endResize(ticket);
        });
        const auto ticket=begunFuture.get();
        Check(ticket!=0&&resizes==1,"idle resize can originate from the window thread");
        Check(!FfxHooks_FahrenheitFrameV1(chain,0)&&frames==1,"frame cannot recreate backbuffers during downstream resize");
        Check(!endResize(ticket),"another thread cannot release an active resize scope");
        Check(!beginResize(chain),"nested resize cannot steal the scope");
        finish.set_value();resizeThread.join();
        Check(ended==1&&FfxHooks_FahrenheitFrameV1(chain,0)==1&&frames==2,"successful resize end reopens frame admission");
        Check(!endResize(ticket),"completed resize tickets cannot be reused");
    }
    C::runtime.Stop();
    Check(!FfxHooks_FahrenheitFrameV1(chain,0)&&!FfxHooks_FahrenheitReadyV1(1,3),"terminal shutdown rejects further entries");
    std::printf("Fahrenheit bridge RT1: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL: Fahrenheit native bridge is missing");return 1;}
#endif
