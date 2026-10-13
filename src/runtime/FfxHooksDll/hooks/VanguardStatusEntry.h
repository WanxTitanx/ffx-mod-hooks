#pragma once
// Jarvis-HOOK: read-only ownership witness for the retained native status entry.
#include "NativeUiHookSupport.h"
#include <array>
#include <atomic>
namespace FfxHooks::VanguardStatusEntry {
inline constexpr auto Rva=ExecutableProfile::Rva<0x38AEC0>();
inline constexpr std::array<unsigned char,32> Native={
    0x55,0x8B,0xEC,0x83,0xEC,0x30,0x8B,0x45,0x0C,0x53,0x56,0x57,0x8D,0xB8,0xDE,0x05,
    0x00,0x00,0x05,0xF7,0x05,0x00,0x00,0x89,0x45,0xD4,0x8B,0x45,0x18,0xBA,0x01,0x00};
inline std::atomic<bool> published{false};
inline std::uintptr_t image=0;
inline std::array<unsigned char,32> owned{};
inline void Publish(std::uintptr_t base,const std::array<unsigned char,32>& original) noexcept {
    // A short matching prologue alone cannot authorize another consumer's
    // longer witness. Verify the full native entry before retaining our proof.
    if(original!=Native||published.load())return;
    if(!NativeUiSupport::Copy(owned.data(),reinterpret_cast<const void*>(base+Rva),owned.size()))return;
    image=base;published.store(true,std::memory_order_release);
}
inline bool Matches(std::uintptr_t base,std::uint32_t rva,const void* bytes,std::size_t size) noexcept {
    return published.load(std::memory_order_acquire)&&base==image&&rva==Rva&&bytes&&size==owned.size()&&
        !std::memcmp(bytes,owned.data(),owned.size());
}
}
