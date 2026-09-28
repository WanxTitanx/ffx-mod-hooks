#pragma once
#include <cstdint>
#include <string>
namespace FfxHooks::TextLanguage::Native {
enum class State { Off, Armed, Active, InvalidPack, Unsupported, TooLate, Conflict, Stopped, RestartRequired, ValidateOnly };
struct Settings {
 bool enabled=false,validateOnly=false;
 std::string locale="pt-BR";
 std::wstring packageDirectory,executablePath;
};
struct Snapshot {
 State state=State::Off;
 bool installed=false,fontReady=false;
 std::uint32_t textOpens=0,fontOpens=0,nativeFallbacks=0;
};
using LogFn=void(*)(const char*);
bool Start(std::uintptr_t,const Settings&,LogFn=nullptr);
bool StartConfigured(std::uintptr_t,bool validateOnly,LogFn=nullptr);
void RequestStop() noexcept;
// Cached translated text needs the paired font until restart. No hot unload.
bool Stop() noexcept;
Snapshot Inspect() noexcept;
const char* Detail() noexcept;
}
