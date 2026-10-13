#pragma once
// Jarvis-HOOK: Steam-only native leaves. Caller owns battle/actor admission.
#include "OmegaT7Config.h"
#include "NativeUiHookSupport.h"
#include "SharedActorRuntime.h"
#include "SharedCombatRuntime.h"
#include "SinSpreadCore.h"
#include <cstring>
namespace FfxHooks::OmegaT7::Native {
using Byte=unsigned char;
inline unsigned Word(const Byte* p) noexcept {return p[0]|(unsigned(p[1])<<8);}
inline std::uint32_t Dword(const Byte* p) noexcept {return Word(p)|(std::uint32_t(Word(p+2))<<16);}
template<class T> inline bool Read(std::uintptr_t at,T& value) noexcept {return NativeUiSupport::Copy(&value,reinterpret_cast<const void*>(at),sizeof(value));}
template<class T> inline T ReadOr(std::uintptr_t at,T fallback={}) noexcept {T value=fallback;return Read(at,value)?value:fallback;}
inline std::uintptr_t Actor(std::uintptr_t base,unsigned id) noexcept {
    if(id>=31||SharedActor::Busy(id))return 0;
    const auto root=ReadOr<std::uint32_t>(base+ExecutableProfile::Rva<0xD334CC>());
    return root>=0x10000&&root<=UINT32_MAX-31*0xF90u?root+id*0xF90u:0;
}
inline bool Alive(std::uintptr_t actor) noexcept {
    return actor&&ReadOr<Byte>(actor+0xDC8)&&!ReadOr<Byte>(actor+0xDCD,1)&&!ReadOr<Byte>(actor+0xDCE,1)&&
        !(ReadOr<std::uint16_t>(actor+0x606,5)&5)&&ReadOr<int>(actor+0x5D0)>0;
}
inline bool BattleOngoing(std::uintptr_t base) noexcept {
    // The native battle-active flag stays set during the exit animation.
    // Its authoritative outcome already distinguishes ongoing (0) from
    // defeat, victory, escape and scripted termination during that interval.
    __try {return reinterpret_cast<unsigned(__cdecl*)()>(base+0x3928D0)()==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline Vitals Life(std::uintptr_t actor) noexcept {
    Vitals v;v.active=Alive(actor);if(!v.active)return v;
    const auto maximum=ReadOr<int>(actor+0x594);if(maximum<=0)return v;
    v.hp=ReadOr<unsigned>(actor+0x5D0);v.maximum=static_cast<unsigned>(maximum);v.zombie=(ReadOr<std::uint16_t>(actor+0x606)&2)!=0;
    for(unsigned i=0;i<9;++i)if(ReadOr<Byte>(actor+BuffOffsets[i]))v.buffs|=1u<<i;
    for(unsigned i=0;i<4;++i)v.stacks[i]=ReadOr<Byte>(actor+0x65E+NativeStackIndex[i]);
    return v;
}
inline void Property(std::uintptr_t base,unsigned id,unsigned field,int value) noexcept {
    using Fn=int(__cdecl*)(unsigned,unsigned,int,int);(void)reinterpret_cast<Fn>(base+0x3B4B70)(id,field,-13,value);
}
inline void Grant(std::uintptr_t base,unsigned id,std::uintptr_t actor,unsigned buffs) noexcept {
    if(!Alive(actor))return;
    for(unsigned i=0;i<9;++i)if((buffs&(1u<<i))&&!ReadOr<Byte>(actor+BuffOffsets[i]))Property(base,id,PropertyIndex[i],i>=5?1:254);
}
inline void Stacks(std::uintptr_t base,unsigned id,std::uintptr_t actor,const std::array<unsigned,4>& changes) noexcept {
    if(!Alive(actor))return;
    using Fn=void(__cdecl*)(unsigned,void*,const Byte*);
    for(unsigned i=0;i<4;++i)if(changes[i]){
        CommandRow row{};row[0x56]=static_cast<Byte>(1u<<NativeStackIndex[i]);row[0x59]=static_cast<Byte>(changes[i]>5?5:changes[i]);
        // Native 38D730 performs the same saturating 0..5 counter update as
        // Cheer/Focus/Reflex/Luck; no derived base attributes are rewritten.
        reinterpret_cast<Fn>(base+0x38D730)(id,reinterpret_cast<void*>(actor),row.data());
    }
}
inline void Heal(std::uintptr_t base,unsigned id,std::uintptr_t actor,unsigned percent) noexcept {
    const auto life=Life(actor);if(!life.hp||!percent||percent>100||!SharedCombat::OriginalHp())return;
    const auto amount=static_cast<int>(std::uint64_t(life.maximum)*percent/100);
    if(!amount)return;
    SharedCombat::OriginalHp()(id,reinterpret_cast<Byte*>(actor),life.zombie?amount:-amount,0,0,0,0);
    if(life.zombie&&ReadOr<int>(actor+0x5D0)<=0)Property(base,id,4,1);
}
inline void Advance(std::uintptr_t base,unsigned id,std::uintptr_t actor,unsigned percent) noexcept {
    if(!Alive(actor)||!percent||percent>100)return;
    const unsigned before=ReadOr<Byte>(actor+0x65C),after=before*(100-percent)/100;
    if(after==before)return;
    using Fn=int(__cdecl*)(unsigned,void*,int,int,int,int);
    (void)reinterpret_cast<Fn>(base+0x38E1E0)(id,reinterpret_cast<void*>(actor),static_cast<int>(after)-static_cast<int>(before),0,0,0x81);
}
inline void Slow(std::uintptr_t base,unsigned source,std::uintptr_t caster,unsigned target,std::uintptr_t victim,const CommandRow& command) noexcept {
    if(!Alive(caster)||!Alive(victim)||command[0x2E+24]>100||!command[0x2E+24])return;
    std::array<Byte,44> status{};std::array<unsigned,64> counters{},aux{};unsigned flags=0,changed=0;
    if(!NativeUiSupport::Copy(status.data()+7,reinterpret_cast<void*>(victim+0x608),13)||
       !NativeUiSupport::Copy(status.data()+20,reinterpret_cast<void*>(victim+0x606),2)||
       !NativeUiSupport::Copy(status.data()+22,reinterpret_cast<void*>(victim+0x616),2))return;
    const Byte haste=status[18],slow=status[19];
    using Fn=int(__cdecl*)(unsigned,Byte*,unsigned,Byte*,const Byte*,void*,void*,Byte*,int,unsigned*,unsigned*);
    (void)reinterpret_cast<Fn>(base+0x38AE00)(source,reinterpret_cast<Byte*>(caster),target,reinterpret_cast<Byte*>(victim),command.data(),counters.data(),aux.data(),status.data(),0,&flags,&changed);
    // Only accepted native deltas are published. A failed resistance/Ribbon/
    // Proof calculation cannot become a forced property-setter debuff.
    if(status[19]!=slow)Property(base,target,0x39,status[19]);
    if(status[18]!=haste)Property(base,target,0x38,status[18]);
}
inline void BasicAbsorb(std::uintptr_t base,unsigned id,unsigned bit,bool enabled) noexcept {
    if(!bit||(bit&(bit-1))||(bit&~15u))return;
    unsigned index=0;while((1u<<index)!=bit)++index;Property(base,id,0x62+index,enabled?1:0);
}
inline unsigned Elements(unsigned native) noexcept {return (native&31u)|((native&128u)?Dark:0);}
inline bool Command(std::uintptr_t base,unsigned id,CommandRow& out) noexcept {
    unsigned table=0,width=0;
    switch(id>>12){
        case 2:table=ExecutableProfile::Rva<0xD2A940>();width=96;break;
        case 3:table=ExecutableProfile::Rva<0xD2A92C>();width=96;break;
        case 4:table=ExecutableProfile::Rva<0xD2A930>();width=92;break;
        case 6:table=ExecutableProfile::Rva<0xD2A934>();width=92;break;
        default:return false;
    }
    const auto root=ReadOr<std::uint32_t>(base+table);std::array<Byte,20> header{};
    if(root<0x10000||!Read(root,header)||Word(header.data())!=1||Word(header.data()+8)||Word(header.data()+12)!=width)return false;
    const unsigned count=Word(header.data()+10)+1u,index=id&4095,offset=Dword(header.data()+16);
    if(index>=count||count>4096||offset<20||offset>UINT32_MAX-count*width||root>UINT32_MAX-offset-count*width)return false;
    return NativeUiSupport::Copy(out.data(),reinterpret_cast<void*>(root+offset+index*width),out.size());
}
}
