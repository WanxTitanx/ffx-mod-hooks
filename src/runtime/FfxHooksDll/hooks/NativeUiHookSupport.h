#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "F8RuntimeCore.h"
#include "EquipmentWorkshopEvidence.h"
#include "MinHookBatchCoordinator.h"
#include <cstdint>
#include <cstring>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
namespace FfxHooks::NativeUiSupport {
inline bool Copy(void* out,const void* source,std::size_t size) noexcept {
    __try{std::memcpy(out,source,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
using OwnedProfile=bool(*)(std::uintptr_t,std::uint32_t,const void*,std::size_t) noexcept;
template<std::size_t N> bool Profile(std::uintptr_t base,const EquipmentWorkshop::Evidence::Span (&spans)[N],OwnedProfile owned=nullptr){
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    for(const auto& span:spans){unsigned char bytes[32]{};
        if(!Copy(bytes,reinterpret_cast<void*>(base+span.rva),32)||
           (!EquipmentWorkshop::Evidence::Matches(span,bytes,base)&&(!owned||!owned(base,span.rva,bytes,32))))return false;}
    return true;
}
template<std::size_t N> bool Install(std::uintptr_t base,const std::uint32_t (&rvas)[N],void* (&replacements)[N],void* (&originals)[N],MinHookBatch::Owner owner,const void* anchor){
#ifdef FFXHOOKS_HAVE_POLYHOOK
    static_assert(N<=MinHookBatch::kMaximumTargets,"bounded native UI batch");
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(anchor),&pin))return false;
    std::uintptr_t targets[N]{};std::size_t count=0;
    for(;count<N;++count){targets[count]=base+rvas[count];if(MH_CreateHook(reinterpret_cast<void*>(targets[count]),replacements[count],&originals[count])!=MH_OK)break;}
    if(count!=N){while(count)MH_RemoveHook(reinterpret_cast<void*>(targets[--count]));return false;}
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),owner,targets,N);
    // A potentially entered trampoline remains pinned even after neutralization.
    // Stop closes logical admission; original code remains callable for the process.
    return report.result==MinHookBatch::BatchResult::Applied;
#else
    (void)base;(void)rvas;(void)replacements;(void)originals;(void)owner;(void)anchor;return false;
#endif
}
}
