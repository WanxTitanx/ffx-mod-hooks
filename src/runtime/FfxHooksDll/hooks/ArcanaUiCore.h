#pragma once
#include "ArcanaCore.h"

namespace FfxHooks::Arcana::Ui {
enum class Page {Closed,Root,Picker,Transfer,ModeReview,SlotBlocked};
enum class Key {None,Up,Down,PageUp,PageDown,Confirm,Cancel};
enum class Action {None,NativeWeapon,NativeArmor,NativeBack,Equip,Mode};
enum class Sound : unsigned char {None=0,MoveConfirm=1,Error=3,Cancel=4};
struct Command {
    Action action=Action::None;std::uint64_t revision=0,generation=0;
    unsigned actor=0,slot=0;std::int16_t card=kEmpty;bool transfer=false;Mode mode=Mode::Twin;
};
struct View {
    Page page=Page::Closed;std::uintptr_t context=0;std::uint64_t generation=0,revision=0;
    unsigned actor=0,category=0,slot=0,cursor=0,top=0;Error error=Error::None;
    std::int16_t pending=kEmpty;Mode proposed=Mode::Twin;
};
inline constexpr unsigned kPickerRows=kCardCount+2,kVisibleRows=7;
inline constexpr unsigned kUnequipRow=0,kModeRow=1,kFirstCardRow=2;
std::int16_t PickerCard(unsigned row) noexcept;
bool SlotLocked(const State&,unsigned actor,unsigned slot) noexcept;
void Observe(View&,const State&,std::uintptr_t context,std::uint64_t generation,unsigned actor,bool idle) noexcept;
Command Input(View&,const State&,Key) noexcept;
void Result(View&,const State&,Error) noexcept;
Sound Feedback(const View& before,const View& after,Key,const Command&,Error result=Error::None) noexcept;
std::int16_t Preview(const View&,const State&) noexcept;
float LayoutY(std::uint32_t callerRva,float value,Mode) noexcept;
float WorkshopLabelY(float value,Mode) noexcept;
float LayoutX(std::uint32_t callerRva,float value,Mode) noexcept;
struct StatusRow {EffectKind kind=EffectKind::None;std::array<char,96> text{};};
struct StatusRows {unsigned count=0;std::array<StatusRow,24> rows{};};
struct StatusGeometry {float header=0,top=0,pitch=0,height=0;};
struct EquippedSlot {std::int16_t card=kEmpty;bool locked=false;std::array<char,80> text{};};
struct EquippedSlots {unsigned count=0;std::array<EquippedSlot,3> slots{};};
StatusRows BuildStatusRows(const State&,unsigned actor) noexcept;
StatusGeometry StatusLayout(unsigned nativeCapacity,unsigned effectCount) noexcept;
EquippedSlots BuildEquippedSlots(const State&,unsigned actor) noexcept;
}
