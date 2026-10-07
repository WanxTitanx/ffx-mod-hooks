#include "../shared/ExecutableProfile.h"
#pragma once
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <windows.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks::SharedAction {
using ResultFunction=int(__cdecl*)(unsigned,unsigned,unsigned,int*,void*);
using FinishFunction=int(__cdecl*)(unsigned,unsigned,unsigned);
struct ResultCall {unsigned source=0,sub=0,target=0;int* output=nullptr;void* descriptors=nullptr;};
struct FinishCall {unsigned owner=0,index=0,preserve=0;};
struct Observer {
    void* (*beforeResult)(const ResultCall&) noexcept=nullptr;
    void (*afterResult)(void*,const ResultCall&,int,bool) noexcept=nullptr;
    void* (*beforeFinish)(const FinishCall&) noexcept=nullptr;
    void (*afterFinish)(void*,const FinishCall&,int,bool) noexcept=nullptr;
};
struct Legacy {
    ResultFunction result=nullptr;FinishFunction finish=nullptr;
    void (*afterObserversFinish)(const FinishCall&,int,bool) noexcept=nullptr;
};
enum class Slot : unsigned {Elemental,Aeon,NulWard,Reserved,Count};
inline constexpr unsigned SlotCount=static_cast<unsigned>(Slot::Count),MaximumDepth=16;
inline std::array<std::atomic<const Observer*>,SlotCount> observers{};
inline std::atomic<const Legacy*> legacy{nullptr};
inline std::atomic<bool> installed{false};
inline std::mutex installationMutex;
inline std::uintptr_t imageBase=0;
inline void* originals[2]{};
inline std::array<std::array<unsigned char,16>,2> owned{};
inline constexpr unsigned rvas[2]={(::FfxHooks::ExecutableProfile::Rva<0x38F0B0>()),(::FfxHooks::ExecutableProfile::Rva<0x3B0870>())};
inline thread_local unsigned resultDepth=0,finishDepth=0;
inline bool Subscribe(Slot slot,const Observer* value) noexcept {
    const unsigned index=static_cast<unsigned>(slot);
    if(index>=SlotCount||!value)return false;
    const Observer* empty=nullptr;
    return observers[index].compare_exchange_strong(empty,value)||empty==value;
}
inline bool Unsubscribe(Slot slot,const Observer* value) noexcept {
    const unsigned index=static_cast<unsigned>(slot);if(index>=SlotCount||!value)return false;
    const Observer* expected=value;return observers[index].compare_exchange_strong(expected,nullptr);
}
inline bool RegisterLegacy(const Legacy* value) noexcept {
    if(!value||!value->result||!value->finish)return false;
    const Legacy* empty=nullptr;return legacy.compare_exchange_strong(empty,value)||empty==value;
}
inline bool UnregisterLegacy(const Legacy* value) noexcept {
    if(!value)return false;const Legacy* expected=value;return legacy.compare_exchange_strong(expected,nullptr);
}
inline ResultFunction OriginalResult() noexcept {return reinterpret_cast<ResultFunction>(originals[0]);}
inline FinishFunction OriginalFinish() noexcept {return reinterpret_cast<FinishFunction>(originals[1]);}
inline int __cdecl ResultShim(unsigned source,unsigned sub,unsigned target,int* output,void* descriptors){
    const ResultCall call{source,sub,target,output,descriptors};
    std::array<const Observer*,SlotCount> listeners{};std::array<void*,SlotCount> tokens{};
    const bool bounded=++resultDepth<=MaximumDepth;
    if(bounded)for(unsigned i=0;i<SlotCount;++i){
        const auto* value=observers[i].load();listeners[i]=value;
        if(value&&value->beforeResult)tokens[i]=value->beforeResult(call);
    }
    int result=0;bool completed=false;
    __try {
        const auto* value=legacy.load();
        const auto original=value?value->result:OriginalResult();
        result=original(source,sub,target,output,descriptors);completed=true;
    }__finally {
        for(unsigned i=SlotCount;i>0;--i){const auto* value=listeners[i-1];
            if(value&&tokens[i-1]&&value->afterResult)value->afterResult(tokens[i-1],call,result,completed);}
        --resultDepth;
    }
    return result;
}
inline int __cdecl FinishShim(unsigned owner,unsigned index,unsigned preserve){
    const FinishCall call{owner,index,preserve};
    const auto* legacyCallbacks=legacy.load();
    std::array<const Observer*,SlotCount> listeners{};std::array<void*,SlotCount> tokens{};
    const bool bounded=++finishDepth<=MaximumDepth;
    if(bounded)for(unsigned i=0;i<SlotCount;++i){
        const auto* value=observers[i].load();listeners[i]=value;
        if(value&&value->beforeFinish)tokens[i]=value->beforeFinish(call);
    }
    int result=0;bool completed=false;
    __try {
        const auto original=legacyCallbacks?legacyCallbacks->finish:OriginalFinish();
        result=original(owner,index,preserve);completed=true;
    }__finally {
        for(unsigned i=SlotCount;i>0;--i){const auto* value=listeners[i-1];
            if(value&&tokens[i-1]&&value->afterFinish)value->afterFinish(tokens[i-1],call,result,completed);}
        // Consumers must witness native removal before a legacy adapter appends
        // new work. Their queue-count and action-retirement proofs depend on it.
        if(legacyCallbacks&&legacyCallbacks->afterObserversFinish)
            legacyCallbacks->afterObserversFinish(call,result,completed);
        --finishDepth;
    }
    return result;
}
inline bool Copy(void* out,const void* in,std::size_t size) noexcept {
    __try {std::memcpy(out,in,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool MatchesOwned(std::uintptr_t base,unsigned rva,const unsigned char* bytes) noexcept {
    if(!installed.load()||base!=imageBase||!bytes)return false;
    for(unsigned i=0;i<2;++i)if(rvas[i]==rva)return std::memcmp(bytes,owned[i].data(),owned[i].size())==0;
    return false;
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installationMutex);
    if(installed.load()){
        if(base!=imageBase)return false;
        for(unsigned i=0;i<2;++i){unsigned char bytes[16]{};
            if(!Copy(bytes,reinterpret_cast<const void*>(base+rvas[i]),16)||!MatchesOwned(base,rvas[i],bytes))return false;}
        return true;
    }
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<const void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    unsigned char expected[2][16]={
        {0x55,0x8B,0xEC,0x83,0xEC,0x44,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xC0,0x33,0xC9},
        {0x55,0x8B,0xEC,0x83,0xEC,0x14,0x0F,0xBE,0x05,0,0,0,0,0x89,0x45,0xF4}};
    const auto queueCount=static_cast<std::uint32_t>(base + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>()));std::memcpy(expected[1]+9,&queueCount,4);
    for(unsigned i=0;i<2;++i){unsigned char bytes[16]{};
        if(!Copy(bytes,reinterpret_cast<const void*>(base+rvas[i]),16)||std::memcmp(bytes,expected[i],16))return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    const std::uintptr_t targets[2]={base+rvas[0],base+rvas[1]};
    void* replacements[2]={reinterpret_cast<void*>(&ResultShim),reinterpret_cast<void*>(&FinishShim)};
    unsigned created=0;
    for(;created<2;++created)if(MH_CreateHook(reinterpret_cast<void*>(targets[created]),replacements[created],&originals[created])!=MH_OK)break;
    if(created!=2){while(created)MH_RemoveHook(reinterpret_cast<void*>(targets[--created]));return false;}
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),
        MinHookBatch::Owner::SharedAction,targets,2);
    if(report.result!=MinHookBatch::BatchResult::Applied)return false;
    for(unsigned i=0;i<2;++i)if(!Copy(owned[i].data(),reinterpret_cast<const void*>(targets[i]),16))return false;
    imageBase=base;installed=true;return true;
#else
    return false;
#endif
}
} // namespace FfxHooks::SharedAction
