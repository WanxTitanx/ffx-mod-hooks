#include "MaechenHook.h"
#include "../shared/Config.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include "../../NativeMenuShell/NativeMenuShell.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstring>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace FfxHooks {
namespace {

using OpaqueHandle = uintptr_t;
using WorkerProc = DWORD (WINAPI*)(void*);

constexpr wchar_t kMaechenHost[] = L"ffxmodstudio.com";
constexpr wchar_t kMaechenPath[] = L"/api/maechen/game/v1";
constexpr wchar_t kMaechenMethod[] = L"POST";
constexpr wchar_t kMaechenUserAgent[] = L"FFX-Hooks-Maechen/1";
constexpr uint16_t kMaechenPort = 443;
constexpr wchar_t kMaechenRequestHeaders[] =
    L"Content-Type: application/json; charset=utf-8\r\n"
    L"Accept: text/plain\r\n"
    L"Accept-Encoding: identity\r\n"
    L"X-Maechen-Protocol: 1\r\n";
constexpr int kResolveTimeoutMs = 5000;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kSendTimeoutMs = 10000;
constexpr int kReceiveTimeoutMs = 30000;
constexpr DWORD kDisabledRequestFeatures =
    WINHTTP_DISABLE_REDIRECTS |
    WINHTTP_DISABLE_COOKIES |
    WINHTTP_DISABLE_AUTHENTICATION;
constexpr size_t kResponseProbeBytes = Maechen::kMaxResponseBytes + 1u;
constexpr size_t kRequestBufferBytes = Maechen::kMaxResponseBytes + 1u;
constexpr size_t kMaechenInputCapacity = Maechen::kMaxQuestionBytes + 1u;
constexpr int kMaechenMaxTextDraws = 12;

enum class TransportHeader : uint8_t {
    ContentType = 0,
    Protocol,
    ContentEncoding,
};

struct WorkerPayload {
    uint32_t generation = 0;
    OpaqueHandle cancelEvent = 0;
    uint32_t bodyLength = 0;
    char body[kRequestBufferBytes]{};
};

struct RequestSlot {
    OpaqueHandle worker = 0;
    OpaqueHandle cancelEvent = 0;
    uint32_t generation = 0;
    bool cancelRequested = false;
    WorkerPayload payload{};
    bool completionAvailable = false;
    MaechenCompletion completion{};
};

CRITICAL_SECTION g_slotLock{};
volatile LONG g_installState = 0;
MaechenLogFn g_log = nullptr;
RequestSlot g_slot{};
Maechen::State g_uiState{};
char g_locale[3] = {'p', 't', '\0'};
char g_question[kMaechenInputCapacity]{};
size_t g_questionLength = 0;
Maechen::Pages g_answerPages{};
int g_menuObj = 0;
volatile LONG g_openRequested = 0;
volatile LONG g_closeRequested = 0;
volatile LONG g_blockedLogRequested = 0;
volatile LONG g_menuActiveOrPending = 0;
volatile LONG g_menuVisiblePublished = 0;
volatile LONG g_pumpReapWakePending = 0;
bool g_inputKeyDown[256]{};
bool g_f9ChordSuppressed = false;
Maechen::FocusedEdgeState g_f9InputState{};
Maechen::ForegroundInputGate g_pumpInputGate{};
volatile LONG g_f9ReleaseObserved = 0;
volatile LONG g_f9ReleaseFocusEpoch = 0;
volatile LONG g_focusLossEpoch = 0;
volatile LONG g_presentFocusLossEpoch = 0;
volatile LONG g_pumpFocusLossEpoch = 0;

struct MaechenRenderSnapshot {
    unsigned char header[32]{};
    unsigned char input[Maechen::kLineColumns + 1]{};
    unsigned char status[64]{};
    unsigned char answer[Maechen::kLinesPerPage][Maechen::kLineColumns + 1]{};
    unsigned char page[32]{};
    unsigned char footer[80]{};
    uint8_t answerLineCount = 0;
};

MaechenRenderSnapshot g_renderSnapshot{};
SRWLOCK g_renderSnapshotLock = SRWLOCK_INIT;

#if defined(FFXHOOKS_TESTING)
MaechenTestOps g_testOps{};
MaechenTestNativeReservationPause g_testNativeReservationPause = nullptr;
void* g_testNativeReservationPauseContext = nullptr;
bool g_testSuppressNativeAllocation = false;
Maechen::ForegroundInputDecision g_testLastPumpInputDecision =
    Maechen::ForegroundInputDecision::Blocked;
#endif

void PauseAfterNativeRequestConsumedForTest() noexcept {
#if defined(FFXHOOKS_TESTING)
    if (g_testNativeReservationPause) {
        g_testNativeReservationPause(g_testNativeReservationPauseContext);
    }
#endif
}

bool IsInstalled() noexcept {
    return InterlockedCompareExchange(&g_installState, 2, 2) == 2;
}

bool ForegroundAfterQueuedLoss(volatile LONG* observedEpoch,
                               bool foregroundNow) noexcept {
    if (!observedEpoch) return false;
    const LONG published = InterlockedCompareExchange(&g_focusLossEpoch, 0, 0);
    const LONG observed = InterlockedExchange(observedEpoch, published);
    // WHY: Present and Pump have independent cursors so neither callback can
    // consume a WndProc loss on behalf of the other after callbacks resume.
    return foregroundNow && observed == published;
}

void LogFixed(const char* message) noexcept;

bool IsMaechenLocaleAllowed(const char* locale) noexcept {
    return locale &&
           (strcmp(locale, "pt") == 0 || strcmp(locale, "en") == 0 ||
            strcmp(locale, "es") == 0 || strcmp(locale, "fr") == 0 ||
            strcmp(locale, "it") == 0 || strcmp(locale, "de") == 0);
}

const char* MaechenLocaleFromConfig() noexcept {
    const char* locale = Config::GetString("maechen.locale", "pt");
    if (IsMaechenLocaleAllowed(locale)) return locale;
    LogFixed("Maechen: invalid locale; using pt.");
    return "pt";
}

bool SamplePlainF9() noexcept {
    const bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    const bool modifiersDown =
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    // WHY: suppress the complete physical chord. Releasing a modifier while F9
    // remains held must never manufacture a later plain-F9 rising edge.
    if (!f9Down) g_f9ChordSuppressed = false;
    if (modifiersDown) g_f9ChordSuppressed = true;
    return f9Down && !modifiersDown && !g_f9ChordSuppressed;
}

void ProcessPlainF9Input(bool ffxForeground, bool plainF9Down, bool gameMenuOpen, bool otherCustomMenuOpen) noexcept {
    if (!ffxForeground) {
        Maechen::ConsumeFocusedRisingEdge(&g_f9InputState, false, false);
        g_f9ChordSuppressed = false;
        InterlockedExchange(&g_f9ReleaseObserved, 0);
        InterlockedExchange(&g_openRequested, 0);
        InterlockedExchange(&g_blockedLogRequested, 0);
        if (InterlockedCompareExchange(&g_menuActiveOrPending, 0, 0) != 0) {
            InterlockedExchange(&g_closeRequested, 1);
        }
        return;
    }
    if (!plainF9Down) {
        // WHY: the core state is Pump-owned, so carry the same physical release
        // that re-arms the Present edge instead of manufacturing one in Pump.
        const LONG focusEpoch =
            InterlockedCompareExchange(&g_focusLossEpoch, 0, 0);
        InterlockedExchange(&g_f9ReleaseFocusEpoch, focusEpoch);
        InterlockedExchange(&g_f9ReleaseObserved, 1);
    }
    if (!Maechen::ConsumeFocusedRisingEdge(
            &g_f9InputState, true, plainF9Down)) {
        return;
    }

    if (InterlockedCompareExchange(&g_menuActiveOrPending, 0, 0) != 0) {
        InterlockedExchange(&g_closeRequested, 1);
        return;
    }
    if (gameMenuOpen || otherCustomMenuOpen) {
        InterlockedExchange(&g_blockedLogRequested, 1);
        return;
    }
    InterlockedExchange(&g_menuActiveOrPending, 1);
    InterlockedExchange(&g_openRequested, 1);
}

bool PumpKeyPressed(int virtualKey) noexcept {
    const int key = virtualKey & 0xFF;
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = down && !g_inputKeyDown[key];
    g_inputKeyDown[key] = down;
    return pressed;
}

void PrimePumpKeys() noexcept {
    memset(g_inputKeyDown, 0, sizeof(g_inputKeyDown));
    const int keys[] = {VK_BACK, VK_RETURN, VK_ESCAPE, VK_LEFT, VK_RIGHT, VK_SPACE,
                        VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS,
                        VK_OEM_PERIOD, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5,
                        VK_OEM_6, VK_OEM_7};
    for (int key : keys) g_inputKeyDown[key & 0xFF] =
        (GetAsyncKeyState(key) & 0x8000) != 0;
    for (int key = '0'; key <= '9'; ++key) g_inputKeyDown[key] =
        (GetAsyncKeyState(key) & 0x8000) != 0;
    for (int key = 'A'; key <= 'Z'; ++key) g_inputKeyDown[key] =
        (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool AppendMaechenInputCharacter(char value) noexcept {
    if (value == '\0' || !strchr(NativeMenu::FFX_ATLAS, value) ||
        g_questionLength >= Maechen::kMaxQuestionBytes) {
        return false;
    }
    g_question[g_questionLength++] = value;
    g_question[g_questionLength] = '\0';
    return true;
}

char ShiftedDigit(int virtualKey, bool shiftDown) noexcept {
    if (!shiftDown) return static_cast<char>(virtualKey);
    constexpr char shifted[] = ")!\0#$%^&*(";
    return shifted[virtualKey - '0'];
}

char OemCharacter(int virtualKey, bool shiftDown) noexcept {
    switch (virtualKey) {
        case VK_OEM_1: return shiftDown ? ':' : ';';
        case VK_OEM_PLUS: return shiftDown ? '+' : '=';
        case VK_OEM_COMMA: return shiftDown ? '<' : ',';
        case VK_OEM_MINUS: return shiftDown ? '_' : '-';
        case VK_OEM_PERIOD: return shiftDown ? '>' : '.';
        case VK_OEM_2: return shiftDown ? '?' : '/';
        case VK_OEM_3: return shiftDown ? '\0' : '`';
        case VK_OEM_4: return shiftDown ? '\0' : '[';
        case VK_OEM_5: return shiftDown ? '\0' : '\\';
        case VK_OEM_6: return shiftDown ? '^' : ']';
        case VK_OEM_7: return shiftDown ? '"' : '\'';
        default: return '\0';
    }
}

const char* MaechenStatusText() noexcept {
    switch (g_uiState.phase) {
        case Maechen::Phase::Editing: return "Type a question and press Enter";
        case Maechen::Phase::Requesting: return "Maechen is thinking...";
        case Maechen::Phase::Answer: return "Answer ready";
        case Maechen::Phase::Error:
            switch (g_uiState.failure) {
                case Maechen::FailureKind::InvalidQuestion:
                    return "Question rejected. Edit and retry";
                case Maechen::FailureKind::RateLimited:
                    return "Too many requests. Try again later";
                case Maechen::FailureKind::RetryLater:
                case Maechen::FailureKind::None:
                    return "Service unavailable. Try again later";
            }
            return "Service unavailable. Try again later";
        default: return "";
    }
}

static_assert(sizeof("Question rejected. Edit and retry") - 1 <= Maechen::kLineColumns);
static_assert(sizeof("Too many requests. Try again later") - 1 <= Maechen::kLineColumns);
static_assert(sizeof("Service unavailable. Try again later") - 1 <= Maechen::kLineColumns);

Maechen::FailureKind FailureFromAdapterResult(MaechenAdapterResult result) noexcept {
    switch (result) {
        case MaechenAdapterResult::InvalidQuestion:
            return Maechen::FailureKind::InvalidQuestion;
        case MaechenAdapterResult::RateLimited:
            return Maechen::FailureKind::RateLimited;
        default:
            return Maechen::FailureKind::RetryLater;
    }
}

void PublishRenderSnapshot() noexcept {
    MaechenRenderSnapshot next{};
    NativeMenu::EncodeLabel("Maechen", next.header, static_cast<int>(sizeof(next.header)));
    char inputDisplay[Maechen::kLineColumns + 1]{};
    const size_t shown = g_questionLength < Maechen::kLineColumns
                             ? g_questionLength : Maechen::kLineColumns;
    const size_t start = g_questionLength - shown;
    memcpy(inputDisplay, g_question + start, shown);
    inputDisplay[shown] = '\0';
    NativeMenu::EncodeLabel(inputDisplay, next.input, static_cast<int>(sizeof(next.input)));
    NativeMenu::EncodeLabel(MaechenStatusText(), next.status,
                            static_cast<int>(sizeof(next.status)));
    if (g_uiState.phase == Maechen::Phase::Answer && g_answerPages.pageCount != 0) {
        const uint8_t page = Maechen::ClampPage(g_uiState.page, g_answerPages.pageCount);
        next.answerLineCount = g_answerPages.lineCount[page];
        for (uint8_t line = 0; line < next.answerLineCount; ++line) {
            NativeMenu::EncodeLabel(g_answerPages.lines[page][line], next.answer[line],
                                    static_cast<int>(sizeof(next.answer[line])));
        }
        char pageText[32]{};
        _snprintf_s(pageText, sizeof(pageText), _TRUNCATE, "Page %u/%u",
                    static_cast<unsigned>(page + 1),
                    static_cast<unsigned>(g_answerPages.pageCount));
        NativeMenu::EncodeLabel(pageText, next.page, static_cast<int>(sizeof(next.page)));
    } else {
        NativeMenu::EncodeLabel("Page 0/0", next.page, static_cast<int>(sizeof(next.page)));
    }
    NativeMenu::EncodeLabel("Enter: ask  Left/Right: page  Esc/F9: close", next.footer,
                            static_cast<int>(sizeof(next.footer)));
    AcquireSRWLockExclusive(&g_renderSnapshotLock);
    g_renderSnapshot = next;
    ReleaseSRWLockExclusive(&g_renderSnapshotLock);
}

bool ApplyCompletionFromPump(const MaechenCompletion& completion) noexcept {
    const bool success = completion.result == MaechenAdapterResult::Success;
    const Maechen::Actions actions = Maechen::Advance(
        &g_uiState,
        Maechen::Event{success ? Maechen::EventKind::Complete : Maechen::EventKind::Fail,
                       false, completion.generation,
                       success ? Maechen::FailureKind::None
                               : FailureFromAdapterResult(completion.result)});
    if (!actions.acceptCompletion) return false;

    // WHY: only validated success pages enter the render snapshot. Failure bodies
    // remain opaque; the UI receives one fixed local taxonomy string instead.
    g_answerPages = success ? completion.pages : Maechen::Pages{};
    g_uiState.pageCount = success ? g_answerPages.pageCount : 0;
    PublishRenderSnapshot();
    return true;
}

bool ApplySubmitFailureFromPump(MaechenAdapterResult result) noexcept {
    const Maechen::Actions actions = Maechen::Advance(
        &g_uiState,
        Maechen::Event{Maechen::EventKind::Fail, false, g_uiState.generation,
                       FailureFromAdapterResult(result)});
    if (!actions.acceptCompletion) return false;
    g_answerPages = Maechen::Pages{};
    g_uiState.pageCount = 0;
    PublishRenderSnapshot();
    return true;
}

bool CopyStableRenderSnapshot(MaechenRenderSnapshot* output) noexcept {
    if (!output) return false;
    // WHY: Present must never wait behind the pump. A missed frame is safer than
    // racing a multi-field snapshot or blocking the render thread.
    if (!TryAcquireSRWLockShared(&g_renderSnapshotLock)) return false;
    *output = g_renderSnapshot;
    ReleaseSRWLockShared(&g_renderSnapshotLock);
    return true;
}

void CloseMaechenMenuFromPump(Maechen::EventKind closeKind = Maechen::EventKind::Close) noexcept {
    InterlockedExchange(&g_menuVisiblePublished, 0);
    if (g_menuObj) {
        // WHY: only the pump thread may touch native menu object lifetime and modal ownership.
        NativeMenu::WrB(g_menuObj, 65, 1);
        NativeMenu::ReleaseModalIfOwned(g_menuObj);
        g_menuObj = 0;
    }
    // WHY: request-slot ownership is independent from visible/modal ownership.
    // Closing releases UI/title/force immediately; only a later worker wake may
    // briefly force one pump pass to reap completed handles.
    InterlockedExchange(&g_menuActiveOrPending, 0);
    const Maechen::Actions actions = Maechen::Advance(
        &g_uiState, Maechen::Event{closeKind});
    if (actions.requestCancel) Maechen_CancelRequest();
    PublishRenderSnapshot();
}

void LogFixed(const char* message) noexcept {
    const MaechenLogFn log = g_log;
    if (log && message) log(message);
}

void LogResult(MaechenAdapterResult result) noexcept {
    switch (result) {
        case MaechenAdapterResult::Success:
            LogFixed("Maechen: request finished (success).");
            break;
        case MaechenAdapterResult::InvalidQuestion:
            LogFixed("Maechen: request finished (invalid question).");
            break;
        case MaechenAdapterResult::RateLimited:
            LogFixed("Maechen: request finished (rate limited).");
            break;
        case MaechenAdapterResult::Cancelled:
            LogFixed("Maechen: request finished (cancelled).");
            break;
        case MaechenAdapterResult::Timeout:
            LogFixed("Maechen: request finished (timeout).");
            break;
        case MaechenAdapterResult::RetryLater:
            LogFixed("Maechen: request finished (retry later).");
            break;
        case MaechenAdapterResult::None:
            break;
    }
}

#if defined(FFXHOOKS_TESTING)

bool CompleteTestOperations(const MaechenTestOps& ops) noexcept {
    return ops.context && ops.createCancelEvent && ops.signalCancelEvent &&
           ops.isCancelSignaled && ops.createWorker && ops.isWorkerComplete &&
           ops.closeWorker && ops.closeCancelEvent && ops.openSession &&
           ops.setTimeouts && ops.connect && ops.openRequest && ops.disableFeatures &&
           ops.sendRequest && ops.receiveResponse && ops.queryStatus && ops.queryHeader &&
           ops.readData && ops.closeTransport;
}

bool PlatformReady() noexcept {
    return CompleteTestOperations(g_testOps);
}

uint32_t TransportLastError() noexcept {
    return GetLastError();
}

OpaqueHandle PlatformCreateCancelEvent() noexcept {
    return g_testOps.createCancelEvent(g_testOps.context, true, false);
}

bool PlatformSignalCancelEvent(OpaqueHandle event) noexcept {
    return g_testOps.signalCancelEvent(g_testOps.context, event);
}

bool PlatformIsCancelSignaled(OpaqueHandle event) noexcept {
    return g_testOps.isCancelSignaled(g_testOps.context, event);
}

OpaqueHandle PlatformCreateWorker(WorkerProc workerProc, void* parameter) noexcept {
    return g_testOps.createWorker(
        g_testOps.context,
        reinterpret_cast<MaechenTestWorkerProc>(workerProc), parameter);
}

bool PlatformIsWorkerComplete(OpaqueHandle worker) noexcept {
    return g_testOps.isWorkerComplete(g_testOps.context, worker);
}

bool PlatformCloseWorker(OpaqueHandle worker) noexcept {
    return g_testOps.closeWorker(g_testOps.context, worker);
}

bool PlatformCloseCancelEvent(OpaqueHandle event) noexcept {
    return g_testOps.closeCancelEvent(g_testOps.context, event);
}

OpaqueHandle TransportOpenSession() noexcept {
    return g_testOps.openSession(g_testOps.context, kMaechenUserAgent);
}

bool TransportSetTimeouts(OpaqueHandle session) noexcept {
    return g_testOps.setTimeouts(g_testOps.context, session, kResolveTimeoutMs,
                                 kConnectTimeoutMs, kSendTimeoutMs,
                                 kReceiveTimeoutMs);
}

OpaqueHandle TransportConnect(OpaqueHandle session) noexcept {
    return g_testOps.connect(g_testOps.context, session, kMaechenHost, kMaechenPort);
}

OpaqueHandle TransportOpenRequest(OpaqueHandle connect) noexcept {
    return g_testOps.openRequest(g_testOps.context, connect, kMaechenMethod,
                                 kMaechenPath, WINHTTP_FLAG_SECURE);
}

bool TransportDisableFeatures(OpaqueHandle request) noexcept {
    return g_testOps.disableFeatures(g_testOps.context, request,
                                     kDisabledRequestFeatures);
}

bool TransportSendRequest(OpaqueHandle request, const uint8_t* body,
                          uint32_t bodyLength, uint32_t* errorOut) noexcept {
    constexpr uint32_t kHeaderLength =
        static_cast<uint32_t>((sizeof(kMaechenRequestHeaders) / sizeof(wchar_t)) - 1u);
    return g_testOps.sendRequest(g_testOps.context, request, kMaechenRequestHeaders,
                                 kHeaderLength, body, bodyLength, errorOut);
}

bool TransportReceiveResponse(OpaqueHandle request, uint32_t* errorOut) noexcept {
    return g_testOps.receiveResponse(g_testOps.context, request, errorOut);
}

bool TransportQueryStatus(OpaqueHandle request, uint32_t* statusOut,
                          uint32_t* errorOut) noexcept {
    return g_testOps.queryStatus(g_testOps.context, request, statusOut, errorOut);
}

bool TransportQueryHeader(OpaqueHandle request, TransportHeader header, char* output,
                          uint32_t capacity, uint32_t* lengthOut, bool* missingOut,
                          uint32_t* errorOut) noexcept {
    return g_testOps.queryHeader(
        g_testOps.context, request, static_cast<MaechenTestHeader>(header), output,
        capacity, lengthOut, missingOut, errorOut);
}

bool TransportReadData(OpaqueHandle request, uint8_t* output, uint32_t capacity,
                       uint32_t* readOut, uint32_t* errorOut) noexcept {
    return g_testOps.readData(g_testOps.context, request, output, capacity, readOut,
                              errorOut);
}

bool TransportClose(OpaqueHandle handle) noexcept {
    return g_testOps.closeTransport(g_testOps.context, handle);
}

#else

bool PlatformReady() noexcept {
    return true;
}

uint32_t TransportLastError() noexcept {
    return GetLastError();
}

OpaqueHandle PlatformCreateCancelEvent() noexcept {
    return reinterpret_cast<OpaqueHandle>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
}

bool PlatformSignalCancelEvent(OpaqueHandle event) noexcept {
    return event && SetEvent(reinterpret_cast<HANDLE>(event)) != FALSE;
}

bool PlatformIsCancelSignaled(OpaqueHandle event) noexcept {
    return event && WaitForSingleObject(reinterpret_cast<HANDLE>(event), 0) == WAIT_OBJECT_0;
}

OpaqueHandle PlatformCreateWorker(WorkerProc workerProc, void* parameter) noexcept {
    return reinterpret_cast<OpaqueHandle>(
        CreateThread(nullptr, 0, workerProc, parameter, 0, nullptr));
}

bool PlatformIsWorkerComplete(OpaqueHandle worker) noexcept {
    return worker &&
           WaitForSingleObject(reinterpret_cast<HANDLE>(worker), 0) == WAIT_OBJECT_0;
}

bool PlatformCloseWorker(OpaqueHandle worker) noexcept {
    return worker && CloseHandle(reinterpret_cast<HANDLE>(worker)) != FALSE;
}

bool PlatformCloseCancelEvent(OpaqueHandle event) noexcept {
    return event && CloseHandle(reinterpret_cast<HANDLE>(event)) != FALSE;
}

OpaqueHandle TransportOpenSession() noexcept {
    return reinterpret_cast<OpaqueHandle>(
        WinHttpOpen(kMaechenUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
}

bool TransportSetTimeouts(OpaqueHandle session) noexcept {
    return WinHttpSetTimeouts(reinterpret_cast<HINTERNET>(session), kResolveTimeoutMs,
                              kConnectTimeoutMs, kSendTimeoutMs,
                              kReceiveTimeoutMs) != FALSE;
}

OpaqueHandle TransportConnect(OpaqueHandle session) noexcept {
    return reinterpret_cast<OpaqueHandle>(
        WinHttpConnect(reinterpret_cast<HINTERNET>(session), kMaechenHost,
                       kMaechenPort, 0));
}

OpaqueHandle TransportOpenRequest(OpaqueHandle connect) noexcept {
    return reinterpret_cast<OpaqueHandle>(
        WinHttpOpenRequest(reinterpret_cast<HINTERNET>(connect), kMaechenMethod,
                           kMaechenPath, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
}

bool TransportDisableFeatures(OpaqueHandle request) noexcept {
    DWORD disabled = kDisabledRequestFeatures;
    return WinHttpSetOption(reinterpret_cast<HINTERNET>(request),
                            WINHTTP_OPTION_DISABLE_FEATURE, &disabled,
                            sizeof(disabled)) != FALSE;
}

bool TransportSendRequest(OpaqueHandle request, const uint8_t* body,
                          uint32_t bodyLength, uint32_t* errorOut) noexcept {
    constexpr DWORD kHeaderLength =
        static_cast<DWORD>((sizeof(kMaechenRequestHeaders) / sizeof(wchar_t)) - 1u);
    const BOOL sent = WinHttpSendRequest(
        reinterpret_cast<HINTERNET>(request), kMaechenRequestHeaders, kHeaderLength,
        const_cast<uint8_t*>(body), bodyLength, bodyLength, 0);
    if (errorOut) *errorOut = sent ? ERROR_SUCCESS : GetLastError();
    return sent != FALSE;
}

bool TransportReceiveResponse(OpaqueHandle request, uint32_t* errorOut) noexcept {
    const BOOL received =
        WinHttpReceiveResponse(reinterpret_cast<HINTERNET>(request), nullptr);
    if (errorOut) *errorOut = received ? ERROR_SUCCESS : GetLastError();
    return received != FALSE;
}

bool TransportQueryStatus(OpaqueHandle request, uint32_t* statusOut,
                          uint32_t* errorOut) noexcept {
    DWORD status = 0;
    DWORD bytes = sizeof(status);
    const BOOL queried = WinHttpQueryHeaders(
        reinterpret_cast<HINTERNET>(request),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (statusOut) *statusOut = status;
    if (errorOut) *errorOut = queried ? ERROR_SUCCESS : GetLastError();
    return queried != FALSE;
}

bool TransportQueryHeader(OpaqueHandle request, TransportHeader header, char* output,
                          uint32_t capacity, uint32_t* lengthOut, bool* missingOut,
                          uint32_t* errorOut) noexcept {
    if (lengthOut) *lengthOut = 0;
    if (missingOut) *missingOut = false;
    if (!output || capacity == 0) return false;
    output[0] = '\0';

    DWORD query = 0;
    const wchar_t* name = WINHTTP_HEADER_NAME_BY_INDEX;
    switch (header) {
        case TransportHeader::ContentType:
            query = WINHTTP_QUERY_CONTENT_TYPE;
            break;
        case TransportHeader::Protocol:
            query = WINHTTP_QUERY_CUSTOM;
            name = L"X-Maechen-Protocol";
            break;
        case TransportHeader::ContentEncoding:
            query = WINHTTP_QUERY_CONTENT_ENCODING;
            break;
    }

    wchar_t wide[128]{};
    DWORD bytes = sizeof(wide);
    const BOOL queried = WinHttpQueryHeaders(
        reinterpret_cast<HINTERNET>(request), query, name, wide, &bytes,
        WINHTTP_NO_HEADER_INDEX);
    if (!queried) {
        const DWORD error = GetLastError();
        if (missingOut) *missingOut = error == ERROR_WINHTTP_HEADER_NOT_FOUND;
        if (errorOut) *errorOut = error;
        return false;
    }

    const size_t length = wcsnlen(wide, sizeof(wide) / sizeof(wide[0]));
    if (length >= capacity) {
        if (errorOut) *errorOut = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (wide[index] > 0x7F) {
            if (errorOut) *errorOut = ERROR_NO_UNICODE_TRANSLATION;
            return false;
        }
        output[index] = static_cast<char>(wide[index]);
    }
    output[length] = '\0';
    if (lengthOut) *lengthOut = static_cast<uint32_t>(length);
    if (errorOut) *errorOut = ERROR_SUCCESS;
    return true;
}

bool TransportReadData(OpaqueHandle request, uint8_t* output, uint32_t capacity,
                       uint32_t* readOut, uint32_t* errorOut) noexcept {
    DWORD read = 0;
    const BOOL succeeded = WinHttpReadData(reinterpret_cast<HINTERNET>(request), output,
                                           capacity, &read);
    if (readOut) *readOut = read;
    if (errorOut) *errorOut = succeeded ? ERROR_SUCCESS : GetLastError();
    return succeeded != FALSE;
}

bool TransportClose(OpaqueHandle handle) noexcept {
    return handle && WinHttpCloseHandle(reinterpret_cast<HINTERNET>(handle)) != FALSE;
}

#endif

struct TransportHandles {
    OpaqueHandle session = 0;
    OpaqueHandle connect = 0;
    OpaqueHandle request = 0;

    ~TransportHandles() {
        // Only the worker owns WinHTTP objects, and reverse-open cleanup keeps every
        // early failure from transferring a partially initialized handle elsewhere.
        if (request) TransportClose(request);
        if (connect) TransportClose(connect);
        if (session) TransportClose(session);
    }
};

MaechenAdapterResult ResultFromError(uint32_t error) noexcept {
    return error == ERROR_WINHTTP_TIMEOUT ? MaechenAdapterResult::Timeout
                                          : MaechenAdapterResult::RetryLater;
}

MaechenAdapterResult ResultFromStatus(uint32_t status) noexcept {
    if (status == 400 || status == 413) return MaechenAdapterResult::InvalidQuestion;
    if (status == 429) return MaechenAdapterResult::RateLimited;
    return MaechenAdapterResult::RetryLater;
}

MaechenAdapterResult RunTransport(const WorkerPayload& payload,
                                  Maechen::Pages* pagesOut) noexcept {
    if (!pagesOut) return MaechenAdapterResult::RetryLater;
    *pagesOut = Maechen::Pages{};
    TransportHandles handles{};

    handles.session = TransportOpenSession();
    if (!handles.session) return ResultFromError(TransportLastError());
    if (!TransportSetTimeouts(handles.session)) return ResultFromError(TransportLastError());
    handles.connect = TransportConnect(handles.session);
    if (!handles.connect) return ResultFromError(TransportLastError());
    handles.request = TransportOpenRequest(handles.connect);
    if (!handles.request) return ResultFromError(TransportLastError());
    if (!TransportDisableFeatures(handles.request)) return ResultFromError(TransportLastError());

    // A close can arrive before the worker reaches the first external side effect.
    if (PlatformIsCancelSignaled(payload.cancelEvent)) {
        return MaechenAdapterResult::Cancelled;
    }

    uint32_t error = ERROR_SUCCESS;
    if (!TransportSendRequest(handles.request,
                              reinterpret_cast<const uint8_t*>(payload.body),
                              payload.bodyLength, &error)) {
        return PlatformIsCancelSignaled(payload.cancelEvent)
                   ? MaechenAdapterResult::Cancelled
                   : ResultFromError(error);
    }
    if (!TransportReceiveResponse(handles.request, &error)) {
        return PlatformIsCancelSignaled(payload.cancelEvent)
                   ? MaechenAdapterResult::Cancelled
                   : ResultFromError(error);
    }
    if (PlatformIsCancelSignaled(payload.cancelEvent)) {
        return MaechenAdapterResult::Cancelled;
    }

    uint32_t status = 0;
    uint32_t statusError = ERROR_SUCCESS;
    const bool statusOk = TransportQueryStatus(handles.request, &status, &statusError);

    char contentType[128]{};
    char protocol[32]{};
    char contentEncoding[64]{};
    uint32_t ignoredLength = 0;
    bool contentTypeMissing = false;
    bool protocolMissing = false;
    bool contentEncodingMissing = false;
    uint32_t contentTypeError = ERROR_SUCCESS;
    uint32_t protocolError = ERROR_SUCCESS;
    uint32_t contentEncodingError = ERROR_SUCCESS;
    const bool contentTypeOk = TransportQueryHeader(
        handles.request, TransportHeader::ContentType, contentType,
        static_cast<uint32_t>(sizeof(contentType)), &ignoredLength,
        &contentTypeMissing, &contentTypeError);
    const bool protocolOk = TransportQueryHeader(
        handles.request, TransportHeader::Protocol, protocol,
        static_cast<uint32_t>(sizeof(protocol)), &ignoredLength, &protocolMissing,
        &protocolError);
    const bool contentEncodingOk = TransportQueryHeader(
        handles.request, TransportHeader::ContentEncoding, contentEncoding,
        static_cast<uint32_t>(sizeof(contentEncoding)), &ignoredLength,
        &contentEncodingMissing, &contentEncodingError);

    if (!statusOk) return ResultFromError(statusError);
    // Error bodies are intentionally opaque. Header queries above are bounded
    // diagnostics of the response envelope, not permission to read or display it.
    if (status != 200) return ResultFromStatus(status);
    if (!contentTypeOk || contentTypeMissing || !protocolOk || protocolMissing ||
        (!contentEncodingOk && !contentEncodingMissing)) {
        if (contentTypeError == ERROR_WINHTTP_TIMEOUT ||
            protocolError == ERROR_WINHTTP_TIMEOUT ||
            contentEncodingError == ERROR_WINHTTP_TIMEOUT) {
            return MaechenAdapterResult::Timeout;
        }
        return MaechenAdapterResult::RetryLater;
    }

    const char* encoding = contentEncodingMissing ? nullptr : contentEncoding;
    constexpr uint8_t kEnvelopeProbe = 'A';
    // Reuse the portable validator with one known-safe body byte so protocol policy
    // is settled before any untrusted response byte enters the fixed body buffer.
    if (Maechen::ValidateResponse(status, contentType, encoding, protocol,
                                  &kEnvelopeProbe, 1) != Maechen::ResponseResult::Ok) {
        return MaechenAdapterResult::RetryLater;
    }

    uint8_t body[kResponseProbeBytes]{};
    size_t total = 0;
    while (total < kResponseProbeBytes) {
        // Checking on every loop means a signal caused by the previous read is
        // observed before another network read can begin.
        if (PlatformIsCancelSignaled(payload.cancelEvent)) {
            return MaechenAdapterResult::Cancelled;
        }
        // Fill only the contractual body first; once it is full, request exactly
        // one additional byte so overflow is observed rather than over-read or truncation.
        const uint32_t capacity = total < Maechen::kMaxResponseBytes
                                      ? static_cast<uint32_t>(Maechen::kMaxResponseBytes - total)
                                      : 1u;
        uint32_t read = 0;
        error = ERROR_SUCCESS;
        if (!TransportReadData(handles.request, body + total, capacity, &read, &error)) {
            return PlatformIsCancelSignaled(payload.cancelEvent)
                       ? MaechenAdapterResult::Cancelled
                       : ResultFromError(error);
        }
        if (read > capacity) return MaechenAdapterResult::RetryLater;
        if (read == 0) break;
        total += read;
        if (total > Maechen::kMaxResponseBytes) {
            return MaechenAdapterResult::RetryLater;
        }
        if (PlatformIsCancelSignaled(payload.cancelEvent)) {
            return MaechenAdapterResult::Cancelled;
        }
    }

    if (Maechen::ValidateResponse(status, contentType, encoding, protocol, body, total) !=
        Maechen::ResponseResult::Ok) {
        return MaechenAdapterResult::RetryLater;
    }
    if (!Maechen::Paginate(reinterpret_cast<const char*>(body), total, pagesOut)) {
        *pagesOut = Maechen::Pages{};
        return MaechenAdapterResult::RetryLater;
    }
    return MaechenAdapterResult::Success;
}

bool PublishCompletion(uint32_t generation, MaechenAdapterResult result,
                       const Maechen::Pages* pages) noexcept {
    if (!IsInstalled() || generation == 0 || result == MaechenAdapterResult::None ||
        (result == MaechenAdapterResult::Success && !pages)) {
        return false;
    }
    EnterCriticalSection(&g_slotLock);
    const bool matches = generation == g_slot.generation &&
                         (g_slot.worker != 0 || g_slot.cancelEvent != 0);
    if (matches) {
        MaechenCompletion completion{};
        completion.generation = generation;
        completion.result = result;
        if (result == MaechenAdapterResult::Success) completion.pages = *pages;
        // One lock commits the generation, taxonomy, and fixed pages as a single
        // snapshot, so Present can never observe mixed generations.
        g_slot.completion = completion;
        g_slot.completionAvailable = true;
    }
    LeaveCriticalSection(&g_slotLock);
    return matches;
}

DWORD WINAPI MaechenWorkerMain(void* parameter) {
    if (!parameter) return 0;
    // The slot cannot be reused until pump reap, but taking an immutable copy also
    // prevents later UI work from accidentally making the worker read live UI data.
    const WorkerPayload payload = *static_cast<const WorkerPayload*>(parameter);
    Maechen::Pages pages{};
    const MaechenAdapterResult result = RunTransport(payload, &pages);
    const bool published = PublishCompletion(
        payload.generation, result,
        result == MaechenAdapterResult::Success ? &pages : nullptr);
    if (published) LogResult(result);
    // The worker owns this one-shot publication. Present never polls a worker
    // handle; it only observes this atomic after transport/completion work ends.
    InterlockedExchange(&g_pumpReapWakePending, 1);
    return 0;
}

bool SlotBusyLocked() noexcept {
    return g_slot.worker != 0 || g_slot.cancelEvent != 0;
}

void ClearReapedSlotLocked() noexcept {
    if (g_slot.worker != 0 || g_slot.cancelEvent != 0) return;
    g_slot.generation = 0;
    g_slot.cancelRequested = false;
    g_slot.payload = WorkerPayload{};
}

} // namespace

static int __cdecl MaechenPumpDraw(int obj);

long NativeMenu_ReserveAndConsumeOpenRequest(volatile long* request,
                                             long emptyValue,
                                             volatile long* ownerPublished) noexcept {
    if (!request || !ownerPublished ||
        InterlockedCompareExchange(request, emptyValue, emptyValue) == emptyValue) {
        return emptyValue;
    }
    // WHY: Present observes request-or-owner while Pump owns allocation. Publish
    // the reservation before clearing the request so no frame can see false idle.
    InterlockedExchange(ownerPublished, 1);
    const long consumed = InterlockedExchange(request, emptyValue);
    PauseAfterNativeRequestConsumedForTest();
    return consumed;
}

bool Maechen_Install(MaechenLogFn log) {
    if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) return false;
    if (!InitializeCriticalSectionEx(&g_slotLock, 4000, 0)) {
        InterlockedExchange(&g_installState, 0);
        return false;
    }
    g_log = log;
    const char* locale = MaechenLocaleFromConfig();
    strncpy_s(g_locale, locale, _TRUNCATE);
    g_uiState = Maechen::State{};
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::SetEnabled, true});
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::F9Sample, false});
    g_question[0] = '\0';
    g_questionLength = 0;
    g_answerPages = Maechen::Pages{};
    memset(g_inputKeyDown, 0, sizeof(g_inputKeyDown));
    g_f9ChordSuppressed = false;
    g_f9InputState = Maechen::FocusedEdgeState{};
    g_pumpInputGate = Maechen::ForegroundInputGate{};
    InterlockedExchange(&g_f9ReleaseObserved, 0);
    const LONG focusLossEpoch =
        InterlockedCompareExchange(&g_focusLossEpoch, 0, 0);
    InterlockedExchange(&g_f9ReleaseFocusEpoch, focusLossEpoch);
    InterlockedExchange(&g_presentFocusLossEpoch, focusLossEpoch);
    InterlockedExchange(&g_pumpFocusLossEpoch, focusLossEpoch);
#if defined(FFXHOOKS_TESTING)
    g_testSuppressNativeAllocation = false;
    g_testLastPumpInputDecision = Maechen::ForegroundInputDecision::Blocked;
#endif
    InterlockedExchange(&g_openRequested, 0);
    InterlockedExchange(&g_closeRequested, 0);
    InterlockedExchange(&g_blockedLogRequested, 0);
    InterlockedExchange(&g_menuActiveOrPending, 0);
    InterlockedExchange(&g_menuVisiblePublished, 0);
    InterlockedExchange(&g_pumpReapWakePending, 0);
    PublishRenderSnapshot();
    InterlockedExchange(&g_installState, 2);
    LogFixed("Maechen: request slot initialized.");
    return true;
}

bool Maechen_SubmitRequest(const char* locale, const char* question,
                           uint32_t generation,
                           MaechenAdapterResult* rejectionOut) {
    if (rejectionOut) *rejectionOut = MaechenAdapterResult::RetryLater;
    if (!IsInstalled() || !PlatformReady() || generation == 0) return false;

    char serialized[kRequestBufferBytes]{};
    size_t serializedLength = 0;
    const Maechen::SerializeResult serializeResult = Maechen::SerializeRequest(
        locale, question, serialized, sizeof(serialized), &serializedLength);
    if (serializeResult != Maechen::SerializeResult::Ok ||
        serializedLength == 0 || serializedLength > Maechen::kMaxResponseBytes) {
        // WHY: local serialization failures describe the submitted question;
        // platform, slot, and allocation failures retain the retry-later default.
        if (rejectionOut) *rejectionOut = MaechenAdapterResult::InvalidQuestion;
        LogFixed("Maechen: request rejected (invalid input).");
        return false;
    }

    EnterCriticalSection(&g_slotLock);
    if (SlotBusyLocked()) {
        LeaveCriticalSection(&g_slotLock);
        LogFixed("Maechen: request rejected (busy).");
        return false;
    }

    const OpaqueHandle cancelEvent = PlatformCreateCancelEvent();
    if (!cancelEvent) {
        LeaveCriticalSection(&g_slotLock);
        LogFixed("Maechen: request rejected (cancel allocation failed).");
        return false;
    }

    g_slot.generation = generation;
    g_slot.cancelEvent = cancelEvent;
    g_slot.cancelRequested = false;
    g_slot.completionAvailable = false;
    g_slot.completion = MaechenCompletion{};
    g_slot.payload = WorkerPayload{};
    g_slot.payload.generation = generation;
    g_slot.payload.cancelEvent = cancelEvent;
    g_slot.payload.bodyLength = static_cast<uint32_t>(serializedLength);
    memcpy(g_slot.payload.body, serialized, serializedLength);
    g_slot.payload.body[serializedLength] = '\0';

    const OpaqueHandle worker = PlatformCreateWorker(&MaechenWorkerMain, &g_slot.payload);
    if (!worker) {
        // If the first rollback close fails, keep ownership visible so the normal
        // pump can retry it; forgetting the handle would turn failure into a leak.
        if (PlatformCloseCancelEvent(cancelEvent)) g_slot.cancelEvent = 0;
        ClearReapedSlotLocked();
        LeaveCriticalSection(&g_slotLock);
        LogFixed("Maechen: request rejected (worker allocation failed).");
        return false;
    }
    g_slot.worker = worker;
    LeaveCriticalSection(&g_slotLock);
    if (rejectionOut) *rejectionOut = MaechenAdapterResult::None;
    LogFixed("Maechen: request started.");
    return true;
}

void Maechen_CancelRequest() noexcept {
    if (!IsInstalled()) return;
    bool requested = false;
    EnterCriticalSection(&g_slotLock);
    if (g_slot.cancelEvent && !g_slot.cancelRequested) {
        g_slot.cancelRequested = true;
        PlatformSignalCancelEvent(g_slot.cancelEvent);
        requested = true;
    }
    LeaveCriticalSection(&g_slotLock);
    if (requested) LogFixed("Maechen: cancellation requested.");
}

bool Maechen_TakeCompletion(MaechenCompletion* completionOut) noexcept {
    if (!IsInstalled() || !completionOut) return false;
    EnterCriticalSection(&g_slotLock);
    const bool available = g_slot.completionAvailable;
    if (available) {
        *completionOut = g_slot.completion;
        g_slot.completionAvailable = false;
        g_slot.completion = MaechenCompletion{};
    }
    LeaveCriticalSection(&g_slotLock);
    return available;
}

bool Maechen_RequestBusy() noexcept {
    if (!IsInstalled()) return false;
    EnterCriticalSection(&g_slotLock);
    const bool busy = SlotBusyLocked();
    LeaveCriticalSection(&g_slotLock);
    return busy;
}

void Maechen_NotifyForegroundLost() noexcept {
    InterlockedIncrement(&g_focusLossEpoch);
}

void Maechen_PresentTick(bool gameMenuOpen, bool otherCustomMenuOpen, bool ffxForeground) {
    if (!IsInstalled()) return;
    const bool inputForeground = ForegroundAfterQueuedLoss(
        &g_presentFocusLossEpoch, ffxForeground);
    // The physical key is read only after the caller proves that an FFX-owned
    // window is foreground. Background state is represented by the release barrier.
    ProcessPlainF9Input(inputForeground,
                        inputForeground ? SamplePlainF9() : false,
                        gameMenuOpen, otherCustomMenuOpen);
}

void Maechen_PumpTick(bool ffxForeground) {
    if (!IsInstalled()) return;

    const bool inputForeground = ForegroundAfterQueuedLoss(
        &g_pumpFocusLossEpoch, ffxForeground);
    const Maechen::ForegroundInputDecision inputDecision =
        Maechen::ObserveForegroundInput(&g_pumpInputGate, inputForeground);
    const LONG pumpFocusEpoch =
        InterlockedCompareExchange(&g_pumpFocusLossEpoch, 0, 0);
#if defined(FFXHOOKS_TESTING)
    g_testLastPumpInputDecision = inputDecision;
#endif
    if (inputDecision == Maechen::ForegroundInputDecision::Blocked) {
        // The pump owns menu lifetime. It closes only Maechen here; the shared
        // owner callback below keeps unrelated F7/F8/Arena state authoritative.
        memset(g_inputKeyDown, 0, sizeof(g_inputKeyDown));
        const LONG releaseObserved =
            InterlockedExchange(&g_f9ReleaseObserved, 0);
        const LONG releaseFocusEpoch =
            InterlockedCompareExchange(&g_f9ReleaseFocusEpoch, 0, 0);
        // WHY: a focused release published after Present consumed this same
        // queued loss is newer than Pump's lagging blocked transition.
        if (ffxForeground && releaseObserved != 0 &&
            releaseFocusEpoch == pumpFocusEpoch) {
            InterlockedExchange(&g_f9ReleaseObserved, 1);
        }
        InterlockedExchange(&g_openRequested, 0);
        InterlockedExchange(&g_closeRequested, 0);
        InterlockedExchange(&g_blockedLogRequested, 0);
        if (g_menuObj ||
            InterlockedCompareExchange(&g_menuActiveOrPending, 0, 0) != 0) {
            CloseMaechenMenuFromPump(Maechen::EventKind::FocusLost);
        }
    } else if (inputDecision == Maechen::ForegroundInputDecision::Prime) {
        // Prime held keys without producing input on the first focused frame.
        PrimePumpKeys();
    }

    // WHY: allocation, registration, modal ownership, text input, submission and
    // handle reap stay on the native menu pump thread; Present only publishes edges.
    if (InterlockedExchange(&g_blockedLogRequested, 0) != 0) {
        LogFixed("Maechen: F9 blocked (another menu is active).");
    }
    if (inputDecision != Maechen::ForegroundInputDecision::Blocked &&
        InterlockedExchange(&g_f9ReleaseObserved, 0) != 0) {
        const LONG releaseFocusEpoch =
            InterlockedCompareExchange(&g_f9ReleaseFocusEpoch, 0, 0);
        if (releaseFocusEpoch == pumpFocusEpoch) {
            Maechen::Advance(&g_uiState,
                             Maechen::Event{Maechen::EventKind::F9Sample, false});
        }
    }
    if (inputDecision != Maechen::ForegroundInputDecision::Blocked &&
        InterlockedExchange(&g_openRequested, 0) != 0 && !g_menuObj) {
        const Maechen::Actions openActions = Maechen::Advance(
            &g_uiState, Maechen::Event{Maechen::EventKind::F9Sample, true});
        if (!openActions.requestOpen) {
            InterlockedExchange(&g_menuActiveOrPending, 0);
        } else {
#if defined(FFXHOOKS_TESTING)
            if (g_testSuppressNativeAllocation) {
                PublishRenderSnapshot();
            } else {
#endif
                g_menuObj = NativeMenu::Alloc();
                if (g_menuObj) {
                    NativeMenu::WrW(g_menuObj, NativeMenu::O_COUNT, 1);
                    NativeMenu::WrW(g_menuObj, NativeMenu::O_PAGE, 1);
                    NativeMenu::WrW(g_menuObj, NativeMenu::O_TOP, 0);
                    NativeMenu::WrW(g_menuObj, NativeMenu::O_SELECTED, 0);
                    NativeMenu::WrB(g_menuObj, NativeMenu::O_SLOTS, 1);
                    NativeMenu::WrB(g_menuObj, NativeMenu::O_CANCEL, 1);
                    NativeMenu::WrB(g_menuObj, NativeMenu::O_GROUP62, 2);
                    NativeMenu::WrB(g_menuObj, NativeMenu::O_GROUP63, 1);
                    NativeMenu::WrP(g_menuObj, NativeMenu::O_UPDATE, nullptr);
                    // WHY: draw must live inside the pump's batch phase. The
                    // previous Present-time emission ran after the engine's
                    // enqueue/flush and produced the F9 black screen.
                    NativeMenu::WrP(g_menuObj, NativeMenu::O_DRAW,
                                    reinterpret_cast<void*>(&MaechenPumpDraw));
                    NativeMenu::WrP(g_menuObj, NativeMenu::O_AUX,
                                    reinterpret_cast<void*>(&NativeMenu::OurAux));
                    // WHY: modal claim prevents the game FSM from consuming Maechen keystrokes.
                    NativeMenu::ClaimModal(g_menuObj);
                    NativeMenu::Register(g_menuObj);
                    InterlockedExchange(&g_menuVisiblePublished, 1);
                    PrimePumpKeys();
                    PublishRenderSnapshot();
                } else {
                    Maechen::Advance(
                        &g_uiState, Maechen::Event{Maechen::EventKind::Close});
                    InterlockedExchange(&g_menuActiveOrPending, 0);
                    LogFixed("Maechen: native menu allocation failed.");
                }
#if defined(FFXHOOKS_TESTING)
            }
#endif
        }
    }

    if (InterlockedExchange(&g_closeRequested, 0) != 0) {
        CloseMaechenMenuFromPump();
    }

    EnterCriticalSection(&g_slotLock);
    const OpaqueHandle worker = g_slot.worker;
    // Keep the zero-time observation and close under one ownership lock so a
    // concurrent Present callback can never poll a handle Pump just reaped.
    bool workerComplete = false;
    if (worker && !PlatformIsWorkerComplete(worker)) {
        workerComplete = false;
    } else if (worker) {
        workerComplete = true;
    }
    if (workerComplete) {
        // A live handle is never closed as cancellation. This branch is reachable
        // only after the zero-time completion observation above succeeds.
        if (!PlatformCloseWorker(worker)) {
            LeaveCriticalSection(&g_slotLock);
            return;
        }
        g_slot.worker = 0;
    }
    if (!g_slot.worker && g_slot.cancelEvent) {
        if (!PlatformCloseCancelEvent(g_slot.cancelEvent)) {
            LeaveCriticalSection(&g_slotLock);
            return;
        }
        g_slot.cancelEvent = 0;
    }
    ClearReapedSlotLocked();
    const bool requestBusy = SlotBusyLocked();
    LeaveCriticalSection(&g_slotLock);

    if (!requestBusy) {
        // Clear only after both worker and cancel handles have been reaped.
        InterlockedExchange(&g_pumpReapWakePending, 0);
    }
    MaechenCompletion completion{};
    if (!requestBusy && g_menuObj && Maechen_TakeCompletion(&completion)) {
        ApplyCompletionFromPump(completion);
    }

    if (!g_menuObj ||
        inputDecision != Maechen::ForegroundInputDecision::Sample) {
        return;
    }

    if (PumpKeyPressed(VK_ESCAPE)) {
        CloseMaechenMenuFromPump();
        return;
    }
    // WHY: text editing keys belong to the Editing/Error phases only. Before
    // this gate, typing on the Answer screen silently grew the hidden question
    // buffer while Enter did nothing (2026-09-16 RT2 report).
    const bool editingPhase = g_uiState.phase == Maechen::Phase::Editing ||
                              g_uiState.phase == Maechen::Phase::Error;
    if (PumpKeyPressed(VK_BACK) && editingPhase) {
        if (g_questionLength != 0) g_question[--g_questionLength] = '\0';
        PublishRenderSnapshot();
    }
    if (PumpKeyPressed(VK_LEFT) && g_uiState.phase == Maechen::Phase::Answer) {
        g_uiState.page = Maechen::ClampPage(static_cast<int32_t>(g_uiState.page) - 1,
                                            g_answerPages.pageCount);
        PublishRenderSnapshot();
    }
    if (PumpKeyPressed(VK_RIGHT) && g_uiState.phase == Maechen::Phase::Answer) {
        g_uiState.page = Maechen::ClampPage(static_cast<int32_t>(g_uiState.page) + 1,
                                            g_answerPages.pageCount);
        PublishRenderSnapshot();
    }

    bool inputChanged = false;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    for (int key = 'A'; key <= 'Z' && editingPhase; ++key) {
        if (PumpKeyPressed(key)) {
            inputChanged |= AppendMaechenInputCharacter(
                shiftDown ? static_cast<char>(key) : static_cast<char>(key + ('a' - 'A')));
        }
    }
    for (int key = '0'; key <= '9' && editingPhase; ++key) {
        if (PumpKeyPressed(key)) inputChanged |= AppendMaechenInputCharacter(
            ShiftedDigit(key, shiftDown));
    }
    if (editingPhase && PumpKeyPressed(VK_SPACE)) {
        inputChanged |= AppendMaechenInputCharacter(' ');
    }
    for (int key : {VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS,
                    VK_OEM_PERIOD, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5,
                    VK_OEM_6, VK_OEM_7}) {
        if (editingPhase && PumpKeyPressed(key)) {
            const char value = OemCharacter(key, shiftDown);
            if (value) inputChanged |= AppendMaechenInputCharacter(value);
        }
    }
    if (inputChanged) PublishRenderSnapshot();

    // WHY: PumpKeyPressed consumes the rising edge by updating g_inputKeyDown.
    // Sample Return once, then route that one edge by phase; sampling separately
    // for Answer and Editing made the first condition swallow every submission.
    const bool enterPressed = PumpKeyPressed(VK_RETURN);
    if (enterPressed && g_uiState.phase == Maechen::Phase::Answer &&
        !Maechen_RequestBusy()) {
        // WHY: Enter on the Answer screen is the documented "ask" key — it must
        // reopen the editor instead of dead-ending the session. The question
        // buffer resets so the previous query never leaks into the next ask.
        g_questionLength = 0;
        g_question[0] = '\0';
        Maechen::Advance(&g_uiState,
                         Maechen::Event{Maechen::EventKind::AskAgain, false});
        PublishRenderSnapshot();
        return;
    }
    if (enterPressed && g_questionLength != 0 && !Maechen_RequestBusy() &&
        editingPhase) {
        const Maechen::Actions actions = Maechen::Advance(
            &g_uiState, Maechen::Event{Maechen::EventKind::Submit});
        MaechenAdapterResult rejectionResult = MaechenAdapterResult::RetryLater;
        if (actions.startRequest &&
            Maechen_SubmitRequest(g_locale, g_question, g_uiState.generation,
                                  &rejectionResult)) {
            PublishRenderSnapshot();
        } else {
            ApplySubmitFailureFromPump(rejectionResult);
        }
    }
}

// Pump-side object draw callback (O_DRAW, +16). The vanilla pump invokes it
// during the menu batch enqueue/flush phase — the only safe point to emit
// native 2D. The former Maechen_PresentDraw emitted from Present after that
// phase closed, so its quads never rendered (F9 black screen, 2026-09-16).
static int __cdecl MaechenPumpDraw(int obj) {
    if (!IsInstalled() ||
        InterlockedCompareExchange(&g_menuVisiblePublished, 0, 0) == 0) return obj;
    MaechenRenderSnapshot stable{};
    if (!CopyStableRenderSnapshot(&stable)) return obj;

    static int s_drawFrame = 0;
    const int F = ++s_drawFrame;   // WHY: pump-thread cosmetic clock for caret/accent pulses.

    // WHY: same shell vocabulary as F7/F8 but translucent — the user asked
    // for the field to stay visible behind the window (RT2, 2026-09-16).
    // A single light scrim + the Spira worldmap replace DrawMenuBackdrop's
    // three darkening layers, and a glass panel replaces the opaque vanilla
    // DrawWindow as the outer container.
    const float fw = NativeMenu::MenuPhysW(), fh = NativeMenu::MenuPhysH();
    NativeMenu::DrawSolidRect(0.0f, 0.0f, fw, fh, 0x38081018u, 0x4004080Cu);
    NativeMenu::DrawTexByAtlasId(NativeMenu::kAtlasWorldmap, 0.0f, 0.0f, fw, fh,
                                 0.0f, 0.0f, 1.0f, 1.0f, 0x4888A8B8u, 0x40586878u);
    NativeMenu::DrawSolidRect(0.0f, 0.0f, fw, fh, 0x20081018u, 0x2804080Cu);

    const float ix = NativeMenu::NX(0.155f);   // inner panel left
    const float iw = NativeMenu::NW(0.69f);    // inner panel width
    NativeMenu::DrawMenuGlassPanel(NativeMenu::NX(0.14f), NativeMenu::NY(0.12f),
                                   NativeMenu::NW(0.72f), NativeMenu::NH(0.70f), F, 0);
    NativeMenu::DrawMenuGlassPanel(ix, NativeMenu::NY(0.140f), iw,
                                   NativeMenu::NH(0.088f), F, 0);
    NativeMenu::DrawMenuGlassPanel(ix, NativeMenu::NY(0.248f), iw,
                                   NativeMenu::NH(0.058f), F, 0);
    NativeMenu::DrawMenuGlassPanel(ix, NativeMenu::NY(0.326f), iw,
                                   NativeMenu::NH(0.385f), F, 0);
    NativeMenu::DrawMenuGlassPanel(ix, NativeMenu::NY(0.731f), iw,
                                   NativeMenu::NH(0.068f), F, 0);

    // Scholar-teal accent line under the title, gently pulsing.
    const unsigned int accent =
        NativeMenu::ColorLerp(0xE02878B0u, 0xE058C8E8u, NativeMenu::Osc01(F, 72));
    NativeMenu::DrawPlasma(NativeMenu::NX(0.175f), NativeMenu::NY(0.212f),
                           NativeMenu::NW(0.30f), NativeMenu::NH(0.006f),
                           accent, accent);

    int draws = 0;
    const auto draw = [&](const unsigned char* text, float x, float y) {
        if (text && text[0] && draws < kMaechenMaxTextDraws) {
            NativeMenu::DrawStringSub(text, NativeMenu::NX(x), NativeMenu::NY(y));
            ++draws;
        }
    };
    const auto drawTitle = [&](const unsigned char* text, float x, float y) {
        if (text && text[0] && draws < kMaechenMaxTextDraws) {
            NativeMenu::DrawString(text, NativeMenu::NX(x), NativeMenu::NY(y));
            ++draws;
        }
    };

    drawTitle(stable.header, 0.175f, 0.152f);
    draw(stable.status, 0.52f, 0.165f);

    // Blinking caret block marks the live input line inside its band.
    const unsigned int caret =
        ((0x40u + (unsigned int)(NativeMenu::Osc01(F, 50) * 96.0f)) << 24) |
        0x00C0E8FFu;
    NativeMenu::DrawSolidRect(NativeMenu::NX(0.172f), NativeMenu::NY(0.260f),
                              NativeMenu::NW(0.005f), NativeMenu::NH(0.026f),
                              caret, caret);
    draw(stable.input, 0.190f, 0.258f);

    // Raw responses remain worker-owned. Pump publishes only six pre-encoded,
    // paginated atlas lines so transport/private metadata cannot reach drawing.
    for (uint8_t line = 0;
         line < stable.answerLineCount && line < Maechen::kLinesPerPage;
         ++line) {
        draw(stable.answer[line], 0.175f, 0.348f + 0.058f * line);
    }
    draw(stable.page, 0.175f, 0.748f);
    // WHY: the footer hints now render the real keyboard glyphs (keyboard_icon
    // sheet) instead of the "Enter:/Esc:" text. Maechen is keyboard-driven, so the
    // pad sheet is never the right device here. Labels still go through the shared
    // text path, so they count against the same frame cap.
    {
        float hintX = NativeMenu::NX(0.40f);
        const float hintY = NativeMenu::NY(0.742f);
        static const struct { int a, b; const char* t; } kHints[3] = {
            { NativeMenu::PC_KB_ENTER, -1, "Ask" },
            { NativeMenu::PC_KB_LEFT, NativeMenu::PC_KB_RIGHT, "Page" },
            { NativeMenu::PC_KB_ESC, NativeMenu::PC_KB_F9, "Close" },
        };
        for (int i = 0; i < 3 && draws + 1 <= kMaechenMaxTextDraws; ++i) {
            hintX = NativeMenu::DrawInputHint(hintX, hintY, -1, -1,
                                              kHints[i].a, kHints[i].b,
                                              kHints[i].t, 0xFFFFFFFFu);
            ++draws;
        }
    }
    return obj;
}

bool Maechen_MenuOwned() noexcept {
    return IsInstalled() &&
           InterlockedCompareExchange(&g_menuActiveOrPending, 0, 0) != 0;
}

bool Maechen_PumpWakePending() noexcept {
    return IsInstalled() &&
           InterlockedCompareExchange(&g_pumpReapWakePending, 0, 0) != 0;
}

bool Maechen_BlocksNativeModalAllocation() noexcept {
    // WHY: a visible/pending Maechen modal and its terminal one-shot reap wake
    // both require the shared native pump to finish before another allocation.
    return Maechen_MenuOwned() || Maechen_PumpWakePending();
}

bool Maechen_IsActive() noexcept {
    return Maechen_MenuOwned() || Maechen_PumpWakePending();
}

#if defined(FFXHOOKS_TESTING)

bool Maechen_TestInstallTransport(const MaechenTestOps* operations) noexcept {
    if (!IsInstalled() || !operations || !CompleteTestOperations(*operations)) return false;
    EnterCriticalSection(&g_slotLock);
    if (SlotBusyLocked()) {
        LeaveCriticalSection(&g_slotLock);
        return false;
    }
    g_testOps = *operations;
    g_slot.completionAvailable = false;
    g_slot.completion = MaechenCompletion{};
    LeaveCriticalSection(&g_slotLock);
    return true;
}

bool Maechen_TestTryPublishCompletion(uint32_t generation,
                                      MaechenAdapterResult result,
                                      const Maechen::Pages* pages) noexcept {
    return PublishCompletion(generation, result, pages);
}

MaechenTestHandle Maechen_TestCancelIdentity() noexcept {
    if (!IsInstalled()) return 0;
    EnterCriticalSection(&g_slotLock);
    const MaechenTestHandle identity = g_slot.cancelEvent;
    LeaveCriticalSection(&g_slotLock);
    return identity;
}

void Maechen_TestSetNativeReservationPause(
    MaechenTestNativeReservationPause pause, void* context) noexcept {
    g_testNativeReservationPauseContext = context;
    g_testNativeReservationPause = pause;
}

bool Maechen_TestPublishMenuOwnedForClose() noexcept {
    if (!IsInstalled()) return false;
    g_uiState = Maechen::State{};
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::SetEnabled, true});
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::F9Sample, false});
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::F9Sample, true});
    Maechen::Advance(&g_uiState, Maechen::Event{Maechen::EventKind::Submit});
    InterlockedExchange(&g_menuActiveOrPending, 1);
    InterlockedExchange(&g_menuVisiblePublished, 1);
    return Maechen_MenuOwned() && g_uiState.phase == Maechen::Phase::Requesting;
}

