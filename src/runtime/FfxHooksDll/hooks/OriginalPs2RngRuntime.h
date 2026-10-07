#pragma once
#include "OriginalPs2RngInputs.h"

namespace FfxHooks::OriginalPs2Rng::Runtime {
enum class Code : unsigned { Disabled, Unavailable, AwaitingReset, AwaitingBoundary, Active, StopPending, Stopped };
enum class Reason : unsigned {
    None, UnsupportedProfile, SignatureMismatch, ValidationOnly, HookConflict,
    CounterOriginMissing, ClockUnavailable, WrongThread, ForeignCounter,
    ReentrantBoundary, BoundaryChanged, NativeInitializationFailed, ClockCallMismatch
};
struct Snapshot {
    Code code=Code::Disabled;
    Reason reason=Reason::None;
    bool bootRequested=false;
    unsigned ownerThread=0;
    std::uint64_t resets=0, initializations=0;
};
using LogFunction=void(*)(const char*);
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,LogFunction log=nullptr) noexcept;
Snapshot Status() noexcept;
const char* Detail() noexcept;
void Service() noexcept;
void RequestStop() noexcept;
bool Stop() noexcept;

#ifdef FFXHOOKS_TESTING
struct InputSource {
    void* context=nullptr;
    bool (*performance)(void*,std::uint64_t&,std::uint64_t&) noexcept=nullptr;
    bool (*calendar)(void*,UtcCalendar&) noexcept=nullptr;
    unsigned (*thread)(void*) noexcept=nullptr;
    void (*committed)(void*) noexcept=nullptr;
};
bool SetInputSourceForTests(const InputSource&) noexcept;
#endif
} // namespace FfxHooks::OriginalPs2Rng::Runtime
