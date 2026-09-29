#pragma once
#include <array>
#include <cstdint>

// Compatibility policy informed by cxldalyy/playable-seymour-mod, MIT,
// copyright 2026 cxldalyy. See third_party/licenses/playable-seymour-mod.txt.
// Native operations/bounds are verified against FFX.exe. No campaign or assets.
namespace FfxHooks::SeymourCompatibility {
inline constexpr std::uint32_t kCommandSafety=1,kGearVisibility=2;
inline constexpr unsigned kSeymour=7,kInventory=200,kGearBytes=0x16;
struct Scope {
    std::uint64_t epoch=0;
    std::uint32_t commandGeneration=0,thread=0;
    // Runtime lease identity, not serialized save data.
    std::uintptr_t actorTable=0;
    bool Valid() const noexcept {
        const auto battle=static_cast<std::uint32_t>(epoch>>32);
        return battle&&battle!=UINT32_MAX&&commandGeneration&&thread&&
               static_cast<std::uint32_t>(epoch)==thread;
    }
    friend bool operator==(const Scope& a,const Scope& b) noexcept {
        return a.epoch==b.epoch&&a.commandGeneration==b.commandGeneration&&
               a.thread==b.thread&&a.actorTable==b.actorTable;
    }
};
inline bool UnsafeCommand(std::uint32_t command) noexcept {
    // Upstream main.cs marks these unsupported actions. Query filtering avoids
    // altering saved or learned ability bitfields.
    return command==0x3017||command==0x3024||command==0x3025||
           command==0x3026||command==0x302a;
}
inline int FilterCommand(int nativeResult,int actor,std::uint32_t command,
                         bool requested,const Scope& before,const Scope& after) noexcept {
    if(nativeResult!=0||!requested||actor!=7||!before.Valid()||!(before==after))return nativeResult;
    return UnsafeCommand(command)?1:nativeResult;
}
inline std::uint8_t VisibilityFlags(std::uint8_t flags,std::uint8_t enable) noexcept {
    // VA 007AD632 uses the low bit of BL, not a general nonzero boolean.
    return static_cast<std::uint8_t>((flags&~2u)|((enable&1u)<<1));
}
using Gear=std::array<std::uint8_t,kGearBytes>;
struct Pair {
    Scope scope{};
    std::array<std::uint8_t,2> slots{255,255};
    std::array<Gear,2> gear{};
    bool Valid() const noexcept {
        if(!scope.Valid()||(slots[0]!=255&&slots[0]==slots[1]))return false;
        for(unsigned i=0;i<2;++i){
            if(slots[i]==255)continue;
            const auto& g=gear[i];
            if(slots[i]>=kInventory||g[2]!=1||g[4]!=kSeymour||g[5]!=i||g[6]!=kSeymour)return false;
        }
        return true;
    }
    friend bool operator==(const Pair& a,const Pair& b) noexcept {
        return a.scope==b.scope&&a.slots==b.slots&&a.gear==b.gear;
    }
};
enum class Outcome : std::uint8_t {Inactive,Unchanged,Applied,Rejected,RolledBack,Partial};
struct Io {
    void* context=nullptr;
    bool (*read)(void*,Pair&)=nullptr;
    bool (*current)(void*,const Pair&)=nullptr;
    // False means no write. Compare full record and current session/indices
    // before changing only the visibility byte with compare/exchange.
    bool (*compareFlag)(void*,unsigned,const Gear&,std::uint8_t)=nullptr;
};
inline Outcome ApplyVisibility(bool requested,int actor,std::uint8_t enable,const Io& io) noexcept {
    if(!requested||actor!=7)return Outcome::Inactive;
    if(!io.read||!io.current||!io.compareFlag)return Outcome::Rejected;
    std::array<bool,2> changed{};Pair before{};auto applied=before.gear;
    try {
        if(!io.read(io.context,before)||!before.Valid()||!io.current(io.context,before))return Outcome::Rejected;
        applied=before.gear;
        for(unsigned i=0;i<2;++i){
            if(before.slots[i]==255)continue;
            const auto value=VisibilityFlags(before.gear[i][3],enable);
            if(value==before.gear[i][3])continue;
            if(!io.current(io.context,before)||!io.compareFlag(io.context,i,before.gear[i],value)){
                bool restored=true;
                for(unsigned j=i;j-->0;){
                    if(changed[j]&&(!io.current(io.context,before)||
                       !io.compareFlag(io.context,j,applied[j],before.gear[j][3])))restored=false;
                }
                return restored?Outcome::RolledBack:Outcome::Partial;
            }
            applied[i][3]=value;changed[i]=true;
        }
        if(!io.current(io.context,before))return (changed[0]||changed[1])?Outcome::Partial:Outcome::Rejected;
        // A successful write callback is not final ownership proof. Read both
        // records again and do not report Applied after metadata/readback loss.
        Pair observed{};
        if(!io.read(io.context,observed)||!observed.Valid()||
           !(observed.scope==before.scope)||observed.slots!=before.slots||
           observed.gear!=applied||!io.current(io.context,before))
            return (changed[0]||changed[1])?Outcome::Partial:Outcome::Rejected;
        return (changed[0]||changed[1])?Outcome::Applied:Outcome::Unchanged;
    }catch(...){return (changed[0]||changed[1])?Outcome::Partial:Outcome::Rejected;}
}
} // namespace FfxHooks::SeymourCompatibility
