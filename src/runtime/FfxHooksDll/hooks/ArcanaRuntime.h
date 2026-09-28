#pragma once
#include "ArcanaNativeUi.h"
#include "ArcanaNativeEffects.h"

namespace FfxHooks::Arcana::Runtime {
enum class Code {Off,Waiting,Ready,Unsupported,Conflict,StorageError,Stopped};
struct Settings {bool enabled=false;Mode defaultMode=Mode::Twin;bool fullDeck=false;bool (*developmentEnabled)() noexcept=nullptr;};
bool Prime(std::uintptr_t,const Settings&,bool validateOnly,void(*log)(const char*));
bool Start();
void Stop() noexcept;
void Tick() noexcept;
bool Requested() noexcept;
bool Capture(State&,std::uint64_t& generation) noexcept;
Error EquipCard(std::uint64_t generation,std::uint64_t revision,unsigned actor,unsigned slot,std::int16_t card,bool transfer) noexcept;
Error SelectMode(std::uint64_t generation,std::uint64_t revision,Mode,bool releaseThird) noexcept;
const Effects* ActorEffects(unsigned actor) noexcept;
unsigned NativeAttackChance(unsigned actor,unsigned status) noexcept;
std::uint64_t BattleGeneration() noexcept;
const char* Detail() noexcept;
Code Status() noexcept;
NativeUi::Callbacks Bindings(void(*images)(const NativeUi::Images&) noexcept);
}
