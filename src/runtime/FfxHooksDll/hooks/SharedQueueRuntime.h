#pragma once
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "NativeUiHookSupport.h"
#include "../shared/ExecutableProfile.h"
#include <array>
#include <atomic>
#include <mutex>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks::SharedQueue {
using Byte=unsigned char;
using Function=int(__cdecl*)(unsigned,const Byte*,unsigned,unsigned,unsigned);
struct Call {unsigned actor=31;const Byte* packet=nullptr;unsigned priority=0,mode=0,tail=0;};
struct Observer {
    void* (*before)(const Call&,const Byte*& forwarded) noexcept=nullptr;
    void (*after)(void*,const Call&,int,bool) noexcept=nullptr;
};
inline constexpr unsigned MaximumDepth=16,AppendRva=0x3B0B90;
enum class Slot : unsigned {OmegaLastSeal,OmegaT7,Count};
inline constexpr unsigned SlotCount=static_cast<unsigned>(Slot::Count);
inline std::array<std::atomic<const Observer*>,SlotCount> observers{};
inline std::atomic<bool> installed{false};
inline std::mutex installation;
inline std::uintptr_t image=0;
inline void* original=nullptr;
inline std::array<Byte,32> owned{};
inline thread_local unsigned depth=0;
inline bool Register(Slot slot,const Observer* value) noexcept {
    const unsigned index=static_cast<unsigned>(slot);
    if(index>=SlotCount||!value||!value->before||!value->after)return false;
    const Observer* empty=nullptr;return observers[index].compare_exchange_strong(empty,value)||empty==value;
}
inline bool Register(const Observer* value) noexcept {return Register(Slot::OmegaLastSeal,value);}
inline void Unregister(Slot slot,const Observer* value) noexcept {
    const unsigned index=static_cast<unsigned>(slot);if(index<SlotCount)observers[index].compare_exchange_strong(value,nullptr);
}
inline void Unregister(const Observer* value) noexcept {Unregister(Slot::OmegaLastSeal,value);}
inline bool Required() noexcept {for(const auto& value:observers)if(value.load())return true;return false;}
inline bool MatchesOwned(std::uintptr_t base,std::uint32_t rva,const void* bytes,std::size_t size) noexcept {
    return installed.load()&&image==base&&rva==AppendRva&&bytes&&size==owned.size()&&!std::memcmp(bytes,owned.data(),owned.size());
}
inline int __cdecl Shim(unsigned actor,const Byte* packet,unsigned priority,unsigned mode,unsigned tail){
    const Call call{actor,packet,priority,mode,tail};const Byte* forwarded=packet;
    std::array<const Observer*,SlotCount> listeners{};std::array<void*,SlotCount> tokens{};std::array<Call,SlotCount> calls{};
    const bool bounded=++depth<=MaximumDepth;
    int result=0;bool complete=false;
    __try{
        if(bounded)for(unsigned i=0;i<SlotCount;++i){
            listeners[i]=observers[i].load();if(!listeners[i])continue;
            calls[i]=call;calls[i].packet=forwarded;const Byte* proposed=forwarded;
            tokens[i]=listeners[i]->before(calls[i],proposed);
            if(tokens[i]&&proposed)forwarded=proposed;
        }
        result=reinterpret_cast<Function>(original)(actor,forwarded,priority,mode,tail);complete=true;
    }
    __finally{for(unsigned i=SlotCount;i>0;--i)if(tokens[i-1]&&listeners[i-1])listeners[i-1]->after(tokens[i-1],calls[i-1],result,complete);--depth;}
    return result;
}
inline bool Start(std::uintptr_t base){
    std::lock_guard<std::mutex> lock(installation);
    if(!ExecutableProfile::Steam20261001||!Required())return false;
    if(installed.load()){std::array<Byte,32> actual{};return image==base&&NativeUiSupport::Copy(actual.data(),reinterpret_cast<void*>(base+AppendRva),actual.size())&&actual==owned;}
    unsigned char header[0x1000]{};F8Runtime::ExecutableIdentity identity{};
    if(!NativeUiSupport::Copy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||!F8Runtime::IsSupportedExecutable(identity))return false;
    std::array<Byte,32> expected{{0x55,0x8B,0xEC,0x51,0x53,0x0F,0xBE,0x1D,0,0,0,0,0x89,0x5D,0xFC,0x83,
        0xFB,0x3E,0x7D,0x69,0x56,0x57,0xFF,0x75,0x08,0xE8,0x72,0x34,0xFE,0xFF,0xFE,0x80}},actual{};
    const auto count=static_cast<std::uint32_t>(base+0xD2BDE1);std::memcpy(expected.data()+8,&count,4);
    if(!NativeUiSupport::Copy(actual.data(),reinterpret_cast<void*>(base+AppendRva),actual.size())||actual!=expected)return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return false;
    HMODULE pin=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pin))return false;
    const auto target=base+AppendRva;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&Shim),&original)!=MH_OK)return false;
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::SharedQueue,&target,1);
    if(report.result!=MinHookBatch::BatchResult::Applied)return false;
    if(!NativeUiSupport::Copy(owned.data(),reinterpret_cast<void*>(target),owned.size()))return false;
    image=base;installed=true;return true;
#else
    return false;
#endif
}
} // namespace FfxHooks::SharedQueue
