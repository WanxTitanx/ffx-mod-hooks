#include "SinTransitionPublication.h"

namespace FfxHooks::SinRam {

namespace {

constexpr std::uint64_t kStateMask = 0x3ull;
constexpr std::uint32_t kRequestShift = 2u;
constexpr std::uint32_t kEpochShift = 34u;

}  // namespace

SinTransitionPublication::SinTransitionPublication(
    std::uint32_t initialPublicationEpoch) noexcept
    : control_(EncodeControl(
          ControlState::Empty, 0u, NormalizeEpoch(initialPublicationEpoch))) {}

std::uint32_t SinTransitionPublication::NormalizeEpoch(
    std::uint32_t epoch) noexcept {
    const std::uint32_t normalized = epoch & kMaxPublicationEpoch;
    return normalized == 0u ? 1u : normalized;
}

std::uint32_t SinTransitionPublication::NextEpoch(std::uint32_t epoch) noexcept {
    const std::uint32_t normalized = NormalizeEpoch(epoch);
    return normalized == kMaxPublicationEpoch ? 1u : normalized + 1u;
}

std::uint64_t SinTransitionPublication::EncodeControl(
    ControlState state, std::uint32_t request, std::uint32_t epoch) noexcept {
    return (static_cast<std::uint64_t>(NormalizeEpoch(epoch)) << kEpochShift) |
           (static_cast<std::uint64_t>(request) << kRequestShift) |
           static_cast<std::uint64_t>(state);
}

SinTransitionPublication::ControlState SinTransitionPublication::DecodeState(
    std::uint64_t control) noexcept {
    return static_cast<ControlState>(control & kStateMask);
}

std::uint32_t SinTransitionPublication::DecodeRequest(
    std::uint64_t control) noexcept {
    return static_cast<std::uint32_t>(control >> kRequestShift);
}

std::uint32_t SinTransitionPublication::DecodeEpoch(
    std::uint64_t control) noexcept {
    return static_cast<std::uint32_t>(control >> kEpochShift) &
           kMaxPublicationEpoch;
}

bool SinTransitionPublication::Complete(const TransitionTicket& ticket) noexcept {
    // Encounter token zero remains representable because the full captured DWORD,
    // rather than a truthiness convention, is part of the Resolver evidence.
    return ticket.request != 0u && ticket.callerRva != 0u &&
           ticket.transitionGeneration != 0u;
}

StageCode SinTransitionPublication::Stage(const TransitionTicket& ticket) noexcept {
    if (!Complete(ticket)) return StageCode::InvalidTicket;

    std::uint64_t expected = control_.load(std::memory_order_acquire);
    if (DecodeState(expected) != ControlState::Empty) return StageCode::Occupied;

    const std::uint32_t epoch = DecodeEpoch(expected);
    const std::uint64_t writing =
        EncodeControl(ControlState::Writing, ticket.request, epoch);
    // WHY: one strong CAS is the only admission point. A losing producer returns
    // immediately, so a hook callback can never wait behind another transition.
    if (!control_.compare_exchange_strong(
            expected, writing, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return StageCode::Raced;
    }

    ticket_ = ticket;
    control_.store(
        EncodeControl(ControlState::Ready, ticket.request, epoch),
        std::memory_order_release);
    return StageCode::Staged;
}

ConsumeResult SinTransitionPublication::Consume(
    std::uint32_t expectedRequest) noexcept {
    ConsumeResult result{};
    std::uint64_t observed = control_.load(std::memory_order_acquire);
    const ControlState state = DecodeState(observed);
    if (state == ControlState::Empty) return result;
    if (state != ControlState::Ready) {
        result.code = ConsumeCode::Busy;
        return result;
    }

    const std::uint32_t epoch = DecodeEpoch(observed);
    const std::uint32_t encodedRequest = DecodeRequest(observed);
    const std::uint64_t claimed =
        EncodeControl(ControlState::Claimed, encodedRequest, epoch);
    if (!control_.compare_exchange_strong(
            observed, claimed, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        result.code = ConsumeCode::Raced;
        return result;
    }

    result.ticket = ticket_;
    if (!Complete(result.ticket) || result.ticket.request != encodedRequest) {
        result.code = ConsumeCode::Corrupt;
    } else if (result.ticket.request == expectedRequest) {
        result.code = ConsumeCode::Matched;
    } else {
        result.code = ConsumeCode::RequestMismatch;
    }

    // WHY: Claimed hides both the copy and zeroing from producers. Publishing the
    // next Empty epoch is the atomic clear point for matched and mismatch results.
    ticket_ = {};
    control_.store(
        EncodeControl(ControlState::Empty, 0u, NextEpoch(epoch)),
        std::memory_order_release);
    return result;
}

CancelCode SinTransitionPublication::Cancel(std::uint32_t request) noexcept {
    if (request == 0u) return CancelCode::InvalidRequest;

    std::uint64_t observed = control_.load(std::memory_order_acquire);
    const ControlState state = DecodeState(observed);
    if (state == ControlState::Empty) return CancelCode::Empty;
    if (DecodeRequest(observed) != request) return CancelCode::RequestMismatch;
    if (state != ControlState::Ready) return CancelCode::Busy;

    const std::uint32_t epoch = DecodeEpoch(observed);
    const std::uint64_t claimed =
        EncodeControl(ControlState::Claimed, request, epoch);
    if (!control_.compare_exchange_strong(
            observed, claimed, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return CancelCode::Raced;
    }

    ticket_ = {};
    control_.store(
        EncodeControl(ControlState::Empty, 0u, NextEpoch(epoch)),
        std::memory_order_release);
    return CancelCode::Cancelled;
}

ResetCode SinTransitionPublication::Reset() noexcept {
    std::uint64_t observed = control_.load(std::memory_order_acquire);
    const ControlState state = DecodeState(observed);
    if (state == ControlState::Empty) return ResetCode::Empty;
    if (state != ControlState::Ready) return ResetCode::Busy;

    const std::uint32_t epoch = DecodeEpoch(observed);
    const std::uint32_t request = DecodeRequest(observed);
    const std::uint64_t claimed =
        EncodeControl(ControlState::Claimed, request, epoch);
    if (!control_.compare_exchange_strong(
            observed, claimed, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return ResetCode::Raced;
    }

    ticket_ = {};
    control_.store(
        EncodeControl(ControlState::Empty, 0u, NextEpoch(epoch)),
        std::memory_order_release);
    return ResetCode::Cleared;
}

bool SinTransitionPublication::IsLockFree() const noexcept {
    return control_.is_lock_free();
}

std::uint32_t SinTransitionPublication::PublicationEpoch() const noexcept {
    return DecodeEpoch(control_.load(std::memory_order_acquire));
}

}  // namespace FfxHooks::SinRam
