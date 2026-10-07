#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: reuse the game's swap, Change list and input-turn consumer.
// This never fabricates formation arrays or grants a second turn-start event.
using FormationSwapFn=int(__cdecl*)(unsigned,unsigned,int,unsigned);
using FormationEscapeFn=void(__cdecl*)(unsigned,Byte*,int);
using FormationTickFn=int(__cdecl*)();
struct Replacement {std::uintptr_t actor=0;std::uint32_t epoch=0;unsigned slot=255;};
Replacement replacementsPending[7]{};
std::atomic<std::uint32_t> formationEpoch{0};
std::uint32_t observedFormationEpoch=0;
thread_local bool formationDispatch=false;
void InvalidateFormationActions() noexcept {
    if(formationEpoch.fetch_add(1)==UINT32_MAX)running=false;
}
bool FormationEpochReady() noexcept {
    const auto now=formationEpoch.load();if(!now)return false;
    if(now!=observedFormationEpoch){for(auto& item:replacementsPending)item={};observedFormationEpoch=now;}
    return true;
}
unsigned FormationSlot(unsigned owner) noexcept {
    unsigned found=255;
    for(unsigned i=0;i<7;++i)if(Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2C895>())+i),255)==owner){
        if(found!=255)return 255;found=i;
    }
    return found;
}
bool ReserveActor(unsigned owner) noexcept {
    auto* actor=Actor(owner);
    if(owner>=7||!actor||!Read<Byte>(actor+0x10)||Read<Byte>(actor+0xDC8)||Read<Byte>(actor+0xDCD)||
       Read<int>(actor+0x5D0)<=0||(Read<std::uint16_t>(actor+0x606)&5)||
       (Read<std::uint16_t>(actor+0x616)&0x100)||Read<Byte>(actor+0xDE6)||Read<Byte>(actor+0xDE7)||
       Read<std::uint16_t>(actor+0x6CC)||FormationSlot(owner)!=255)return false;
    using Allowed=int(__cdecl*)(unsigned);
    // The native Change predicate is zero when allowed. It retains the game's
    // battle-specific membership, action locks and swimming-area restrictions.
    return reinterpret_cast<Allowed>(module + (::FfxHooks::ExecutableProfile::Rva<0x39A090>()))(owner)==0;
}
bool FormationProfile(std::uintptr_t base) noexcept {
    #ifdef FFXHOOKS_TARGET_STEAM_20261001
    static constexpr Span helpers[]={
        {::FfxHooks::ExecutableProfile::Rva<0x3B20F0>(),{0x55,0x8B,0xEC,0x83,0xEC,0x14,0x53,0x0F,0xBE,0x1D,0xE0,0xBD,0x12,0x01,0x89,0x5D},10},
        {::FfxHooks::ExecutableProfile::Rva<0x39A090>(),{0x55,0x8B,0xEC,0xFF,0x75,0x08,0xE8,0x95,0x9F,0xFF,0xFF,0x83,0xC4,0x04,0x83,0xB8}},
        {::FfxHooks::ExecutableProfile::Rva<0x3854D0>(),{0x55,0x8B,0xEC,0x51,0x53,0x56,0x8B,0x75,0x08,0x57,0x81,0xE6,0xFF,0x00,0x00,0x00}},
    };
#else
    static constexpr Span helpers[]={
        {::FfxHooks::ExecutableProfile::Rva<0x3B20F0>(),{0x55,0x8B,0xEC,0x83,0xEC,0x14,0x53,0x0F,0xBE,0x1D,0xE0,0xBD,0x12,0x01,0x89,0x5D},10},
        {::FfxHooks::ExecutableProfile::Rva<0x39A090>(),{0x55,0x8B,0xEC,0xFF,0x75,0x08,0xE8,0x95,0x9F,0xFF,0xFF,0x83,0xC4,0x04,0x83,0xB8}},
        {::FfxHooks::ExecutableProfile::Rva<0x3854D0>(),{0x55,0x8B,0xEC,0x51,0x53,0x56,0x8B,0x75,0x08,0x57,0x81,0xE6,0xFF,0,0,0}},
    };
#endif
    for(const auto& span:helpers){Byte expected[16]{},actual[16]{};std::memcpy(expected,span.bytes,16);
        if(span.relocation!=255){std::uint32_t value=0;std::memcpy(&value,expected+span.relocation,4);
            value+=static_cast<std::uint32_t>(base-0x400000u);std::memcpy(expected+span.relocation,&value,4);}
        if(!Copy(actual,reinterpret_cast<void*>(base+span.rva),16)||std::memcmp(actual,expected,16))return false;
    }
    Byte call[5]{};std::int32_t displacement=0;
    if(!Copy(call,reinterpret_cast<void*>(base + (::FfxHooks::ExecutableProfile::Rva<0x3AEB7F>())),5)||call[0]!=0xE8)return false;
    std::memcpy(&displacement,call+1,4);
    return base + (::FfxHooks::ExecutableProfile::Rva<0x3AEB84>())+static_cast<std::intptr_t>(displacement)==base + (::FfxHooks::ExecutableProfile::Rva<0x3ADAF0>());
}
int CallFormationSwap(unsigned out,unsigned in,int mode,unsigned options){
    const bool previous=formationDispatch;formationDispatch=true;int result=255;
    __try {result=reinterpret_cast<FormationSwapFn>(originals[SwapHook])(out,in,mode,options);}
    __finally {formationDispatch=previous;}
    return result;
}
bool ConsumeIncomingSwitch(unsigned owner,Byte* actor) noexcept {
    const int before=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>())));
    if(before<1||before>62||!actor||Read<Byte>(actor+0xDE6)!=1||Read<std::uint16_t>(actor+0x6CC))return false;
    unsigned index=255;
    for(unsigned i=0;i<static_cast<unsigned>(before);++i){Byte row[8]{};
        if(!Copy(row,reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2AA80>())+8*i),8))return false;
        if(row[0]!=owner)continue;
        if(index!=255||row[1]!=0)return false;index=i;
    }
    if(index==255)return false;
    const Byte old=Read<Byte>(actor+0xDE8);
    __try {
        // Explicit balance ruling: a voluntary switch costs native Attack rank3.
        if(static_cast<Byte>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(actor+0xDE8),3,static_cast<char>(old)))!=old)return false;
        using Consume=int(__cdecl*)(unsigned,unsigned,unsigned,unsigned);
        (void)reinterpret_cast<Consume>(module + (::FfxHooks::ExecutableProfile::Rva<0x3B20F0>()))(owner,index,0,0);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return Actor(owner)==actor&&Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE0>())))==before-1&&
           !Read<Byte>(actor+0xDE6)&&!Read<std::uint16_t>(actor+0x6CC);
}
int __cdecl FormationSwapShim(unsigned out,unsigned in,int mode,unsigned options){
    const auto original=reinterpret_cast<FormationSwapFn>(originals[SwapHook]);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(!Enter()||!On(Feature::TurnCostSwitch)||formationDispatch||caller!=module + (::FfxHooks::ExecutableProfile::Rva<0x3AEB84>())||mode||options)
        return original(out,in,mode,options);
    auto* source=Actor(out);auto* target=Actor(in);const unsigned slot=FormationSlot(out);
    if(out>=7||in>=7||out==in||!source||!target||!Read<Byte>(source+0x10)||
       !Read<Byte>(source+0xDC8)||slot==255||!ReserveActor(in))return 255;
    const int result=CallFormationSwap(out,in,mode,options);
    if(result!=static_cast<int>(in)||Actor(out)!=source||Actor(in)!=target||Read<Byte>(source+0xDC8)||
       !Read<Byte>(target+0xDC8)||Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2C895>())+slot),255)!=in||
       !ConsumeIncomingSwitch(in,target)){
        running=false;if(logger)logger("[ffx-hooks] Vanguard: native switch settlement failed; admission closed\n");
    }
    return result;
}
void __cdecl FormationEscapeShim(unsigned owner,Byte* actor,int removed){
    const bool observe=Enter()&&On(Feature::AutoReinforce)&&!formationDispatch&&FormationEpochReady()&&
        owner<7&&Actor(owner)==actor&&Read<Byte>(actor+0x10)&&Read<Byte>(actor+0xDC8)&&removed!=0;
    const auto epoch=formationEpoch.load();const unsigned slot=observe?FormationSlot(owner):255;
    reinterpret_cast<FormationEscapeFn>(originals[EscapeHook])(owner,actor,removed);
    if(!observe||slot==255||!running.load()||epoch!=formationEpoch.load()||Actor(owner)!=actor||
       Read<Byte>(actor+0xDC8)||!Read<Byte>(actor+0xDCD))return;
    const unsigned status=Read<std::uint16_t>(actor+0x606),extra=Read<std::uint16_t>(actor+0x616);
    if((extra&0x100)||((status&4)&&Read<int>(actor+0x5D0)<=0))
        replacementsPending[owner]={reinterpret_cast<std::uintptr_t>(actor),epoch,slot};
}
void PumpFormationReplacements() noexcept {
    if(!Enter()||!On(Feature::AutoReinforce)||formationDispatch||!FormationEpochReady())return;
    const auto epoch=formationEpoch.load();
    for(unsigned slot=0;slot<7&&running.load();++slot){
        const unsigned owner=Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2C895>())+slot),255);
        if(owner>=7)continue;const auto pending=replacementsPending[owner];
        if(!pending.actor)continue;
        auto* source=Actor(owner);
        if(pending.epoch!=epoch||pending.slot!=slot||reinterpret_cast<std::uintptr_t>(source)!=pending.actor||
           !source||!Read<Byte>(source+0x10)||Read<Byte>(source+0xDC8)||!Read<Byte>(source+0xDCD)){
            replacementsPending[owner]={};continue;
        }
        int count=0;const auto* listed=reinterpret_cast<CastListFn>(originals[CastListHook])(owner,10,&count);
        std::uint16_t candidates[31]{};
        if(!listed||count<0||count>31||!Copy(candidates,listed,static_cast<std::size_t>(count)*2))continue;
        unsigned selected=255;for(int i=0;i<count;++i)if(ReserveActor(candidates[i])){selected=candidates[i];break;}
        if(selected==255)continue;
        auto* target=Actor(selected);const Byte wait=Read<Byte>(target+0x65C);
        replacementsPending[owner]={}; // consume before a reentrant native producer
        const int result=CallFormationSwap(owner,selected,-1,0);
        if(!running.load()||formationEpoch.load()!=epoch)return;
        if(result!=static_cast<int>(selected)||Actor(selected)!=target||!Read<Byte>(target+0xDC8)||
           Read<Byte>(source+0xDC8)||!Read<Byte>(source+0xDCD)||Read<Byte>(target+0xDE6)||
           Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2C895>())+slot),255)!=selected){running=false;return;}
        // Native restore mode creates no free input action but resets wait. Put
        // back only that still-owned zero byte; never overwrite a foreign delay.
        __try {
            if(static_cast<Byte>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(target+0x65C),static_cast<char>(wait),0))!=0){running=false;return;}
        }__except(EXCEPTION_EXECUTE_HANDLER){running=false;return;}
    }
}
int __cdecl FormationSchedulerShim(){
    PumpFormationReplacements();return reinterpret_cast<FormationTickFn>(originals[SchedulerHook])();
}
