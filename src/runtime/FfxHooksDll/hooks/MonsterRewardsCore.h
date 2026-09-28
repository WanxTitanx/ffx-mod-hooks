#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace FfxHooks::MonsterRewards {
inline constexpr unsigned SpeciesCount=4096,MaximumMultiplier=1000;
inline constexpr std::uint32_t NativeAccumulatorCap=999999999;
enum class Kind : unsigned {Ap,Gil};
struct BaseRewards {std::uint16_t gil=0,ap=0,overkillAp=0;};
struct Calculation {std::uint64_t individual=0,combined=0;std::uint32_t applied=0;bool valid=false,clamped=false;};
inline constexpr std::uint32_t SafeBeforeVanilla(Kind kind) noexcept {
    return (static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())-NativeAccumulatorCap)/(kind==Kind::Ap?3u:2u);
}
inline Calculation Calculate(std::uint16_t original,unsigned individual,unsigned general,Kind kind) noexcept {
    if(!individual||individual>MaximumMultiplier||!general||general>100)return {};
    Calculation value{};value.valid=true;value.individual=std::uint64_t(original)*individual;
    value.combined=value.individual*general;const auto limit=SafeBeforeVanilla(kind);
    value.clamped=value.combined>limit;value.applied=value.clamped?limit:static_cast<std::uint32_t>(value.combined);return value;
}
inline bool ObservedGeneral(std::uint16_t original,std::int32_t scaled,unsigned& multiplier) noexcept {
    multiplier=1;
    if(!original)return scaled==0;
    if(scaled<original||static_cast<unsigned>(scaled)%original)return false;
    multiplier=static_cast<unsigned>(scaled)/original;return multiplier<=100;
}
inline bool Species(unsigned rawIdentity,unsigned& species) noexcept {
    if((rawIdentity&0xF000u)!=0x1000u||rawIdentity>0x1FFFu)return false;
    species=rawIdentity&0xFFFu;return true;
}
inline std::uint16_t Word(const unsigned char* p) noexcept {return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));}
inline std::uint32_t Dword(const unsigned char* p) noexcept {return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
inline BaseRewards Decode(const unsigned char* bytes) noexcept {return {Word(bytes),Word(bytes+2),Word(bytes+4)};}
// Source identity: FFX.exe 78ce3439..., RVA3990E0. These stack values are
// consumed before native per-character AP abilities and the Gillionaire stage.
inline bool Adjust(BaseRewards base,bool overkill,unsigned apRate,unsigned gilRate,
                   std::int32_t& nativeAp,std::int32_t& nativeGil) noexcept {
    const auto ap=overkill?base.overkillAp:base.ap;unsigned globalAp=0,globalGil=0;
    if(!ObservedGeneral(ap,nativeAp,globalAp)||!ObservedGeneral(base.gil,nativeGil,globalGil))return false;
    const auto a=Calculate(ap,apRate,globalAp,Kind::Ap),g=Calculate(base.gil,gilRate,globalGil,Kind::Gil);
    if(!a.valid||!g.valid)return false;
    nativeAp=static_cast<std::int32_t>(a.applied);nativeGil=static_cast<std::int32_t>(g.applied);return true;
}
}
