#pragma once

#include <cstddef>
#include <cstdint>

namespace FfxHooks::Maechen {

inline constexpr size_t kMaxQuestionBytes = 1024;
inline constexpr size_t kMaxResponseBytes = 4096;
inline constexpr size_t kLineColumns = 56;
inline constexpr size_t kLinesPerPage = 6;
inline constexpr size_t kMaxPages = 13;
inline constexpr size_t kMaxRemoteLines = 72;

enum class Phase : uint8_t {
    Disabled = 0,
    Closed,
    Editing,
    Requesting,
    Answer,
    Error,
    Stopping,
};

enum class EventKind : uint8_t {
    SetEnabled = 0,
    F9Sample,
    Submit,
    Complete,
    Fail,
    Close,
    FocusLost,
    Stop,
    AskAgain,
};

enum class FailureKind : uint8_t {
    None = 0,
    InvalidQuestion,
    RateLimited,
    RetryLater,
};

struct Event {
    EventKind kind;
    bool value = false;
    uint32_t generation = 0;
    FailureKind failure = FailureKind::None;
};

struct State {
    Phase phase = Phase::Disabled;
    bool f9WasDown = false;
    bool releaseRequired = true;
    bool cancelRequested = false;
    uint32_t generation = 0;
    uint8_t page = 0;
    uint8_t pageCount = 0;
    FailureKind failure = FailureKind::None;
};

struct Actions {
    bool requestOpen = false;
    bool requestClose = false;
    bool startRequest = false;
    bool requestCancel = false;
    bool acceptCompletion = false;
};

Actions Advance(State* state, const Event& event) noexcept;

struct FocusedEdgeState {
    bool wasDown = false;
    bool releaseRequired = true;
};

bool ConsumeFocusedRisingEdge(FocusedEdgeState* state, bool foreground,
                              bool down) noexcept;

enum class ForegroundInputDecision : uint8_t {
    Blocked = 0,
    Prime,
    Sample,
};

struct ForegroundInputGate {
    bool primeRequired = true;
};

ForegroundInputDecision ObserveForegroundInput(ForegroundInputGate* state,
                                               bool foreground) noexcept;

enum class SerializeResult : uint8_t {
    Ok = 0,
    NullOutput,
    InvalidLocale,
    EmptyQuestion,
    QuestionTooLong,
    OutputTooSmall,
    InvalidQuestionCharacter,
};

SerializeResult SerializeRequest(const char* locale, const char* question, char* output,
                                 size_t outputCapacity, size_t* outputLength) noexcept;

enum class ResponseResult : uint8_t {
    Ok = 0,
    WrongStatus,
    WrongContentType,
    WrongContentEncoding,
    WrongProtocol,
    EmptyBody,
    BodyTooLarge,
    InvalidAscii,
};

ResponseResult ValidateResponse(uint32_t httpStatus, const char* contentType,
                                const char* contentEncoding, const char* protocol,
                                const uint8_t* body, size_t bodyLength) noexcept;

struct Pages {
    char lines[kMaxPages][kLinesPerPage][kLineColumns + 1]{};
    uint8_t pageCount = 0;
    uint8_t lineCount[kMaxPages]{};
};

bool Paginate(const char* text, size_t length, Pages* pages) noexcept;
uint8_t ClampPage(int32_t requestedPage, uint8_t pageCount) noexcept;

} // namespace FfxHooks::Maechen
