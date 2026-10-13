#pragma once
#include "ArcanaCore.h"
#include "ArcanaAcquisitionState.h"

namespace FfxHooks::Arcana::Acquisition {
struct Location {std::uint16_t room=0;float x=0,z=0;};
struct Site {std::uint8_t card=0,stage=0;Location location;const char* hint=nullptr;};
struct Facts {
    std::uint16_t story=0;
    std::uint32_t aeons=0;
    std::uint8_t darkAeons=0;
    bool freeTravel=false,mirror=false,omega=false,wobbly=false,riding=false;
};
enum class TargetKind : std::uint8_t {Card,Recovery,RecoveryMark,CoastalRecord,RouteStart,RoutePost,RouteFinish,Pyrefly,IfritEmblem};
struct Target {TargetKind kind=TargetKind::Card;std::uint8_t card=0,part=0;Location location;};
struct Targets {std::array<Target,16> values{};unsigned count=0;};
enum class ResultKind {None,Progressed,Awarded};
struct Result {ResultKind kind=ResultKind::None;std::uint8_t card=0;const char* message=nullptr;};
inline constexpr float kInteractRadius=35.f,kDisplayRadius=280.f;
const std::array<Site,kStandardCardCount>& Catalog() noexcept;
bool ReadFacts(const unsigned char*,std::size_t,Facts&) noexcept;
bool Eligible(const State&,const Progress&,const Facts&,unsigned card) noexcept;
void ObserveTravel(Progress&,const Facts&,std::uint16_t room) noexcept;
Targets Discover(const State&,const Progress&,const Facts&,std::uint16_t room) noexcept;
Result Interact(State&,Progress&,const Facts&,const Target&,std::uint16_t room,float x,float z,bool riding) noexcept;
}
