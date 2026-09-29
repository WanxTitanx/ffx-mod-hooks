#include "../hooks/NativeSaveEvents.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

namespace E = FfxHooks::NativeSaveEvents;
namespace {
int failed = 0, checks = 0;
void Check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failed; std::fprintf(stderr, "FAIL %s\n", name); }
}
std::atomic<unsigned> primaryReads{0}, secondaryReads{0}, secondaryWrites{0}, resets{0};
void PrimaryRead(const wchar_t*, const unsigned char*, const unsigned char*, std::size_t) noexcept {
    ++primaryReads;
}
void AdditionalRead(const wchar_t*, const unsigned char*, const unsigned char*, std::size_t) noexcept {
    ++secondaryReads;
}
void AdditionalWrite(const wchar_t*, const unsigned char*, std::size_t) noexcept { ++secondaryWrites; }
void Reset() noexcept { ++resets; }
const E::Observer primary{PrimaryRead, AdditionalWrite, Reset};
const E::Observer additional{AdditionalRead, AdditionalWrite, Reset};
}

int main() {
    Check(!E::Requested(), "initially not requested");
    Check(!E::SubscribeAdditional(nullptr), "null additional observer rejected");
    const E::Observer incomplete{nullptr, AdditionalWrite, nullptr};
    Check(!E::SubscribeAdditional(&incomplete), "incomplete additional observer rejected");
    Check(E::Subscribe(&primary), "existing primary owner remains supported");
    Check(E::Subscribe(&additional), "main shared registry admits an independent owner");
    Check(E::Subscribed(&primary) && E::Subscribed(&additional), "both owners retain independent registration");
    Check(E::SubscribeAdditional(&additional), "additional subscriber coexists with primary");
    Check(E::SubscribeAdditional(&additional), "additional registration is idempotent");
    E::ReadCompleted(L"slot", nullptr, nullptr, 0);
    Check(primaryReads == 1 && secondaryReads == 1, "both receive exactly one read");
    E::WriteCompleted(L"slot", nullptr, 0);
    Check(secondaryWrites == 2, "both receive exactly one write");
    E::ResetCompleted();
    Check(resets == 2, "both receive exactly one reset");
    E::UnsubscribeAdditional(&additional);
    Check(E::Requested(), "removing additional observer preserves primary");
    E::ReadCompleted(L"slot", nullptr, nullptr, 0);
    Check(primaryReads == 2 && secondaryReads == 1, "removed observer receives no later dispatch");
    E::Unsubscribe(&primary);
    Check(!E::Requested(), "all subscriptions removed");

    // These fixtures remain alive through all threads and dispatches, just like
    // production observer storage, which is retained for the module lifetime.
    std::array<E::Observer, E::kAdditionalObserverCapacity + 1> listeners{};
    for (auto& o : listeners) o = {AdditionalRead, AdditionalWrite, nullptr};
    for (std::size_t i=0; i<E::kAdditionalObserverCapacity; ++i)
        Check(E::SubscribeAdditional(&listeners[i]), "bounded slot is admitted");
    Check(!E::SubscribeAdditional(&listeners.back()), "capacity exhaustion is reported");
    Check(E::Requested(), "additional-only subscriptions request native producer");
    const unsigned before = secondaryReads;
    E::ReadCompleted(L"slot", nullptr, nullptr, 0);
    Check(secondaryReads == before + E::kAdditionalObserverCapacity, "bounded fanout");
    for (auto& o : listeners) E::UnsubscribeAdditional(&o);

    // Concurrent idempotent admission may be rejected while the short mutation
    // gate is busy; it must never publish the same observer in multiple slots.
    std::array<std::thread, 8> threads;
    for (auto& t : threads) t=std::thread([] {
        for (unsigned n=0; n<2000; ++n) E::SubscribeAdditional(&additional);
    });
    for (auto& t : threads) t.join();
    const unsigned beforeConcurrent = secondaryReads;
    E::ReadCompleted(L"slot", nullptr, nullptr, 0);
    Check(secondaryReads == beforeConcurrent + 1, "concurrent registration never duplicates delivery");
    E::UnsubscribeAdditional(&additional);
    Check(!E::Requested(), "clean additional teardown");
    std::printf("RecoverySaveEventsRt0: %d/%d passed\n", checks-failed, checks);
    return failed ? 1 : 0;
}
