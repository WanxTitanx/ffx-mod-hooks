#pragma once
#include "../shared/ExecutableProfile.h"
#include <array>
#include <cstdint>

// Jarvis-HOOK: FFX_Kernel_LoadFileToTable coordinates for the optional 32-bit
// kernel table adapter. Both functions were disassembled in the exact legacy
// 78ce3439... and Steam 0537b2a1... PEs and differ only in relocated call, branch
// and string operands; every pair below is an explicit correspondence, not a
// uniform delta. The data globals keep the same RVA in both executables.
// Byte-scanned xrefs: the u16 size globals of cases 0/2/3 are read and written
// only inside this function in both PEs.
namespace FfxHooks::TextLanguage::KernelSites {
template<std::uint32_t Legacy, std::uint32_t Steam>
constexpr std::uint32_t Pick() noexcept { return ExecutableProfile::Steam20261001 ? Steam : Legacy; }

inline constexpr std::uint32_t Load = Pick<0x381E00, 0x381D40>();      // int __cdecl(int kind)
inline constexpr std::uint32_t JumpTable = Pick<0x3820EC, 0x38202C>(); // six relocated case VAs
inline constexpr std::uint32_t Read = Pick<0x382EB0, 0x382DF0>();      // u32 __cdecl(const char* name, void* staging)
inline constexpr std::uint32_t HeapAlloc = Pick<0x2871C0, 0x287020>(); // void* __cdecl(u32)
inline constexpr std::uint32_t HeapFree = Pick<0x2871B0, 0x287010>();  // void __cdecl(void*)
inline constexpr std::uint32_t Align = Pick<0x43C520, 0x43C4A0>();     // SgMem_SetAlignment(0x40)
inline constexpr std::uint32_t Account = Pick<0x399F10, 0x399F00>();   // adds the 64-byte-rounded size to a usage counter
// The finalizer pads the already allocated banks to this historical budget, then
// enters a permanent mem_over loop if their total is larger. It is not the heap
// allocator. The only direct caller is the end of the native kernel initialization.
inline constexpr std::uint32_t BudgetFinish = Pick<0x399F30, 0x399F20>();
inline constexpr std::uint32_t BudgetReturn = Pick<0x381CF8, 0x381C28>();
inline constexpr std::uint32_t BudgetUsed = Pick<0xD35FF8, 0xD36000>();
inline constexpr std::uint32_t BudgetPadding = BudgetUsed + 4;
inline constexpr std::uint32_t BudgetError = Pick<0x754C88, 0x754C90>();
inline constexpr std::uint32_t Fatal = Pick<0x39F230, 0x39F210>();
inline constexpr std::uint32_t OriginalBudget = 0x70000u;
// push ebp; mov ebp,esp; mov eax,[ebp+8]; push esi; push edi; cmp eax,5; ja +0x2D5
inline constexpr std::array<unsigned char, 17> Prologue{
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x56, 0x57, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0xD5, 0x02, 0x00, 0x00};

struct Site {
    int kind;
    std::uint32_t name;      // "command" / "a_ability" / "item" (regional path leaf)
    std::uint32_t size16;    // u16 rounded size written by the native case
    std::uint32_t bank;      // table bank pointer read by the game's accessors
    std::uint32_t entry;     // jump-table target of this case
    std::uint32_t readCall;  // E8 rel32 to Read inside the case
    std::uint32_t sizeStore; // 66 A3 abs32: mov word ptr [size16], ax
};
// Order matches TextLanguage::ExtendedKernelTables.
inline constexpr std::array<Site, 3> Sites{{
    {0, Pick<0x7547A0, 0x7547A8>(), 0xD2A968, 0xD2A92C, Pick<0x381E18, 0x381D58>(), Pick<0x381E2A, 0x381D6A>(), Pick<0x381E38, 0x381D78>()},
    {2, Pick<0x7547B0, 0x7547B8>(), 0xD2A970, 0xD2A944, Pick<0x381F74, 0x381EB4>(), Pick<0x381F86, 0x381EC6>(), Pick<0x381F94, 0x381ED4>()},
    {3, Pick<0x7547BC, 0x7547C4>(), 0xD2A96E, 0xD2A940, Pick<0x381FDF, 0x381F1F>(), Pick<0x381FF1, 0x381F31>(), Pick<0x381FFF, 0x381F3F>()},
}};
} // namespace FfxHooks::TextLanguage::KernelSites
