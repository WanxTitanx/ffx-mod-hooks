#pragma once
#include "EquipmentWorkshopStore.h"
#include <cstdint>

namespace FfxHooks::EquipmentWorkshop {
using LogFn=void(*)(const char*);
enum class RuntimeCode { Disabled, WaitingForSave, Ready, Unsupported, Conflict, StorageError, Stopped };
struct RuntimeStatus {RuntimeCode code=RuntimeCode::Disabled;bool enabled=false;unsigned ownerThread=0;std::uint64_t revision=0;};
// Worker-thread installation before Fastload can load the initial save.
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,LogFn logger);
void PrimeSaveIo(bool enabled,bool validateOnly);
void RequestStop() noexcept;
RuntimeStatus Status();
const char* Detail();
// Menu calls run on the native pump owner; operations never write a game file.
bool Capture(workshop::State& state);
workshop::Error Preview(const workshop::Request&,workshop::Plan&);
bool Commit(const workshop::Request& expected,const workshop::Plan&);
bool Requested();
#ifdef FFXHOOKS_TESTING
bool StartForTests(std::uintptr_t base,bool enabled,const wchar_t* directory,LogFn logger);
bool LoadForTests(const wchar_t* path,const SaveImage& disk,const SaveImage& loaded);
bool CommitLoadForTests(const SaveImage& loaded);
bool WriteForTests(const wchar_t* path,const SaveImage& image);
#endif
}
