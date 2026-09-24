#pragma once

#include "MaechenCore.h"

#include <cstdint>

namespace FfxHooks {

using MaechenLogFn = void (*)(const char*);

enum class MaechenAdapterResult : uint8_t {
    None = 0,
    Success,
    InvalidQuestion,
    RateLimited,
    Cancelled,
    Timeout,
    RetryLater,
};

struct MaechenCompletion {
    uint32_t generation = 0;
    MaechenAdapterResult result = MaechenAdapterResult::None;
    Maechen::Pages pages{};
};

bool Maechen_Install(MaechenLogFn log);
bool Maechen_SubmitRequest(const char* locale, const char* question,
                           uint32_t generation,
                           MaechenAdapterResult* rejectionOut = nullptr);
void Maechen_CancelRequest() noexcept;
bool Maechen_TakeCompletion(MaechenCompletion* completionOut) noexcept;
bool Maechen_RequestBusy() noexcept;
void Maechen_NotifyForegroundLost() noexcept;
void Maechen_PresentTick(bool gameMenuOpen, bool otherCustomMenuOpen,
                         bool ffxForeground);
void Maechen_PumpTick(bool ffxForeground);
bool Maechen_MenuOwned() noexcept;
bool Maechen_PumpWakePending() noexcept;
bool Maechen_BlocksNativeModalAllocation() noexcept;
bool Maechen_IsActive() noexcept;

long NativeMenu_ReserveAndConsumeOpenRequest(volatile long* request,
                                             long emptyValue,
                                             volatile long* ownerPublished) noexcept;

#if defined(FFXHOOKS_TESTING)

#if defined(_MSC_VER)
#define FFXHOOKS_MAECHEN_TEST_CALL __stdcall
#else
#define FFXHOOKS_MAECHEN_TEST_CALL
#endif

using MaechenTestHandle = uintptr_t;
using MaechenTestWorkerProc = unsigned long (FFXHOOKS_MAECHEN_TEST_CALL*)(void*);
using MaechenTestNativeReservationPause = void (*)(void*);

enum class MaechenTestHeader : uint8_t {
    ContentType = 0,
    Protocol,
    ContentEncoding,
};

// The executable RT0 seam mirrors effects instead of accepting request policy. In
// particular, tests can observe the compiled endpoint but cannot replace it.
struct MaechenTestOps {
    void* context = nullptr;

    MaechenTestHandle (FFXHOOKS_MAECHEN_TEST_CALL *createCancelEvent)(
        void*, bool, bool) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *signalCancelEvent)(void*, MaechenTestHandle) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *isCancelSignaled)(void*, MaechenTestHandle) = nullptr;
    MaechenTestHandle (FFXHOOKS_MAECHEN_TEST_CALL *createWorker)(
        void*, MaechenTestWorkerProc, void*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *isWorkerComplete)(void*, MaechenTestHandle) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *closeWorker)(void*, MaechenTestHandle) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *closeCancelEvent)(void*, MaechenTestHandle) = nullptr;

    MaechenTestHandle (FFXHOOKS_MAECHEN_TEST_CALL *openSession)(
        void*, const wchar_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *setTimeouts)(
        void*, MaechenTestHandle, int, int, int, int) = nullptr;
    MaechenTestHandle (FFXHOOKS_MAECHEN_TEST_CALL *connect)(
        void*, MaechenTestHandle, const wchar_t*, uint16_t) = nullptr;
    MaechenTestHandle (FFXHOOKS_MAECHEN_TEST_CALL *openRequest)(
        void*, MaechenTestHandle, const wchar_t*, const wchar_t*, uint32_t) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *disableFeatures)(
        void*, MaechenTestHandle, uint32_t) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *sendRequest)(
        void*, MaechenTestHandle, const wchar_t*, uint32_t, const uint8_t*, uint32_t,
        uint32_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *receiveResponse)(
        void*, MaechenTestHandle, uint32_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *queryStatus)(
        void*, MaechenTestHandle, uint32_t*, uint32_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *queryHeader)(
        void*, MaechenTestHandle, MaechenTestHeader, char*, uint32_t, uint32_t*, bool*,
        uint32_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *readData)(
        void*, MaechenTestHandle, uint8_t*, uint32_t, uint32_t*, uint32_t*) = nullptr;
    bool (FFXHOOKS_MAECHEN_TEST_CALL *closeTransport)(
        void*, MaechenTestHandle) = nullptr;
};

bool Maechen_TestInstallTransport(const MaechenTestOps* operations) noexcept;
bool Maechen_TestTryPublishCompletion(uint32_t generation,
                                      MaechenAdapterResult result,
                                      const Maechen::Pages* pages) noexcept;
MaechenTestHandle Maechen_TestCancelIdentity() noexcept;
void Maechen_TestSetNativeReservationPause(
    MaechenTestNativeReservationPause pause, void* context) noexcept;
bool Maechen_TestPublishMenuOwnedForClose() noexcept;
void Maechen_TestCloseMenuFromPump() noexcept;
void Maechen_TestPrepareRequestingState(uint32_t generation) noexcept;
bool Maechen_TestApplyCompletionToUi(MaechenAdapterResult result,
                                     uint32_t generation) noexcept;
bool Maechen_TestApplySubmitFailureToUi(MaechenAdapterResult result) noexcept;
bool Maechen_TestCopyPublishedStatus(unsigned char* output,
                                     size_t capacity) noexcept;
void Maechen_TestPresentInput(bool ffxForeground, bool plainF9Down,
                              bool gameMenuOpen,
                              bool otherCustomMenuOpen) noexcept;
Maechen::Phase Maechen_TestUiPhase() noexcept;
Maechen::ForegroundInputDecision Maechen_TestLastPumpInputDecision() noexcept;
void Maechen_TestSuppressNativeAllocation(bool suppress) noexcept;

#endif

} // namespace FfxHooks
