#pragma once
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "RecoveryEvidence.generated.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::RecoveryNative {
inline bool Copy(void* output,const void* input,std::size_t size) noexcept {
    if(!output||!input||!size)return false;
    __try {std::memcpy(output,input,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool Range(std::uintptr_t address,std::size_t size,std::uintptr_t image=0,
                  bool executable=false,bool writable=false) noexcept {
    if(!address||!size||size>UINT32_MAX||address>UINT32_MAX-size)return false;
    const auto end=address+size;
    while(address<end){
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info))||
           info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        if(image&&(info.Type!=MEM_IMAGE||reinterpret_cast<std::uintptr_t>(info.AllocationBase)!=image))return false;
        const auto p=info.Protect&0xff;
        const bool code=p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        const bool write=p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        if(!(code||write||p==PAGE_READONLY)||(executable&&!code)||(writable&&!write))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(next<=address)return false;
        address=next;
    }
    return true;
}
inline bool Profile(std::uintptr_t base) noexcept {
    std::array<std::uint8_t,4096> header{};F8Runtime::ExecutableIdentity identity{};
    return Range(base,header.size(),base)&&Copy(header.data(),reinterpret_cast<void*>(base),header.size())&&
        F8Runtime::ParseExecutableIdentity(header.data(),header.size(),&identity)==F8Runtime::ProfileResult::Supported&&
        F8Runtime::IsSupportedExecutable(identity)&&base<=UINT32_MAX-identity.sizeOfImage;
}
inline bool Match(std::uintptr_t base,const RecoveryEvidence::Proof& proof) noexcept {
    std::array<std::uint8_t,256> actual{},expected{};
    if(!proof.size||proof.size>actual.size()||proof.rva>=0x237d000u||proof.size>0x237d000u-proof.rva||
       base>UINT32_MAX-proof.rva-proof.size||!Range(base+proof.rva,proof.size,base,true)||
       !Copy(actual.data(),reinterpret_cast<void*>(base+proof.rva),proof.size))return false;
    std::memcpy(expected.data(),proof.bytes,proof.size);
    for(std::size_t i=0;i<proof.relocationCount;++i){
        const auto at=static_cast<std::size_t>(proof.relocations[i]);if(at+4>proof.size)return false;
        std::uint32_t value=0;std::memcpy(&value,expected.data()+at,4);
        value+=static_cast<std::uint32_t>(base-0x400000u);std::memcpy(expected.data()+at,&value,4);
    }
    return std::memcmp(actual.data(),expected.data(),proof.size)==0;
}
inline bool EnvironmentEnabled(const char* name) noexcept {
    char value[16]{};const DWORD n=GetEnvironmentVariableA(name,value,sizeof(value));
    return n>0&&n<sizeof(value)&&(value[0]=='1'||value[0]=='y'||value[0]=='Y'||value[0]=='t'||value[0]=='T');
}

// Callback code, originals and generated gateways are retained after any possible
// publication. A zero callback count does not cover threads paused in the prologue.
class OwnedBatch {
public:
    bool Add(std::uintptr_t target,void* replacement,void** original) noexcept {
#ifdef FFXHOOKS_HAVE_POLYHOOK
        if(attempted_||count_==targets_.size()||!target||!replacement||!original)return false;
        for(std::size_t i=0;i<count_;++i)if(targets_[i]==target)return false;
        if(MH_CreateHook(reinterpret_cast<void*>(target),replacement,original)!=MH_OK)return false;
        targets_[count_++]=target;
        return *original!=nullptr;
#else
        (void)target;(void)replacement;(void)original;return false;
#endif
    }
    bool Publish(MinHookBatch::Owner owner,const void* moduleAddress) noexcept {
#ifdef FFXHOOKS_HAVE_POLYHOOK
        if(attempted_||!count_||!moduleAddress)return false;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
              reinterpret_cast<LPCSTR>(moduleAddress),&pinned))return false;
        owner_=owner;attempted_=true;
        const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),
            MinHookBatch::RuntimeBatchIo(),owner_,targets_.data(),count_);
        active_=report.result==MinHookBatch::BatchResult::Applied;
        return active_;
#else
        (void)owner;(void)moduleAddress;return false;
#endif
    }
    bool DiscardUnpublished() noexcept {
#ifdef FFXHOOKS_HAVE_POLYHOOK
        if(attempted_)return false;
        while(count_){
            if(MH_RemoveHook(reinterpret_cast<void*>(targets_[count_-1]))!=MH_OK)return false;
            targets_[--count_]=0;
        }
        return true;
#else
        return true;
#endif
    }
    bool Neutralize() noexcept {
#ifdef FFXHOOKS_HAVE_POLYHOOK
        if(!attempted_)return DiscardUnpublished();
        if(!count_)return true;
        const auto report=MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),
            MinHookBatch::RuntimeBatchIo(),owner_,targets_.data(),count_);
        active_=false;
        // Exact disables cannot prove that a failed global queue operation or
        // drain fence completed. Keep the failure visible and retain callbacks.
        return report.result==MinHookBatch::BatchResult::Neutralized &&
            report.neutralized && report.exactDisabled;
#else
        return true;
#endif
    }
    bool RetainsCode() const noexcept {return attempted_||count_!=0;}
    bool Active() const noexcept {return active_;}
private:
    std::array<std::uintptr_t,16> targets_{};
    std::size_t count_=0;
    bool attempted_=false,active_=false;
    MinHookBatch::Owner owner_=MinHookBatch::Owner::None;
};
inline void* AllocateCode(std::size_t size) noexcept {
    return size&&size<=4096?VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE):nullptr;
}
inline bool SealCode(void* address,std::size_t size) noexcept {
    DWORD previous=0;
    return address&&size&&VirtualProtect(address,size,PAGE_EXECUTE_READ,&previous)&&
        FlushInstructionCache(GetCurrentProcess(),address,size);
}
inline bool Pin(const void* address) noexcept {
    HMODULE module=nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCSTR>(address),&module)!=FALSE;
}
} // namespace FfxHooks::RecoveryNative
