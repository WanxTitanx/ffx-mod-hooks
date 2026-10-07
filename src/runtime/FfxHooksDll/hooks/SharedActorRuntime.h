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

namespace FfxHooks::SharedActor {
using InitializeFunction=int(__cdecl*)(unsigned,unsigned);
struct Observer {
    void (*beforeInitialize)(unsigned mode,unsigned actor) noexcept=nullptr;
    void (*afterInitialize)(unsigned mode,unsigned actor,int result,bool completed) noexcept=nullptr;
};
enum class Slot : unsigned { Elemental,Vanguard,Aeon,NulWard,Count };
inline constexpr unsigned ActorCount=31,MaximumDepth=16,Rva=(::FfxHooks::ExecutableProfile::Rva<0x39B500>());
inline std::array<std::atomic<const Observer*>,static_cast<unsigned>(Slot::Count)> observers{};
inline std::array<std::atomic<unsigned>,ActorCount> initializing{};
inline thread_local unsigned depth=0;
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t imageBase=0;
inline void* original=nullptr;
inline std::array<unsigned char,18> owned{};
inline bool Busy(unsigned actor) noexcept {return actor<ActorCount&&initializing[actor].load()!=0;}
inline bool Subscribe(Slot slot,const Observer* observer) noexcept {
    const auto index=static_cast<unsigned>(slot);
    if(index>=observers.size()||!observer)return false;
    const Observer* expected=nullptr;return observers[index].compare_exchange_strong(expected,observer)||expected==observer;
}
inline bool Unsubscribe(Slot slot,const Observer* observer) noexcept {
    const auto index=static_cast<unsigned>(slot);
    if(index>=observers.size()||!observer)return false;
    const Observer* expected=observer;return observers[index].compare_exchange_strong(expected,nullptr);
}
inline int __cdecl InitializeShim(unsigned mode,unsigned actor){
    std::array<const Observer*,static_cast<unsigned>(Slot::Count)> listeners{};
    const bool bounded=++depth<=MaximumDepth&&actor<ActorCount;
    if(actor<ActorCount)initializing[actor].fetch_add(1);
    if(bounded)for(unsigned i=0;i<listeners.size();++i){
        listeners[i]=observers[i].load();
        if(listeners[i]&&listeners[i]->beforeInitialize)listeners[i]->beforeInitialize(mode,actor);
    }
    int result=0;bool completed=false;
    __try {result=reinterpret_cast<InitializeFunction>(original)(mode,actor);completed=true;}
    __finally {
        for(unsigned i=static_cast<unsigned>(listeners.size());i>0;--i){
            const auto* observer=listeners[i-1];
            if(observer&&observer->afterInitialize)observer->afterInitialize(mode,actor,result,completed);
        }
        if(actor<ActorCount)initializing[actor].fetch_sub(1);
        --depth;
    }
    return result;
}
inline bool Copy(void* out,const void* in,std::size_t size) noexcept {
    __try {std::memcpy(out,in,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    if(installed.load()){
        std::array<unsigned char,18> bytes{};
        return base==imageBase&&Copy(bytes.data(),reinterpret_cast<const void*>(base+Rva),bytes.size())&&bytes==owned;
    }
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!Copy(header,reinterpret_cast<const void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity))return false;
    // SHA78ce3439... / PE32: this entry resolves its second DWORD argument as
    // the actor and clears actor+540..72B before rebuilding native status data.
    constexpr unsigned char expected[]={0x55,0x8B,0xEC,0x83,0xEC,0x10,0x53,0x56,0x57,
                                       0x8B,0x7D,0x0C,0x57,0xE8,0x1E,0x8B,0xFF,0xFF};
    std::array<unsigned char,sizeof(expected)> bytes{};
    if(!Copy(bytes.data(),reinterpret_cast<const void*>(base+Rva),bytes.size())||
       std::memcmp(bytes.data(),expected,bytes.size()))return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    const auto target=base+Rva;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&InitializeShim),&original)!=MH_OK)return false;
    const auto result=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),
                                               MinHookBatch::Owner::SharedActor,&target,1);
    if(result.result!=MinHookBatch::BatchResult::Applied)return false;
    if(!Copy(owned.data(),reinterpret_cast<const void*>(target),owned.size()))return false;
    imageBase=base;installed=true;return true;
#else
    return false;
#endif
}
} // namespace FfxHooks::SharedActor
