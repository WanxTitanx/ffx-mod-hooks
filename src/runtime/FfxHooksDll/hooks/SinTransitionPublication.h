#pragma once

#include <atomic>
#include <cstdint>

namespace FfxHooks::SinRam {

// This ticket is deliberately value-only. The eventual adapter can correlate a
// natural Resolver observation without lending this core any game-memory authority.
struct TransitionTicket {
    std::uint32_t request = 0u;
    std::uint32_t encounterToken = 0u;
    std::uint32_t callerRva = 0u;
    std::uint64_t transitionGeneration = 0u;
};

enum class StageCode : std::uint8_t {
    Staged,
    InvalidTicket,
    Occupied,
    Raced,
};

enum class ConsumeCode : std::uint8_t {
    Empty,
    Busy,
    Raced,
    Matched,
    RequestMismatch,
    Corrupt,
};

struct ConsumeResult {
    ConsumeCode code = ConsumeCode::Empty;
    TransitionTicket ticket{};
};

enum class CancelCode : std::uint8_t {
    Cancelled,
    Empty,
    Busy,
    Raced,
    RequestMismatch,
    InvalidRequest,
};

enum class ResetCode : std::uint8_t {
    Cleared,
    Empty,
    Busy,
    Raced,
};

// A single atomic control word owns visibility of the non-atomic payload. Each
// operation is bounded: staging performs one CAS, readers never spin, and the
// payload is accessible only while the caller exclusively owns the control state.
class SinTransitionPublication {
public:
    static constexpr std::uint32_t kMaxPublicationEpoch = 0x3FFFFFFFu;

    explicit SinTransitionPublication(
        std::uint32_t initialPublicationEpoch = 1u) noexcept;

    SinTransitionPublication(const SinTransitionPublication&) = delete;
    SinTransitionPublication& operator=(const SinTransitionPublication&) = delete;

    StageCode Stage(const TransitionTicket& ticket) noexcept;
    ConsumeResult Consume(std::uint32_t expectedRequest) noexcept;
    CancelCode Cancel(std::uint32_t request) noexcept;
    ResetCode Reset() noexcept;

    bool IsLockFree() const noexcept;
    std::uint32_t PublicationEpoch() const noexcept;

private:
    enum class ControlState : std::uint8_t {
        Empty = 0u,
        Writing = 1u,
        Ready = 2u,
        Claimed = 3u,
    };

    static std::uint32_t NormalizeEpoch(std::uint32_t epoch) noexcept;
    static std::uint32_t NextEpoch(std::uint32_t epoch) noexcept;
    static std::uint64_t EncodeControl(
        ControlState state, std::uint32_t request, std::uint32_t epoch) noexcept;
    static ControlState DecodeState(std::uint64_t control) noexcept;
    static std::uint32_t DecodeRequest(std::uint64_t control) noexcept;
    static std::uint32_t DecodeEpoch(std::uint64_t control) noexcept;
    static bool Complete(const TransitionTicket& ticket) noexcept;

    alignas(8) std::atomic<std::uint64_t> control_;
    TransitionTicket ticket_{};
};

}  // namespace FfxHooks::SinRam
