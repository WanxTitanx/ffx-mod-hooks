#include "MaechenCore.h"

#include <cstring>

namespace FfxHooks::Maechen {
namespace {

constexpr size_t kMaxLocalLines = kMaxPages * kLinesPerPage;
constexpr char kNativeAtlas[] =
    "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";

bool IsMenuVisible(Phase phase) noexcept {
    return phase == Phase::Editing || phase == Phase::Requesting || phase == Phase::Answer ||
           phase == Phase::Error;
}

bool IsLocaleAllowed(const char* locale) noexcept {
    return locale != nullptr &&
           (strcmp(locale, "pt") == 0 || strcmp(locale, "en") == 0 ||
            strcmp(locale, "es") == 0 || strcmp(locale, "fr") == 0 ||
            strcmp(locale, "it") == 0 || strcmp(locale, "de") == 0);
}

bool IsNativeAtlasByte(uint8_t value) noexcept {
    if (value == 0 || value >= 0x80) return false;
    return strchr(kNativeAtlas, static_cast<int>(value)) != nullptr;
}

char LowerAscii(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return static_cast<char>(value + ('a' - 'A'));
    return value;
}

bool EqualsIgnoreAsciiCase(const char* value, const char* expected) noexcept {
    if (!value || !expected) return false;
    while (*value != '\0' && *expected != '\0') {
        if (LowerAscii(*value) != LowerAscii(*expected)) return false;
        ++value;
        ++expected;
    }
    return *value == '\0' && *expected == '\0';
}

bool MatchIgnoreAsciiCase(const char** cursor, const char* expected) noexcept {
    if (!cursor || !*cursor || !expected) return false;
    const char* value = *cursor;
    while (*expected != '\0') {
        if (*value == '\0' || LowerAscii(*value) != LowerAscii(*expected)) return false;
        ++value;
        ++expected;
    }
    *cursor = value;
    return true;
}

bool IsExactContentType(const char* value) noexcept {
    if (!value) return false;
    const char* cursor = value;
    if (!MatchIgnoreAsciiCase(&cursor, "text/plain")) return false;
    while (*cursor == ' ') ++cursor;
    if (*cursor != ';') return false;
    ++cursor;
    while (*cursor == ' ') ++cursor;
    return MatchIgnoreAsciiCase(&cursor, "charset=utf-8") && *cursor == '\0';
}

uint32_t NextGeneration(uint32_t current) noexcept {
    const uint32_t next = current + 1u;
    // Generation zero is a permanent sentinel, so wrap cannot alias an unowned completion.
    return next == 0 ? 1u : next;
}

void CloseVisibleState(State* state, Actions* actions) noexcept {
    if (!state || !actions || !IsMenuVisible(state->phase)) return;
    actions->requestClose = true;
    if (state->phase == Phase::Requesting) {
        state->cancelRequested = true;
        actions->requestCancel = true;
    }
    // F9Sample owns the release latch; an external close must preserve an observed release.
    state->phase = Phase::Closed;
    state->page = 0;
    state->pageCount = 0;
    state->failure = FailureKind::None;
}

bool AppendLiteral(char* output, size_t capacity, size_t* cursor, const char* literal) noexcept {
    if (!output || !cursor || !literal) return false;
    while (*literal != '\0') {
        if (*cursor >= capacity) return false;
        output[*cursor] = *literal;
        ++*cursor;
        ++literal;
    }
    return true;
}

bool AppendPageLine(Pages* pages, size_t* totalLines, const char* line,
                    size_t length) noexcept {
    if (!pages || !totalLines || !line || length > kLineColumns ||
        *totalLines >= kMaxLocalLines) {
        return false;
    }
    const size_t page = *totalLines / kLinesPerPage;
    const size_t row = *totalLines % kLinesPerPage;
    memcpy(pages->lines[page][row], line, length);
    pages->lines[page][row][length] = '\0';
    pages->lineCount[page] = static_cast<uint8_t>(row + 1);
    pages->pageCount = static_cast<uint8_t>(page + 1);
    ++*totalLines;
    return true;
}

bool AppendLogicalLine(Pages* pages, size_t* totalLines, const char* line,
                       size_t length) noexcept {
    if (length == 0) return AppendPageLine(pages, totalLines, line, 0);
    size_t consumed = 0;
    while (consumed < length) {
        const size_t remaining = length - consumed;
        const size_t chunk = remaining < kLineColumns ? remaining : kLineColumns;
        // Server lines are already bounded; this split is only a local-text guardrail.
        if (!AppendPageLine(pages, totalLines, line + consumed, chunk)) return false;
        consumed += chunk;
    }
    return true;
}

} // namespace

Actions Advance(State* state, const Event& event) noexcept {
    Actions actions{};
    if (!state) return actions;
    if (state->phase == Phase::Stopping) return actions;

    switch (event.kind) {
        case EventKind::SetEnabled:
            if (event.value) {
                if (state->phase == Phase::Disabled) {
                    state->phase = Phase::Closed;
                    // A key held across enablement must not synthesize a fresh rising edge.
                    state->releaseRequired = true;
                    state->cancelRequested = false;
                    state->page = 0;
                    state->pageCount = 0;
                    state->failure = FailureKind::None;
                }
            } else {
                CloseVisibleState(state, &actions);
                state->phase = Phase::Disabled;
                state->releaseRequired = true;
            }
            break;

        case EventKind::F9Sample: {
            const bool rising = event.value && !state->f9WasDown;
            state->f9WasDown = event.value;
            if (!event.value) {
                state->releaseRequired = false;
                break;
            }
            if (!rising || state->releaseRequired || state->phase == Phase::Disabled ||
                state->phase == Phase::Stopping) {
                break;
            }
            state->releaseRequired = true;
            if (state->phase == Phase::Closed) {
                state->phase = Phase::Editing;
                state->page = 0;
                state->pageCount = 0;
                state->failure = FailureKind::None;
                actions.requestOpen = true;
            } else {
                CloseVisibleState(state, &actions);
            }
            break;
        }

        case EventKind::Submit:
            if (state->phase == Phase::Editing || state->phase == Phase::Error) {
                state->generation = NextGeneration(state->generation);
                state->phase = Phase::Requesting;
                state->cancelRequested = false;
                state->page = 0;
                state->pageCount = 0;
                state->failure = FailureKind::None;
                actions.startRequest = true;
            }
            break;

        case EventKind::AskAgain:
            // WHY: Enter on the Answer screen starts a fresh question — without
            // this edge the client was a dead end after the first reply
            // (2026-09-16 RT2: user could not ask twice). Error already routes
            // through Submit's Editing/Error gate, so it joins here too.
            if (state->phase == Phase::Answer || state->phase == Phase::Error) {
                state->phase = Phase::Editing;
                state->page = 0;
                state->pageCount = 0;
                state->failure = FailureKind::None;
            }
            break;

        case EventKind::Complete:
        case EventKind::Fail:
            if (state->phase == Phase::Requesting && event.generation != 0 &&
                event.generation == state->generation) {
                state->phase = event.kind == EventKind::Complete ? Phase::Answer : Phase::Error;
                state->cancelRequested = false;
                state->page = 0;
                state->failure = event.kind == EventKind::Complete
                                     ? FailureKind::None
                                     : (event.failure == FailureKind::None
                                            ? FailureKind::RetryLater
                                            : event.failure);
                actions.acceptCompletion = true;
            }
            break;

        case EventKind::Close:
            CloseVisibleState(state, &actions);
            break;

        case EventKind::FocusLost:
            CloseVisibleState(state, &actions);
            // Focus loss is not a physical release. Re-arm only after a later
            // focused key-up so a held background F9 cannot become a new edge.
            state->f9WasDown = false;
            state->releaseRequired = true;
            break;

        case EventKind::Stop:
            if (state->phase != Phase::Stopping) {
                CloseVisibleState(state, &actions);
                state->phase = Phase::Stopping;
                state->releaseRequired = true;
            }
            break;
    }

    return actions;
}

bool ConsumeFocusedRisingEdge(FocusedEdgeState* state, bool foreground,
                              bool down) noexcept {
    if (!state) return false;
    if (!foreground) {
        state->wasDown = false;
        state->releaseRequired = true;
        return false;
    }

    const bool rising = down && !state->wasDown;
    state->wasDown = down;
    if (!down) {
        state->releaseRequired = false;
        return false;
    }
    if (!rising || state->releaseRequired) return false;
    state->releaseRequired = true;
    return true;
}

ForegroundInputDecision ObserveForegroundInput(ForegroundInputGate* state,
                                               bool foreground) noexcept {
    if (!state) return ForegroundInputDecision::Blocked;
    if (!foreground) {
        state->primeRequired = true;
        return ForegroundInputDecision::Blocked;
    }
    if (state->primeRequired) {
        state->primeRequired = false;
        return ForegroundInputDecision::Prime;
    }
    return ForegroundInputDecision::Sample;
}

SerializeResult SerializeRequest(const char* locale, const char* question, char* output,
                                 size_t outputCapacity, size_t* outputLength) noexcept {
    if (outputLength) *outputLength = 0;
    if (!output || !outputLength) return SerializeResult::NullOutput;
    if (outputCapacity != 0) output[0] = '\0';
    if (!IsLocaleAllowed(locale)) return SerializeResult::InvalidLocale;
    if (!question) return SerializeResult::EmptyQuestion;

    size_t begin = 0;
    while (question[begin] == ' ') ++begin;
    size_t end = begin;
    size_t trimmedLength = 0;
    size_t pendingSpaces = 0;
    // Spaces consume the post-trim budget only when a later byte makes them interior.
    for (size_t index = begin; question[index] != '\0'; ++index) {
        const uint8_t value = static_cast<uint8_t>(question[index]);
        if (!IsNativeAtlasByte(value)) return SerializeResult::InvalidQuestionCharacter;
        if (value == ' ') {
            if (pendingSpaces <= kMaxQuestionBytes) ++pendingSpaces;
            continue;
        }
        if (pendingSpaces > kMaxQuestionBytes - trimmedLength ||
            trimmedLength + pendingSpaces >= kMaxQuestionBytes) {
            return SerializeResult::QuestionTooLong;
        }
        trimmedLength += pendingSpaces + 1;
        pendingSpaces = 0;
        end = index + 1;
    }
    if (begin == end) return SerializeResult::EmptyQuestion;

    size_t escapedLength = 0;
    for (size_t index = begin; index < end; ++index) {
        const uint8_t value = static_cast<uint8_t>(question[index]);
        escapedLength += value == '"' || value == '\\' ? 2u : 1u;
    }

    constexpr size_t kFixedBytes = sizeof("{\"version\":1,\"locale\":\"") - 1 +
                                   sizeof("\",\"question\":\"") - 1 + sizeof("\"}") - 1;
    const size_t serializedLength = kFixedBytes + strlen(locale) + escapedLength;
    if (serializedLength + 1 > outputCapacity) return SerializeResult::OutputTooSmall;

    size_t cursor = 0;
    if (!AppendLiteral(output, outputCapacity, &cursor, "{\"version\":1,\"locale\":\"") ||
        !AppendLiteral(output, outputCapacity, &cursor, locale) ||
        !AppendLiteral(output, outputCapacity, &cursor, "\",\"question\":\"")) {
        return SerializeResult::OutputTooSmall;
    }
    for (size_t index = begin; index < end; ++index) {
        const char value = question[index];
        if (value == '"' || value == '\\') output[cursor++] = '\\';
        output[cursor++] = value;
    }
    output[cursor++] = '"';
    output[cursor++] = '}';
    output[cursor] = '\0';
    *outputLength = cursor;
    return SerializeResult::Ok;
}

ResponseResult ValidateResponse(uint32_t httpStatus, const char* contentType,
                                const char* contentEncoding, const char* protocol,
                                const uint8_t* body, size_t bodyLength) noexcept {
    if (httpStatus != 200) return ResponseResult::WrongStatus;
    if (!IsExactContentType(contentType)) return ResponseResult::WrongContentType;
    if (contentEncoding && *contentEncoding != '\0' &&
        !EqualsIgnoreAsciiCase(contentEncoding, "identity")) {
        return ResponseResult::WrongContentEncoding;
    }
    if (!protocol || strcmp(protocol, "1") != 0) return ResponseResult::WrongProtocol;
    if (bodyLength == 0) return ResponseResult::EmptyBody;
    if (bodyLength > kMaxResponseBytes) return ResponseResult::BodyTooLarge;
    if (!body) return ResponseResult::InvalidAscii;

    size_t lineLength = 0;
    size_t lineCount = 1;
    for (size_t index = 0; index < bodyLength; ++index) {
        const uint8_t value = body[index];
        if (value == '\n') {
            lineLength = 0;
            if (index + 1 < bodyLength) {
                ++lineCount;
                if (lineCount > kMaxRemoteLines) return ResponseResult::InvalidAscii;
            }
            continue;
        }
        if (!IsNativeAtlasByte(value)) return ResponseResult::InvalidAscii;
        ++lineLength;
        if (lineLength > kLineColumns) return ResponseResult::InvalidAscii;
    }
    return ResponseResult::Ok;
}

bool Paginate(const char* text, size_t length, Pages* pages) noexcept {
    if (!pages) return false;
    *pages = Pages{};
    if (!text || length == 0) return false;
    // Each fixed line owns 56 glyph bytes plus at most one terminating LF byte.
    constexpr size_t kMaxRepresentablePageBytes = kMaxLocalLines * (kLineColumns + 1);
    if (length > kMaxRepresentablePageBytes) return false;

    // Build transactionally so an over-cap answer never exposes a partial page set.
    Pages result{};
    size_t totalLines = 0;
    size_t lineStart = 0;
    for (size_t cursor = 0; cursor < length; ++cursor) {
        if (text[cursor] != '\n') continue;
        const size_t lineLength = cursor - lineStart;
        if (!AppendLogicalLine(&result, &totalLines, text + lineStart, lineLength)) return false;
        lineStart = cursor + 1;
    }
    if (lineStart < length &&
        !AppendLogicalLine(&result, &totalLines, text + lineStart, length - lineStart)) {
        return false;
    }

    *pages = result;
    return true;
}

uint8_t ClampPage(int32_t requestedPage, uint8_t pageCount) noexcept {
    if (requestedPage <= 0 || pageCount == 0) return 0;
    const int32_t lastPage = static_cast<int32_t>(pageCount) - 1;
    return static_cast<uint8_t>(requestedPage > lastPage ? lastPage : requestedPage);
}

} // namespace FfxHooks::Maechen
