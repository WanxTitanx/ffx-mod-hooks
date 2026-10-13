#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::CaptureCascade {
// Native terminal-event order, not ascending monster/model ID order.
enum class Aeon : std::uint8_t {Valefor,Ifrit,Ixion,Shiva,Bahamut,Yojimbo,Anima,Sisters,Count};
enum class Tutorial : std::uint8_t {None,Sin,Cascade,Region};
inline constexpr unsigned kFamilies=8,kMaximumActors=8,kNoFamily=255;
struct Stamp {std::uint64_t utc=0;std::uint32_t playSeconds=0;std::array<char,32> build{};};
struct Pages {std::uint64_t queued=0;std::uint8_t page=0;bool complete=false;};
struct State {
    std::uint8_t nativeSeen=0,defeated=0,captured=0,regionPending=0,regionSeen=0,first=kNoFamily;
    std::uint64_t sequence=0,encounters=0,revision=0;
    Pages sin{},cascade{};
    std::array<Stamp,kFamilies> defeats{},captures{};
};
struct View {Tutorial kind=Tutorial::None;std::uint8_t page=0,family=kNoFamily;std::uint64_t revision=0;};
struct Config {bool enabled=false;unsigned chance=750,secondChance=2500;};
struct Candidate {Aeon family=Aeon::Valefor;std::uint8_t count=1;std::array<std::uint16_t,3> monsters{};};
struct Slot {std::uint16_t monster=0;bool replaceable=false;};
struct Selection {bool selected=false;unsigned count=0,groups=0;std::array<std::uint16_t,kMaximumActors> monsters{};std::uint8_t changed=0;};

bool Valid(const State&) noexcept;
State Adopt(std::uint8_t nativeFlags) noexcept;
bool ObserveVictories(State&,std::uint8_t nativeFlags,const Stamp&) noexcept;
bool ObserveCapture(State&,Aeon,const Stamp&) noexcept;
bool ObserveRegion(State&,Aeon,bool macalania) noexcept;
View Current(const State&,Aeon region) noexcept;
bool Acknowledge(State&,const View&,bool previous=false,bool skip=false) noexcept;
unsigned PageCount(Tutorial) noexcept;
unsigned FamilyForMonster(unsigned model) noexcept;
Selection Select(const Config&,const State&,Aeon region,const Candidate*,std::size_t,
                 const Slot*,std::size_t,std::uint32_t entropy) noexcept;
const char* Name(Aeon) noexcept;
}
