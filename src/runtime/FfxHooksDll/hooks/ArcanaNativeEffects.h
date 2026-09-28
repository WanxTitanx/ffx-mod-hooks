#pragma once
#include "ArcanaStore.h"

namespace FfxHooks::Arcana::NativeEffects {
inline constexpr unsigned kPlayerBytes=0x94,kBattleRamBytes=0x1EC;
using Player=std::array<unsigned char,kPlayerBytes>;
using Battle=std::array<unsigned char,kBattleRamBytes>;
struct Shadow {Player baseline{},applied{};std::uint16_t seen=0;bool valid=false;};
struct Projection {bool changed=false,conflict=false;std::uint32_t hp=0,mp=0;};
int ClampRole(std::uint32_t caller) noexcept;
void AdjustClamp(int role,int nativeValue,int minimum,int& maximum,int& value,const Effects&,Shadow&) noexcept;
void MergeFlags(unsigned char* sixBytes,const Effects&) noexcept;
void MergeBattle(Battle&,const Effects&) noexcept;
Projection Project(Player&,const Shadow&) noexcept;
}
