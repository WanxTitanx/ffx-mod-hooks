#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: this native helper exclusively handles percentage immunity.
// The explicit profile override bypasses only that decision for its current
// admitted HP call; formula, status bits and all other target state stay intact.
using GravityImmunityFunction=int(__cdecl*)(const void*,unsigned,unsigned*,unsigned,unsigned*,int);
GravityImmunityFunction gravityImmunityOriginal=nullptr;
bool gravityImmunityInstalled=false;
int __cdecl GravityImmunityShim(const void* target,unsigned formula,unsigned* components,
                               unsigned component,unsigned* blocked,int amount){
    const auto* frame=CurrentFrame(nullptr,target);
    if(frame&&frame->gravity&&frame->gravityOverride&&component==1&&(formula==5||formula==8))return amount;
    return gravityImmunityOriginal(target,formula,components,component,blocked,amount);
}
bool StartGravity(){
    if(gravityImmunityInstalled)return true;
    constexpr Byte expected[]={0x55,0x8B,0xEC,0x8B,0x45,0x0C,0x83,0xF8,0x08,
                               0x74,0x05,0x83,0xF8,0x05,0x75,0x1F};
    Byte bytes[sizeof(expected)]{};
    if(!Copy(bytes,reinterpret_cast<const void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x38AE40>())),sizeof(bytes))||
       std::memcmp(bytes,expected,sizeof(bytes)))return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    const auto target=module + (::FfxHooks::ExecutableProfile::Rva<0x38AE40>());void* original=nullptr;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&GravityImmunityShim),&original)!=MH_OK)return false;
    gravityImmunityOriginal=reinterpret_cast<GravityImmunityFunction>(original);
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),
                                               MinHookBatch::Owner::ElementalRuntime,&target,1);
    gravityImmunityInstalled=result.result==MinHookBatch::BatchResult::Applied;
    return gravityImmunityInstalled;
#else
    return false;
#endif
}
