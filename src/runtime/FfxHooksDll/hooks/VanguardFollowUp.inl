#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: enqueue at most one reaction Attack per eligible equipped ally.
// Native queue capacity and exact readback are authoritative; never call the
// higher-level wrapper whose failure path can remove a normal actor turn.
bool FollowProfile(std::uintptr_t base) noexcept {
    #ifdef FFXHOOKS_TARGET_STEAM_20261001
    static constexpr Span helpers[]={
        {::FfxHooks::ExecutableProfile::Rva<0x390AE0>(),{0x55,0x8B,0xEC,0xFF,0x75,0x0C,0x8B,0x45,0x08,0xFF,0x35,0x2C,0xA9,0x12,0x01,0x25},11},
        {::FfxHooks::ExecutableProfile::Rva<0x39AD40>(),{0x55,0x8B,0xEC,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xF6,0xE8,0xE0,0x92,0xFF,0xFF}},
        {::FfxHooks::ExecutableProfile::Rva<0x39A090>(),{0x55,0x8B,0xEC,0xFF,0x75,0x08,0xE8,0x95,0x9F,0xFF,0xFF,0x83,0xC4,0x04,0x83,0xB8}},
        {::FfxHooks::ExecutableProfile::Rva<0x3B0BA0>(),{0x55,0x8B,0xEC,0x51,0x53,0x0F,0xBE,0x1D,0xE1,0xBD,0x12,0x01,0x89,0x5D,0xFC,0x83},8},
    };
#else
    static constexpr Span helpers[]={
        {::FfxHooks::ExecutableProfile::Rva<0x390AE0>(),{0x55,0x8B,0xEC,0xFF,0x75,0x0C,0x8B,0x45,0x08,0xFF,0x35,0x2C,0xA9,0x12,0x01,0x25},11},
        {::FfxHooks::ExecutableProfile::Rva<0x39AD40>(),{0x55,0x8B,0xEC,0x53,0x56,0x57,0xFF,0x75,0x08,0x33,0xF6,0xE8,0xE0,0x92,0xFF,0xFF}},
        {::FfxHooks::ExecutableProfile::Rva<0x39A090>(),{0x55,0x8B,0xEC,0xFF,0x75,0x08,0xE8,0x95,0x9F,0xFF,0xFF,0x83,0xC4,0x04,0x83,0xB8}},
        {::FfxHooks::ExecutableProfile::Rva<0x3B0BA0>(),{0x55,0x8B,0xEC,0x51,0x53,0x0F,0xBE,0x1D,0xE1,0xBD,0x12,0x01,0x89,0x5D,0xFC,0x83},8},
    };
#endif
    for(const auto& span:helpers){Byte expected[16]{},actual[16]{};std::memcpy(expected,span.bytes,16);
        if(span.relocation!=255){std::uint32_t value=0;std::memcpy(&value,expected+span.relocation,4);
            value+=static_cast<std::uint32_t>(base-0x400000u);std::memcpy(expected+span.relocation,&value,4);}
        if(!Copy(actual,reinterpret_cast<void*>(base+span.rva),16)||std::memcmp(expected,actual,16))return false;
    }
    return true;
}
bool FreeNativeAttack(Byte row[96]) noexcept {
    const auto table=Read<std::uint32_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2A92C>())));
    if(table<0x10000||table>UINT32_MAX-20)return false;
    Byte header[20]{};if(!Copy(header,reinterpret_cast<void*>(table),20)||Word(header)!=1||Word(header+8)!=0||Word(header+12)!=96||Word(header+14)<96)return false;
    const auto offset=Read<std::uint32_t>(header+16);
    if(offset<20||offset>UINT32_MAX-96||table>UINT32_MAX-offset-96)return false;
    const auto* expected=reinterpret_cast<const Byte*>(table+offset);
    using Entry=const Byte*(__cdecl*)(unsigned,unsigned);
    const auto* actual=reinterpret_cast<Entry>(module + (::FfxHooks::ExecutableProfile::Rva<0x390AE0>()))(0x3000,0);
    if(actual!=expected||!Copy(row,actual,96))return false;
    return !row[0x25]&&!row[0x26]&&(row[0x23]&1)&&!(row[0x20]&0x10)&&!(row[0x1A]&4)&&row[0x2B]==1&&(Read<std::uint32_t>(row+0x1C)&0x40000);
}
void QueueFollowUps(unsigned source,const ActionBinding& binding) noexcept {
    if(!On(Feature::FollowUp)||!binding.single||!binding.targets||(binding.targets&(binding.targets-1)))return;
    unsigned target=0;while(target<31&&!(binding.targets&(1u<<target)))++target;
    auto* victim=Actor(target);
    if(target<18||target>=31||!victim||!Read<Byte>(victim+0xDC8)||Read<int>(victim+0x5D0)<=0||(Read<std::uint16_t>(victim+0x606)&1))return;
    Byte attack[96]{};if(!FreeNativeAttack(attack))return;
    const auto epoch=actionEpoch.load();
    for(unsigned ally=0;ally<18;++ally){
        if(ally==source||ally==7||!running.load()||actionEpoch.load()!=epoch)continue;
        auto* actor=Actor(ally);
        if(!actor||!Read<Byte>(actor+0xDC8)||Read<int>(actor+0x5D0)<=0||Read<Byte>(actor+0x608)||(Read<std::uint16_t>(actor+0x606)&0x305)||Read<Byte>(actor+0xDE7)==255)continue;
        using CanAct=int(__cdecl*)(unsigned);using Learned=int(__cdecl*)(unsigned,unsigned);
        // This native predicate returns zero when allowed, despite its old name.
        if(reinterpret_cast<CanAct>(module + (::FfxHooks::ExecutableProfile::Rva<0x39A090>()))(ally)!=0||reinterpret_cast<Learned>(module + (::FfxHooks::ExecutableProfile::Rva<0x39AD40>()))(ally,0x3000)!=1)continue;
        Effects current{},unused{};ReadEffects(actor,nullptr,current,unused);if(!running.load()||!current[5])continue;
        const int before=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())));if(before<0||before>=62)return;
        Byte proposal[72]{};proposal[0]=static_cast<Byte>(ally);proposal[1]=proposal[3]=1;
        const std::uint16_t command=0x3000,none=255;std::memcpy(proposal+8,&command,2);std::memcpy(proposal+10,&none,2);std::memcpy(proposal+16,&binding.targets,4);
        using Append=int(__cdecl*)(unsigned,const Byte*,unsigned,unsigned,unsigned);
        const int added=reinterpret_cast<Append>(module + (::FfxHooks::ExecutableProfile::Rva<0x3B0BA0>()))(ally,proposal,0,0,1);Byte actual[72]{};
        if(added!=-1||Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())))!=before+1||!Copy(actual,reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2AC70>())+72*before),72)||std::memcmp(actual,proposal,72)){
            running=false;if(logger)logger("[ffx-hooks] Vanguard: follow-up queue readback failed; admission closed\n");return;
        }
        // The token identifies the triggering action; the ally gets its own
        // action token on the first native hit. A missing begin locates a stall
        // before damage/aftermath without touching the scheduler or animation.
        TraceAction("follow-enqueued",ally,binding.token,actual);
    }
}
