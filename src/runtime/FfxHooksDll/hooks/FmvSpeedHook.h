#pragma once
#include "FmvSpeedCore.h"
#include <cstddef>
#include <cstdint>
namespace FfxHooks::FmvSpeed {
enum class Code {Off,Ready,Applied,Limited,AudioUnavailable,Conflict,RestorePending,Unsupported};
struct Status {Code code=Code::Off;unsigned requested=1,applied=1;bool playing=false;};
bool Start(std::uintptr_t imageBase);
bool Playing();
bool Ready();
unsigned PublicationEpoch();
void SetDesired(unsigned factor,bool admitted,unsigned expectedEpoch=0xFFFFFFFFu);
void Neutralize();
void RequestStop();
Status CurrentStatus();
const char* Detail();
#ifdef FFXHOOKS_TESTING
using FrameFn=bool(__thiscall*)(void*,void*,void*,void*);
struct AudioIo {
    int(__stdcall* getFrequency)(void*,float*)=nullptr;
    int(__stdcall* setFrequency)(void*,float)=nullptr;
    int(__stdcall* getSound)(void*,void**)=nullptr;
};
bool PrepareForTests(std::uintptr_t,FrameFn,AudioIo);
bool FetchForTests(void*,void*,void*,void*,std::uint32_t callerRva);
#endif
}
