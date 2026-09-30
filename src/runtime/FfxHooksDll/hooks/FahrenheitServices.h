#pragma once
// Jarvis-HOOK: optional cooperative transport. V1 frame capability remains 3.
#include "FahrenheitCoexistenceCore.h"
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
namespace FfxHooks::Coexistence {
using ResourceConflictFn=int(__cdecl*)(const char*);
inline std::atomic<ResourceConflictFn> resourceConflict{nullptr};
}
extern "C" {
// Called only by the pinned provider after its initial hook registry commits.
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitActivateServicesV2(std::uint32_t abi,std::uint32_t capabilities,FfxHooks::Coexistence::ResourceConflictFn);
__declspec(dllexport) std::uint64_t __cdecl FfxHooks_FahrenheitBeginWriteV2(const wchar_t*,std::uint32_t,const unsigned char*,std::uint32_t,unsigned char*);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitOpenWriteV2(std::uint64_t,std::uintptr_t);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitEndWriteV2(std::uint64_t,int);
__declspec(dllexport) std::uint64_t __cdecl FfxHooks_FahrenheitBeginReadV2(const wchar_t*,std::uint32_t,unsigned char*,std::uint32_t);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitTransformReadV2(std::uint64_t,std::uintptr_t,const unsigned char*,std::uint32_t);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitEndReadV2(std::uint64_t,int);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitAbortIoV2(std::uint64_t);
__declspec(dllexport) int __cdecl FfxHooks_FahrenheitCancelReadV2(unsigned char*,std::uint32_t);
// 0: not handled; UINTPTR_MAX: fatal failure after publishing paired fonts.
__declspec(dllexport) std::uintptr_t __cdecl FfxHooks_FahrenheitOpenResourceV2(const char*);
}
#endif
