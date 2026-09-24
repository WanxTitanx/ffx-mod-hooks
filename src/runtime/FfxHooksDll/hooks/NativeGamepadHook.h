#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <cstdint>
#include "NativeGamepadCore.h"

namespace FfxHooks::NativeGamepad {
struct Snapshot {bool available=false,connected=false;unsigned controller=0;std::uint32_t buttons=0;};
bool Start(std::uintptr_t imageBase);
void RequestStop();
void Stop();
Snapshot Poll();
ButtonMap Mapping();
bool SaveMapping(const ButtonMap&);
bool MappingApplied();
bool MappingConflict();
void RefreshMapping();
#ifdef FFXHOOKS_TESTING
using StateReader=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
void SetStateReaderForTests(StateReader);
#endif
}
