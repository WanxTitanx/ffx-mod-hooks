#pragma once
#include "ArcanaNativeUi.h"
#include "ArcanaNativeEffects.h"
#include "ArcanaPresentation.h"
#include "ArcanaWorldCore.h"
#include "ArcanaVictory.h"

namespace FfxHooks::Arcana::Runtime {
enum class Code {Off,Waiting,Ready,Unsupported,Conflict,StorageError,Stopped};
struct Settings {bool enabled=false;Mode defaultMode=Mode::Twin;bool fullDeck=false;bool (*developmentEnabled)() noexcept=nullptr;};
bool Prime(std::uintptr_t,const Settings&,bool validateOnly,void(*log)(const char*));
bool Start();
void Stop() noexcept;
void Tick() noexcept;
bool Requested() noexcept;
bool Capture(State&,std::uint64_t& generation) noexcept;
bool CapturePresentation(Presentation::State&,std::uint64_t& generation) noexcept;
Error AcknowledgePresentation(std::uint64_t generation,const Presentation::View&,Presentation::Navigation) noexcept;
bool CaptureWorld(State&,Acquisition::Progress&,Acquisition::Facts&,std::uint64_t& generation,std::uint16_t room) noexcept;
Acquisition::Result InteractWorld(std::uint64_t generation,const Acquisition::Target&,std::uint16_t room,float x,float z) noexcept;
Error EquipCard(std::uint64_t generation,std::uint64_t revision,unsigned actor,unsigned slot,std::int16_t card,bool transfer) noexcept;
Error SelectMode(std::uint64_t generation,std::uint64_t revision,Mode,bool releaseThird) noexcept;
const Effects* ActorEffects(unsigned actor) noexcept;
unsigned NativeAttackChance(unsigned actor,unsigned status) noexcept;
std::uint64_t BattleGeneration() noexcept;
std::uint64_t SaveSession() noexcept;
Error GrantVictory(const VictoryReceipt&) noexcept;
const char* Detail() noexcept;
Code Status() noexcept;
NativeUi::Callbacks Bindings(void(*images)(const NativeUi::Images&) noexcept);
}
