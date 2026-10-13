#pragma once
#include "ArcanaCore.h"
#include <array>
#include <cstdint>

namespace FfxHooks::Arcana::Presentation {
inline constexpr unsigned kCardCount=::FfxHooks::Arcana::kCardCount, kGuidePageCount=12, kWorldCard=21;
inline constexpr std::uint8_t kNoCard=255;
using Owned=std::array<std::uint8_t,kCardCount>;

enum class Intro : std::uint8_t {Waiting,Pending,Reading,Complete};
enum class Kind : std::uint8_t {None,Notice,Introduction,Objective,System};
enum class Navigation : std::uint8_t {None,Next,Previous,Skip};

// This state belongs to one save snapshot, never to a process-wide preference.
// Acknowledgements are saved with ownership; an unsaved acknowledgement can replay
// after a crash, but replaying a notice never awards another copy of a card.
struct State {
    std::array<std::uint8_t,kCardCount> pending{};
    std::uint8_t count=0,firstCard=kNoCard,page=0;
    Intro intro=Intro::Waiting;
    bool legacy=false;
    State() noexcept {pending.fill(kNoCard);}
};
struct View {
    Kind kind=Kind::None;
    std::uint8_t card=kNoCard,page=0;
    bool legacy=false;
    // Supplemental native dialogs carry their own save-state revision.
    std::uint64_t revision=0;
};
struct GuidePage {const char* title;const char* text;};

bool Validate(const State&,const Owned&) noexcept;
bool AdoptLegacy(State&,const Owned&) noexcept;
bool ObserveAwards(State&,const Owned& before,const Owned& after) noexcept;
View Current(const State&) noexcept;
bool Acknowledge(State&,const View& expected,Navigation) noexcept;
const GuidePage* Guide(unsigned page) noexcept;

class InputLatch {
public:
    void Reset() noexcept {armed_=false;}
    Navigation Poll(bool focused,bool available,bool confirm,bool back) noexcept;
private:
    bool armed_=false;
};
}
