// Jarvis-HOOK: a legacy flag must not bypass managed-save admission.
// Only a private synthetic memory region is used; no executable or save is read.
#include "../hooks/FahrenheitCoexistenceCore.h"
#include "../hooks/NulWardTeachHook.cpp"
#include <array>
#include <cstring>
#include <cstdio>
int main(){
    using namespace FfxHooks;
    static_assert(sizeof(void*)==4,"Native legacy writer is x86");
    constexpr std::size_t span=0x500;
    const std::size_t total=RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU+span;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,total,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!memory){std::puts("FAIL: owned legacy writer fixture allocation");return 1;}
    auto* target=memory+RVA_FFX_BATTLE_BUILD_ACTOR_COMMAND_MENU;
    const unsigned char pattern[]={0x81,0xFE,0x40,0x01,0x00,0x00};
    std::memcpy(target,pattern,sizeof(pattern));
    std::array<unsigned char,span> before{};std::memcpy(before.data(),target,span);
    FfxHooks::Coexistence::runtime.Observe(true);
    const auto result=FfxHooks::InstallNulWardTeachHook(reinterpret_cast<std::uintptr_t>(memory),false,nullptr);
    int failures=0;
    if(result.ok){++failures;std::puts("FAIL: peer presence must reject the legacy writer");}
    if(FfxHooks::IsNulWardTeachHookInstalled()){++failures;std::puts("FAIL: legacy writer must remain inactive");}
    if(std::memcmp(before.data(),target,span)!=0){++failures;std::puts("FAIL: peer admission must precede all menu writes");}
    FfxHooks::RemoveNulWardTeachHook(nullptr);
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("Nul Ward legacy coexistence RT1: 3 checks, %d failures\n",failures);
    return failures?1:0;
}
