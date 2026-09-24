#include "FieldScoutAdmissionCore.h"

namespace FfxHooks::FieldScoutAdmission {
namespace {

constexpr uint32_t kShutdownBit = 0x1u;
constexpr uint32_t kQuiescedBit = 0x2u;
constexpr uint32_t kStickyClosedBit = 0x4u;
constexpr uint32_t kClosedWord = kShutdownBit | kQuiescedBit;

bool ValidFamily(ShimFamily family) {
    return static_cast<size_t>(family) < kShimFamilyCount;
}

} // namespace

void InitializeClosed(State* state) {
    if (!state) return;
    state->threadStarts.store(0u, std::memory_order_release);
    state->word.store(kClosedWord, std::memory_order_release);
}

bool Open(State* state) {
    if (!state) return false;
    // Installation opens admission only once, after the complete owned MinHook batch succeeds.
    // Sticky close adds a distinct bit, so a concurrent or repeated open cannot clear teardown.
    uint32_t expected = kClosedWord;
    return state->word.compare_exchange_strong(
        expected, 0u, std::memory_order_acq_rel,
        std::memory_order_acquire);
}

void RequestClose(State* state) {
    if (!state) return;
    state->word.fetch_or(
        kClosedWord | kStickyClosedBit, std::memory_order_acq_rel);
}

bool CloseAndDrainThreadStarts(State* state, const WaitIo& wait) {
    if (!state) return false;
    RequestClose(state);
    uint32_t waited = 0u;
    while (ActiveThreadStarts(*state) != 0u) {
        if (!wait.pause || waited >= wait.timeoutMs) return false;
        wait.pause(wait.context, 1u);
        ++waited;
    }
    return true;
}

bool IsShuttingDown(const State& state) {
    return (state.word.load(std::memory_order_acquire) & kShutdownBit) != 0u;
}

bool IsQuiesced(const State& state) {
    return (state.word.load(std::memory_order_acquire) & kQuiescedBit) != 0u;
}

bool TryEnterAfterPrologue(State* state, ShimFamily family) {
    return state && ValidFamily(family) && !IsShuttingDown(*state);
}

bool ShouldSkipCapture(const State& state, bool battleActive) {
    const uint32_t word = state.word.load(std::memory_order_acquire);
    return (word & kClosedWord) != 0u || battleActive;
}

bool ApplyPathTransition(
    State* state, PathTransition transition, bool battleActive) {
    if (!state) return false;
    uint32_t observed = state->word.load(std::memory_order_acquire);
    for (;;) {
        if ((observed & kShutdownBit) != 0u) return false;
        uint32_t desired = observed;
        if (transition == PathTransition::QuiesceBattle) {
            desired |= kQuiescedBit;
        } else {
            if (battleActive) return false;
            desired &= ~kQuiescedBit;
        }
        // Close and path transitions contend on the same word. If close wins, this CAS reloads
        // the shutdown bit and refuses to reopen; if transition wins, close subsequently closes.
        if (state->word.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

bool TryAcquireThreadStart(State* state) {
    if (!state || ShouldSkipCapture(*state, false)) return false;
    uint32_t expected = 0u;
    if (!state->threadStarts.compare_exchange_strong(
            expected, 1u, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return false;
    }
    if (ShouldSkipCapture(*state, false)) {
        ReleaseThreadStart(state);
        return false;
    }
    return true;
}

void ReleaseThreadStart(State* state) {
    if (!state) return;
    uint32_t observed = state->threadStarts.load(std::memory_order_acquire);
    while (observed != 0u &&
           !state->threadStarts.compare_exchange_weak(
               observed, observed - 1u, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
}

uint32_t ActiveThreadStarts(const State& state) {
    return state.threadStarts.load(std::memory_order_acquire);
}

} // namespace FfxHooks::FieldScoutAdmission