void Maechen_TestCloseMenuFromPump() noexcept {
    CloseMaechenMenuFromPump();
}

void Maechen_TestPrepareRequestingState(uint32_t generation) noexcept {
    g_uiState = Maechen::State{};
    g_uiState.phase = Maechen::Phase::Requesting;
    g_uiState.generation = generation;
    g_answerPages = Maechen::Pages{};
    PublishRenderSnapshot();
}

bool Maechen_TestApplyCompletionToUi(MaechenAdapterResult result,
                                     uint32_t generation) noexcept {
    MaechenCompletion completion{};
    completion.generation = generation;
    completion.result = result;
    return ApplyCompletionFromPump(completion);
}

bool Maechen_TestApplySubmitFailureToUi(MaechenAdapterResult result) noexcept {
    return ApplySubmitFailureFromPump(result);
}

bool Maechen_TestCopyPublishedStatus(unsigned char* output,
                                     size_t capacity) noexcept {
    if (!output || capacity < sizeof(g_renderSnapshot.status)) return false;
    AcquireSRWLockShared(&g_renderSnapshotLock);
    memcpy(output, g_renderSnapshot.status, sizeof(g_renderSnapshot.status));
    ReleaseSRWLockShared(&g_renderSnapshotLock);
    return true;
}

void Maechen_TestPresentInput(bool ffxForeground, bool plainF9Down,
                              bool gameMenuOpen,
                              bool otherCustomMenuOpen) noexcept {
    if (!IsInstalled()) return;
    const bool inputForeground = ForegroundAfterQueuedLoss(
        &g_presentFocusLossEpoch, ffxForeground);
    ProcessPlainF9Input(inputForeground,
                        inputForeground ? plainF9Down : false,
                        gameMenuOpen, otherCustomMenuOpen);
}

Maechen::Phase Maechen_TestUiPhase() noexcept {
    return g_uiState.phase;
}

Maechen::ForegroundInputDecision Maechen_TestLastPumpInputDecision() noexcept {
    return g_testLastPumpInputDecision;
}

void Maechen_TestSuppressNativeAllocation(bool suppress) noexcept {
    g_testSuppressNativeAllocation = suppress;
}

#endif

} // namespace FfxHooks
