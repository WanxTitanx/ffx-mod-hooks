#pragma once
#include "SphereGridProgress8SaveService.h"
namespace FfxHooks::SphereGridProgress8Runtime {
struct Layout {
    std::uint64_t generation=0;
    SphereGridProgress::Hash layout{},contents{};
};
// The native Grid8 owner validates its buffers and retains its descriptor/code
// until exit. Merely registering this source never authorizes a save write.
struct Source {
    void* context=nullptr;
    bool (*describe)(void*,Layout&) noexcept=nullptr;
    bool (*capture)(void*,const Layout&,SphereGridProgress8::Snapshot&) noexcept=nullptr;
};
struct Status {
    bool subscribed=false,sourceRegistered=false;
    unsigned staged=0,written=0,rejected=0;
    SphereGridProgress8Save::Persist last=SphereGridProgress8Save::Persist::Rejected;
};
bool RegisterSource(const Source*) noexcept;
void PrimeSaveIo(std::uintptr_t base,bool validateOnly,const wchar_t* storageOverride=nullptr);
void PresentTick();
void RequestStop() noexcept;
bool Remove() noexcept;
Status GetStatus() noexcept;
void MenuLabel(char*,std::size_t);
void Detail(char*,std::size_t);
bool MenuAction();
#ifdef FFXHOOKS_TESTING
bool SetGatesForTests(bool (*profile)(std::uintptr_t),bool (*pin)(const void*)) noexcept;
#endif
}
