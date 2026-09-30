// Jarvis-HOOK: synthetic x86 functions; no game, injection or save data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#if __has_include("../hooks/CompatibleDetour.h")
#include "../hooks/CompatibleDetour.h"
using Create=int(WINAPI*)(void*,void*,void**);
using Target=int(WINAPI*)(void*);
static Create createHook;static Target enableHook,disableHook,removeHook;
static bool failEnable=false,failAfterCreate=false;static int checks=0,failures=0;
static std::uint64_t original=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static int __cdecl Replacement(){return original?reinterpret_cast<int(__cdecl*)()>(original)()+4:-100;}
static bool CreateAdapter(std::uintptr_t target,std::uintptr_t detour,std::uintptr_t* out) noexcept {
    const bool created=createHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(detour),reinterpret_cast<void**>(out))==0;
    return created&&!failAfterCreate;
}
static bool EnableAdapter(std::uintptr_t target) noexcept {return !failEnable&&enableHook(reinterpret_cast<void*>(target))==0;}
static bool DisableAdapter(std::uintptr_t target) noexcept {const int result=disableHook(reinterpret_cast<void*>(target));return result==0||result==6;}
static bool RemoveAdapter(std::uintptr_t target) noexcept {return removeHook(reinterpret_cast<void*>(target))==0;}
static unsigned char* Fixture(){
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!memory)return nullptr;std::memset(memory,0x90,4096);
    const unsigned char code[]={0xB8,7,0,0,0,0xC3};std::memcpy(memory+32,code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),memory,4096);return memory+32;
}
static int Invoke(void* target){
    __try{return reinterpret_cast<int(__cdecl*)()>(target)();}
    __except(EXCEPTION_EXECUTE_HANDLER){return -10000;}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    HMODULE provider=LoadLibraryA(argv[1]),peer=LoadLibraryA(argv[2]);
    if(!provider||!peer)return 2;
    using Init=int(WINAPI*)();using Bind=int(WINAPI*)(HMODULE);
    auto init=reinterpret_cast<Init>(GetProcAddress(provider,"MH_Initialize"));
    auto bind=reinterpret_cast<Bind>(GetProcAddress(provider,"MH_BindSharedProvider"));
    createHook=reinterpret_cast<Create>(GetProcAddress(provider,"MH_CreateHook"));
    enableHook=reinterpret_cast<Target>(GetProcAddress(provider,"MH_EnableHook"));
    disableHook=reinterpret_cast<Target>(GetProcAddress(provider,"MH_DisableHook"));
    removeHook=reinterpret_cast<Target>(GetProcAddress(provider,"MH_RemoveHook"));
    if(!init||!bind||!createHook||!enableHook||!disableHook||!removeHook)return 2;
    Check(bind(peer)==0&&init()==0,"shared backend initializes");
    FfxHooks::Coexistence::runtime.Observe(true);
    auto* first=Fixture();auto* second=Fixture();if(!first||!second)return 2;
    {FfxHooks::CompatibleDetour missing(reinterpret_cast<std::uintptr_t>(first),reinterpret_cast<std::uintptr_t>(&Replacement),&original);
        Check(!missing.hook()&&Invoke(first)==7,"peer mode with missing backend cannot fall back to PolyHook");}
    static const FfxHooks::Coexistence::DetourApi api{CreateAdapter,EnableAdapter,DisableAdapter,RemoveAdapter};
    FfxHooks::Coexistence::detourApi.store(&api);
    {FfxHooks::CompatibleDetour normal(reinterpret_cast<std::uintptr_t>(first),reinterpret_cast<std::uintptr_t>(&Replacement),&original);
        Check(normal.hook()&&Invoke(first)==11,"routed hook calls original exactly once");
        Check(normal.hook()&&Invoke(first)==11,"repeat installation does not double the hook");
        Check(normal.unHook()&&Invoke(first)==7&&original!=0,"neutralization retains callable original");}
    Check(Invoke(first)==7,"retired adapter keeps the function native");
    failEnable=true;
    {FfxHooks::CompatibleDetour failed(reinterpret_cast<std::uintptr_t>(second),reinterpret_cast<std::uintptr_t>(&Replacement),&original);
        Check(!failed.hook(),"enable rejection propagates to the caller");
        original=0;
        auto externalEnable=reinterpret_cast<Target>(GetProcAddress(peer,"MH_EnableHook"));
        Check(externalEnable(second)==0&&Invoke(second)==7,"partial publication remains a neutral trampoline after caller cleanup");}
    Check(Invoke(second)==7,"failed adapter destruction cannot expose a dangling detour");
    auto* third=Fixture();if(!third)return 2;
    failEnable=false;failAfterCreate=true;
    {FfxHooks::CompatibleDetour failed(reinterpret_cast<std::uintptr_t>(third),reinterpret_cast<std::uintptr_t>(&Replacement),&original);
        Check(!failed.hook(),"post-create validation failure is reported");
        original=0;
        auto externalEnable=reinterpret_cast<Target>(GetProcAddress(peer,"MH_EnableHook"));
        Check(externalEnable(third)==0&&Invoke(third)==7,"failed creation retains a neutral relay before caller destruction");}
    Check(Invoke(third)==7,"failed creation remains native after caller destruction");
    std::printf("Fahrenheit detour RT1: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL: shared-provider PolyHook routing is missing");return 1;}
#endif
