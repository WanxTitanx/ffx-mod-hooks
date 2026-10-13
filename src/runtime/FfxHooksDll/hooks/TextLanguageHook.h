#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
namespace FfxHooks::TextLanguage::Native {
enum class State { Off, Armed, Active, InvalidPack, Unsupported, TooLate, Conflict, Stopped, RestartRequired, ValidateOnly };
struct Settings {
 bool enabled=false,validateOnly=false;
 // Optional 32-bit size adapter for the command/a_ability/item kernel tables.
 bool extendedKernelTables=false;
 std::string locale="pt-BR";
 std::wstring packageDirectory,executablePath;
};
struct Snapshot {
 State state=State::Off;
 bool installed=false,fontReady=false;
 std::uint32_t textOpens=0,fontOpens=0,nativeFallbacks=0;
 // Resources whose native/mod file stays in charge, and reads served that way.
 std::uint32_t retainedResources=0,retainedOpens=0;
 // 32-bit kernel table adapter: armed, loads served, loads above the 16-bit
 // limit, and banks replaced because a reload outgrew them.
 bool kernelTables=false;
 std::uint32_t kernelLoads=0,kernelExtendedLoads=0,kernelRelocations=0;
};
using LogFn=void(*)(const char*);
bool Start(std::uintptr_t,const Settings&,LogFn=nullptr);
bool StartConfigured(std::uintptr_t,bool validateOnly,LogFn=nullptr);
void RequestStop() noexcept;
// Cached translated text needs the paired font until restart. No hot unload.
bool Stop() noexcept;
Snapshot Inspect() noexcept;
// Uses only the already published paired font; no resource loading or fallback
// atlas is introduced while a native field message is alive.
bool EncodeFieldLiteral(const char*,unsigned char*,std::size_t,std::size_t&) noexcept;
bool DecodeNameLiteral(const unsigned char*,std::size_t,char*,std::size_t) noexcept;
std::uint32_t KernelBankBytes(unsigned kind,std::uintptr_t bank) noexcept;
const char* Detail() noexcept;
}
