#pragma once
#include "OmegaT7Native.h"
#include "OmegaLastSealCore.h"
#include <cmath>

namespace FfxHooks::OmegaLastSeal::Native {
namespace Common=OmegaT7::Native;
using Byte=unsigned char;
struct Identity {
    std::uintptr_t actor=0,resource=0,worker=0,model=0;
    unsigned species=0;
};
inline unsigned Species(unsigned role) noexcept {constexpr unsigned ids[]={100,911,912,913,914};return role<5?ids[role]:0;}
inline Identity Identify(std::uintptr_t base,unsigned role) noexcept {
    Identity out;if(role>4)return out;out.actor=Common::Actor(base,20+role);if(!out.actor)return {};
    out.species=SinSpread::ModelFromNative(Common::ReadOr<std::uint16_t>(out.actor+0xE,0xFFFF));
    out.resource=Common::ReadOr<std::uint32_t>(out.actor+0x48);out.worker=Common::ReadOr<std::uint32_t>(out.actor+0xF7C);out.model=Common::ReadOr<std::uint32_t>(out.actor);
    if(out.species!=Species(role)||out.resource<0x10000||out.resource>UINT32_MAX-36)return {};
    const auto size=Common::ReadOr<std::uint32_t>(out.resource+32),workerOffset=Common::ReadOr<std::uint32_t>(out.resource+8),statOffset=Common::ReadOr<std::uint32_t>(out.resource+12);
    // Native resource binding RVA383B00 publishes these exact section pointers.
    // A model alone or a fabricated/stale script reference is not an actor lease.
    if(size<36||size>1024*1024||out.resource>UINT32_MAX-size||workerOffset<36||workerOffset>=size||statOffset<36||statOffset>=size||
       out.worker!=out.resource+workerOffset||Common::ReadOr<std::uint32_t>(out.actor+0xF80)!=out.resource+statOffset)return {};
    return out;
}
inline bool Same(std::uintptr_t base,unsigned role,const Identity& key) noexcept {
    const auto now=Identify(base,role);return key.actor&&now.actor==key.actor&&now.resource==key.resource&&now.worker==key.worker&&now.model==key.model&&now.species==key.species;
}
inline bool Queued(std::uintptr_t base,unsigned id) noexcept {
    const int count=Common::ReadOr<std::int8_t>(base+0xD2BDE1,-1);if(count<0||count>62)return true;
    for(int i=0;i<count;++i)if(Common::ReadOr<Byte>(base+0xD2AC70+72u*static_cast<unsigned>(i),255)==id)return true;
    return false;
}
inline bool Dormant(std::uintptr_t base,unsigned role,const Identity& key,bool dormant) noexcept {
    if(!role||role>4||!Same(base,role,key)||Queued(base,20+role))return false;
    const unsigned id=20+role;
    if(dormant){Common::Property(base,id,0xAE,1);Common::Property(base,id,0x15,0);}
    else{Common::Property(base,id,0x15,1);Common::Property(base,id,0xAE,0);}
    return Same(base,role,key)&&Common::ReadOr<Byte>(key.actor+0xDC8,2)==(dormant?0:1);
}
inline bool Scale(std::uintptr_t base,unsigned role,const Identity& key) noexcept {
    if(!role||role>4||!key.model||!Same(base,role,key))return false;
    const float scale=role<3?0.45f:0.35f;
    using Function=void(__cdecl*)(void*,float,float,float,int);
    reinterpret_cast<Function>(base+0x42B5D0)(reinterpret_cast<void*>(key.model),scale,scale,scale,0);
    return Same(base,role,key);
}
inline bool Revive(std::uintptr_t base,unsigned role,const Identity& key) noexcept {
    if(!role||role>4||!key.model||!Same(base,role,key)||Queued(base,20+role))return false;
    using Function=int(__cdecl*)(unsigned,std::uint32_t,int);
    // The native 7050 leaf retains the allocated slot/WorkerFile, restores
    // status/CTB/model state, and schedules its usual event with no parent wait.
    (void)reinterpret_cast<Function>(base+0x3A89C0)(0,1u<<(20+role),0);
    if(!Same(base,role,key)||!Scale(base,role,key))return false;
    const auto expected=role<3?150000u:75000u;
    return Common::Alive(key.actor)&&Common::ReadOr<unsigned>(key.actor+0x594)==expected&&Common::ReadOr<unsigned>(key.actor+0x5D0)==expected;
}
inline bool Gauge(std::uintptr_t base,const Identity& body,unsigned value) noexcept {
    if(value>100||!Same(base,0,body)||Common::ReadOr<Byte>(body.actor+0x5BD)!=100)return false;
    // Native property 13 clamps zero to one. Only this battle-owned enemy
    // byte is published directly; player/Aeon gauges and save data are excluded.
    const Byte charge=static_cast<Byte>(value);
    return NativeUiSupport::Copy(reinterpret_cast<void*>(body.actor+0x5BC),&charge,1);
}
inline unsigned RawOutcome(std::uintptr_t base) noexcept {
    using Function=unsigned(__cdecl*)();return reinterpret_cast<Function>(base+0x3928D0)();
}
} // namespace FfxHooks::OmegaLastSeal::Native
