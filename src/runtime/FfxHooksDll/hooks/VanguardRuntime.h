#pragma once
#include "VanguardRules.h"
#include <cstdint>
namespace FfxHooks::Vanguard {
using LogFn=void(*)(const char*);
enum class MappingCode : unsigned { Valid, NoKernel, InvalidId, Duplicate, NativePayload, IdentityMismatch };
struct MappingState { Mapping ids{}; std::array<MappingCode,AbilityCount> codes{}; std::uint64_t stamp=0; };
enum class BindingCode : unsigned {Disabled,Valid,InvalidConfiguration,NoKernel,UnverifiedAbility,DuplicateCommand,NotExecutable};
struct CommandBinding {unsigned packed=0,command=0,cost=256;BindingCode code=BindingCode::Disabled;};
struct BindingState {std::array<CommandBinding,AbilityCount> entries{};std::uint64_t stamp=0;};
bool ReadBindings(BindingState& out) noexcept;
bool SaveBinding(unsigned effect,unsigned packed,std::uint64_t expectedStamp) noexcept;
void CaptureStartup();
void RefreshUiStatus() noexcept;
bool Start(std::uintptr_t base,bool validateOnly,LogFn log);
void RequestStop() noexcept;
bool Active() noexcept;
bool ReadMapping(MappingState& out) noexcept;
bool SaveMapping(unsigned effect,unsigned id) noexcept;
const char* MappingDetail(MappingCode code) noexcept;
}
