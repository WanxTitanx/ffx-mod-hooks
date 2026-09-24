#pragma once
#include "SinAiCore.h"
#include "SinSpreadCore.h"
#include "SinNaturalCore.h"

namespace FfxHooks::SinAi {
struct Context {
    bool enabled=false;
    std::uint16_t field=0;
    std::uint32_t seed=0,visit=0;
    SinSpread::Distribution distribution=SinSpread::Distribution::Random;
    std::uint64_t generation=0;
};
using ContextReader=bool(*)(bool commandsReady,Context*);
using RegistrationReporter=void(*)(unsigned actorSlot,std::uint64_t generation);
using NaturalReporter=bool(*)(const SinNatural::Evidence&);
enum class StatusCode {Off,Ready,MissingPack,InvalidPack,Unsupported,Conflict,DependenciesMissing,SourceMismatch,Installed};
struct Status {StatusCode code=StatusCode::Off;unsigned accepted=0,rejected=0;std::uint64_t generation=0;};
bool Start(std::uintptr_t base,const wchar_t* path,ContextReader reader,RegistrationReporter reporter,NaturalReporter naturalReporter);
void RequestStop();
void RefreshLabels(std::uint64_t generation);
void EndEncounter(std::uint64_t generation);
bool Ready();
bool DependenciesReady();
Status CurrentStatus();
const char* Detail();
#ifdef FFXHOOKS_TESTING
using RegisterFn=int(__cdecl*)(int,const char*,const void*);
bool PrepareForTests(std::uintptr_t base,const std::uint8_t*,std::size_t,ContextReader,RegistrationReporter,RegisterFn);
int RegisterForTests(int,const char*,const void*,std::uint32_t callerRva);
const void* RewardViewForTests(const void* source);
#endif
}
