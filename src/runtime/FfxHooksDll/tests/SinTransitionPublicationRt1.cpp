#include "../hooks/SinTransitionPublication.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>

namespace {

using FfxHooks::SinRam::CancelCode;
using FfxHooks::SinRam::ConsumeCode;
using FfxHooks::SinRam::ResetCode;
using FfxHooks::SinRam::SinTransitionPublication;
using FfxHooks::SinRam::StageCode;
using FfxHooks::SinRam::TransitionTicket;

int g_passed = 0;
int g_failed = 0;

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

TransitionTicket TicketFor(std::uint32_t request) {
    TransitionTicket ticket{};
    ticket.request = request;
    ticket.encounterToken = request ^ 0xA5C39E71u;
    ticket.callerRva = 0x00471CEFu;
    ticket.transitionGeneration =
        0xD1A6000000000000ull | static_cast<std::uint64_t>(request);
    return ticket;
}

bool SameTicket(const TransitionTicket& left, const TransitionTicket& right) {
    return left.request == right.request &&
           left.encounterToken == right.encounterToken &&
           left.callerRva == right.callerRva &&
           left.transitionGeneration == right.transitionGeneration;
}

void WaitForStart(std::atomic<int>& ready, std::atomic<bool>& start) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void TestExactWidthsAndLockFreeControl() {
    static_assert(std::is_same_v<decltype(TransitionTicket::request), std::uint32_t>);
    static_assert(std::is_same_v<decltype(TransitionTicket::encounterToken), std::uint32_t>);
    static_assert(std::is_same_v<decltype(TransitionTicket::callerRva), std::uint32_t>);
    static_assert(
        std::is_same_v<decltype(TransitionTicket::transitionGeneration), std::uint64_t>);

    SinTransitionPublication publication{};
    Expect(publication.IsLockFree(),
           "the one-shot publication control must be lock-free on the supported x86 build");
}

void TestStageValidatesTheClosedValueTicket() {
    SinTransitionPublication publication{};
    TransitionTicket invalid = TicketFor(11u);
    invalid.request = 0u;
    Expect(publication.Stage(invalid) == StageCode::InvalidTicket,
           "request zero must not acquire the publication");

    invalid = TicketFor(12u);
    invalid.callerRva = 0u;
    Expect(publication.Stage(invalid) == StageCode::InvalidTicket,
           "an absent transition caller must not acquire the publication");

    invalid = TicketFor(13u);
    invalid.transitionGeneration = 0u;
    Expect(publication.Stage(invalid) == StageCode::InvalidTicket,
           "generation zero must not acquire the publication");

    const TransitionTicket valid = TicketFor(14u);
    Expect(publication.Stage(valid) == StageCode::Staged,
           "one complete value-only ticket must stage");
    Expect(publication.Stage(TicketFor(15u)) != StageCode::Staged,
           "a second ticket must fail while the one-shot publication is occupied");

    const auto consumed = publication.Consume(valid.request);
    Expect(consumed.code == ConsumeCode::Matched && SameTicket(consumed.ticket, valid),
           "matched consume must preserve every full-width transition value");
    Expect(publication.Consume(valid.request).code == ConsumeCode::Empty,
           "a consumed ticket must never replay");
}

void TestMismatchConsumesAndClears() {
    SinTransitionPublication publication{};
    const TransitionTicket ticket = TicketFor(0x10203040u);
    Expect(publication.Stage(ticket) == StageCode::Staged,
           "mismatch fixture must stage");

    const auto mismatched = publication.Consume(0x10203041u);
    Expect(mismatched.code == ConsumeCode::RequestMismatch &&
               SameTicket(mismatched.ticket, ticket),
           "request mismatch must be explicit and retain the consumed evidence in the result");
    Expect(publication.Consume(ticket.request).code == ConsumeCode::Empty,
           "request mismatch must clear the payload instead of retaining stale evidence");
}

void TestCancelClearsOnlyTheExactRequest() {
    SinTransitionPublication publication{};
    const TransitionTicket ticket = TicketFor(91u);
    Expect(publication.Stage(ticket) == StageCode::Staged,
           "cancel fixture must stage");
    Expect(publication.Cancel(92u) == CancelCode::RequestMismatch,
           "a different request must not cancel the staged transition");

    const auto stillPresent = publication.Consume(ticket.request);
    Expect(stillPresent.code == ConsumeCode::Matched &&
               SameTicket(stillPresent.ticket, ticket),
           "mismatched cancellation must preserve the exact staged ticket");

    Expect(publication.Stage(ticket) == StageCode::Staged,
           "the exact-cancel fixture must restage after consumption");
    Expect(publication.Cancel(ticket.request) == CancelCode::Cancelled,
           "the exact request must cancel its own ticket");
    Expect(publication.Consume(ticket.request).code == ConsumeCode::Empty,
           "exact cancellation must leave no replayable payload");
    Expect(publication.Cancel(0u) == CancelCode::InvalidRequest,
           "request zero must never alias an empty publication");
}

void TestResetAndFullWidthGenerationBoundary() {
    SinTransitionPublication publication{SinTransitionPublication::kMaxPublicationEpoch};
    TransitionTicket ticket = TicketFor(101u);
    ticket.encounterToken = 0xFEDCBA98u;
    ticket.callerRva = 0xFFFFFFFFu;
    ticket.transitionGeneration = std::numeric_limits<std::uint64_t>::max();

    Expect(publication.Stage(ticket) == StageCode::Staged,
           "the maximum nonzero transition generation must remain representable");
    const auto consumed = publication.Consume(ticket.request);
    Expect(consumed.code == ConsumeCode::Matched && SameTicket(consumed.ticket, ticket),
           "epoch rollover must not truncate the ticket generation, token, or caller RVA");
    Expect(publication.PublicationEpoch() == 1u,
           "the internal publication epoch must wrap deterministically from max to one");

    const TransitionTicket next = TicketFor(102u);
    Expect(publication.Stage(next) == StageCode::Staged,
           "the publication must remain usable after deterministic epoch rollover");
    Expect(publication.Reset() == ResetCode::Cleared,
           "reset must clear one ready ticket");
    Expect(publication.Reset() == ResetCode::Empty,
           "reset on an empty publication must be an explicit no-op");
    Expect(publication.Consume(next.request).code == ConsumeCode::Empty,
           "reset must not retain the cleared ticket");
}

void TestConcurrentStageHasExactlyOneCompleteWinner() {
    constexpr int kRounds = 1000;
    bool allRoundsValid = true;

    for (int round = 0; round < kRounds; ++round) {
        SinTransitionPublication publication{};
        const TransitionTicket first = TicketFor(static_cast<std::uint32_t>(round * 2 + 1));
        const TransitionTicket second = TicketFor(static_cast<std::uint32_t>(round * 2 + 2));
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        StageCode firstCode = StageCode::InvalidTicket;
        StageCode secondCode = StageCode::InvalidTicket;

        std::thread firstThread([&]() {
            WaitForStart(ready, start);
            firstCode = publication.Stage(first);
        });
        std::thread secondThread([&]() {
            WaitForStart(ready, start);
            secondCode = publication.Stage(second);
        });
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        firstThread.join();
        secondThread.join();

        const bool firstWon = firstCode == StageCode::Staged;
        const bool secondWon = secondCode == StageCode::Staged;
        if (firstWon == secondWon) {
            allRoundsValid = false;
            continue;
        }
        const TransitionTicket& winner = firstWon ? first : second;
        const auto consumed = publication.Consume(winner.request);
        if (consumed.code != ConsumeCode::Matched ||
            !SameTicket(consumed.ticket, winner) ||
            publication.Consume(winner.request).code != ConsumeCode::Empty) {
            allRoundsValid = false;
        }
    }

    Expect(allRoundsValid,
           "concurrent staging must publish exactly one complete ticket with no torn loser bytes");
}

void TestConcurrentConsumeAndCancelCannotReplay() {
    constexpr int kRounds = 4000;
    bool allRoundsValid = true;

    for (int round = 0; round < kRounds; ++round) {
        SinTransitionPublication publication{};
        const TransitionTicket ticket = TicketFor(static_cast<std::uint32_t>(round + 1));
        if (publication.Stage(ticket) != StageCode::Staged) {
            allRoundsValid = false;
            continue;
        }

        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        ConsumeCode consumeCode = ConsumeCode::Empty;
        TransitionTicket consumedTicket{};
        CancelCode cancelCode = CancelCode::Empty;

        std::thread consumer([&]() {
            WaitForStart(ready, start);
            const auto result = publication.Consume(ticket.request);
            consumeCode = result.code;
            consumedTicket = result.ticket;
        });
        std::thread canceller([&]() {
            WaitForStart(ready, start);
            cancelCode = publication.Cancel(ticket.request);
        });
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        consumer.join();
        canceller.join();

        const bool consumed = consumeCode == ConsumeCode::Matched;
        const bool cancelled = cancelCode == CancelCode::Cancelled;
        if (consumed == cancelled || (consumed && !SameTicket(consumedTicket, ticket)) ||
            publication.Consume(ticket.request).code != ConsumeCode::Empty) {
            allRoundsValid = false;
        }
    }

    Expect(allRoundsValid,
           "concurrent consume/cancel must have one clearing winner and no stale replay");
}

void TestConcurrentPipelineNeverReturnsTornTickets() {
    constexpr std::uint32_t kIterations = 20000u;
    SinTransitionPublication publication{};
    std::atomic<std::uint32_t> staged{0u};
    std::atomic<std::uint32_t> consumed{0u};
    std::atomic<int> faults{0};

    std::thread producer([&]() {
        for (std::uint32_t request = 1u; request <= kIterations; ++request) {
            const TransitionTicket ticket = TicketFor(request);
            while (publication.Stage(ticket) != StageCode::Staged) {
                std::this_thread::yield();
            }
            staged.store(request, std::memory_order_release);
            while (consumed.load(std::memory_order_acquire) != request) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (std::uint32_t request = 1u; request <= kIterations; ++request) {
            while (staged.load(std::memory_order_acquire) != request) {
                std::this_thread::yield();
            }
            for (;;) {
                const auto result = publication.Consume(request);
                if (result.code == ConsumeCode::Empty ||
                    result.code == ConsumeCode::Busy ||
                    result.code == ConsumeCode::Raced) {
                    std::this_thread::yield();
                    continue;
                }
                if (result.code != ConsumeCode::Matched ||
                    !SameTicket(result.ticket, TicketFor(request))) {
                    faults.fetch_add(1, std::memory_order_relaxed);
                }
                break;
            }
            if (publication.Consume(request).code != ConsumeCode::Empty) {
                faults.fetch_add(1, std::memory_order_relaxed);
            }
            consumed.store(request, std::memory_order_release);
        }
    });

    producer.join();
    consumer.join();
    Expect(faults.load(std::memory_order_relaxed) == 0,
           "the concurrent pipeline must never return torn values or replay a prior ticket");
}

}  // namespace

int main() {
    TestExactWidthsAndLockFreeControl();
    TestStageValidatesTheClosedValueTicket();
    TestMismatchConsumesAndClears();
    TestCancelClearsOnlyTheExactRequest();
    TestResetAndFullWidthGenerationBoundary();
    TestConcurrentStageHasExactlyOneCompleteWinner();
    TestConcurrentConsumeAndCancelCannotReplay();
    TestConcurrentPipelineNeverReturnsTornTickets();

    std::printf(
        "S.I.N. transition publication RT0/RT1: %d/%d passed\n",
        g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
