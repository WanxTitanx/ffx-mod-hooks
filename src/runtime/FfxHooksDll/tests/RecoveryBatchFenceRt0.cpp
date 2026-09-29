// Execute the real coordinator with bounded fake MinHook I/O, never a game process.
#include "../hooks/MinHookBatchCoordinator.h"
#include <array>
#include <cstdio>
#include <map>

using namespace FfxHooks::MinHookBatch;
namespace {
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
constexpr std::array<uintptr_t, 3> targets{0x1100, 0x2200, 0x3300};
constexpr std::array<Owner, 4> owners{Owner::NulWardRecovery, Owner::GridTeachSave,
    Owner::GridTeachRecovery, Owner::SphereGridRecovery};
struct FakeIo {
    unsigned enables = 0, disables = 0, applies = 0, exact = 0;
    unsigned failEnable = 0, failApply = 0;
    std::map<uintptr_t, bool> queued, active;
    static bool Init(void*) { return true; }
    static bool Enable(void* context, uintptr_t target) {
        auto& self = *static_cast<FakeIo*>(context);
        if (++self.enables == self.failEnable) return false;
        self.queued[target] = true; return true;
    }
    static bool Disable(void* context, uintptr_t target) {
        auto& self = *static_cast<FakeIo*>(context);
        ++self.disables; self.queued[target] = false; return true;
    }
    static bool Apply(void* context) {
        auto& self = *static_cast<FakeIo*>(context);
        ++self.applies;
        for (const auto& entry : self.queued) self.active[entry.first] = entry.second;
        self.queued.clear();
        return self.applies != self.failApply;
    }
    static bool Exact(void* context, uintptr_t target) {
        auto& self = *static_cast<FakeIo*>(context);
        ++self.exact; self.active[target] = false; return true;
    }
    BatchIo Io() { return {this, Enable, Disable, Apply, Exact}; }
    unsigned Calls() const { return enables + disables + applies + exact; }
    bool Inert() const {
        for (const auto& entry : active) if (entry.second) return false;
        return true;
    }
};
struct Fence {
    Coordinator& coordinator;
    FakeIo& io;
    bool allowPre = true, allowPost = true, reenter = false;
    unsigned pre = 0, post = 0;
    bool preBeforeDisables = false, postAfterDisables = false;
    void Reenter() {
        if (!reenter) return;
        const auto before = io.Calls();
        Check(EnsureInitialized(&coordinator, {&io, FakeIo::Init}) == InitializationResult::Busy,
            "reentrant initialization returns Busy");
        Check(EnableBatch(&coordinator, io.Io(), Owner::ElementScan, targets.data(), targets.size()).result
            == BatchResult::Busy, "reentrant enable returns Busy");
        Check(NeutralizeBatch(&coordinator, io.Io(), Owner::ElementScan, targets.data(), targets.size()).result
            == BatchResult::Busy, "reentrant stop returns Busy");
        Check(io.Calls() == before, "reentrant operations never enter MinHook I/O");
    }
    static bool Pre(void* context) {
        auto& self = *static_cast<Fence*>(context);
        ++self.pre;
        self.preBeforeDisables = self.io.disables == 0 && self.io.exact == 0;
        self.Reenter(); return self.allowPre;
    }
    static bool Post(void* context) {
        auto& self = *static_cast<Fence*>(context);
        ++self.post;
        self.postAfterDisables = self.pre == 1 && self.io.disables == targets.size() &&
            self.io.exact == targets.size() && self.io.Inert();
        self.Reenter(); return self.allowPost;
    }
    NeutralizationFence Io() { return {this, Pre, Post}; }
};
void Initialize(Coordinator& coordinator, FakeIo& io) {
    Check(EnsureInitialized(&coordinator, {&io, FakeIo::Init}) == InitializationResult::Ready,
        "fixture initializes the real coordinator");
}
void CheckPoison(Coordinator& coordinator, FakeIo& io, Fence& fence, Owner owner) {
    const auto snapshot = GetSnapshot(coordinator);
    Check(snapshot.state == State::Poisoned && snapshot.owner == owner &&
        snapshot.targetCount == targets.size(), "incomplete cleanup retains owner and targets");
    const auto calls = io.Calls(), pre = fence.pre, post = fence.post;
    Check(EnableBatch(&coordinator, io.Io(), Owner::ElementScan, targets.data(), targets.size(),
        fence.Io()).result == BatchResult::Poisoned, "another owner cannot reuse poisoned queue");
    Check(NeutralizeBatch(&coordinator, io.Io(), owner, targets.data(), targets.size(),
        fence.Io()).result == BatchResult::Poisoned, "repeated poisoned stop stays closed");
    Check(io.Calls() == calls && fence.pre == pre && fence.post == post,
        "poisoned retry invokes neither I/O nor lifecycle fences");
}
void TestStopFences() {
    for (const auto owner : owners) for (unsigned mode = 0; mode != 3; ++mode) {
        Coordinator coordinator; FakeIo io; Initialize(coordinator, io);
        Check(EnableBatch(&coordinator, io.Io(), owner, targets.data(), targets.size()).result
            == BatchResult::Applied && !io.Inert(), "stop begins with actually active fake targets");
        Fence fence{coordinator, io};
        fence.allowPre = mode != 1; fence.allowPost = mode != 2; fence.reenter = true;
        const auto calls = io.Calls();
        const auto result = NeutralizeBatch(&coordinator, io.Io(), owner, targets.data(),
            targets.size(), fence.Io());
        Check(fence.pre == 1 && fence.preBeforeDisables, "pre-fence runs once before any disable");
        if (mode == 1) {
            Check(result.result == BatchResult::Poisoned && !result.neutralized && !result.exactDisabled,
                "failed admission drain cannot report safe stop");
            Check(result.neutralizationFailure == FailureStage::PreNeutralizeFence,
                "pre-fence failure remains identifiable");
            Check(io.Calls() == calls && !io.Inert() && fence.post == 0,
                "failed pre-fence leaves code reachable without unsafe patch I/O");
            CheckPoison(coordinator, io, fence, owner);
        } else {
            Check(fence.post == 1 && fence.postAfterDisables, "post-fence runs only after exact disables");
            Check(result.exactDisabled && io.Inert(), "targets are inert before post-drain result");
            if (mode == 2) {
                Check(result.result == BatchResult::Poisoned && !result.neutralized,
                    "exact disable cannot substitute for draining borrowed callbacks");
                Check(result.neutralizationFailure == FailureStage::PostNeutralizeFence,
                    "post-fence failure remains identifiable");
                CheckPoison(coordinator, io, fence, owner);
            } else {
                const auto snapshot = GetSnapshot(coordinator);
                Check(result.result == BatchResult::Neutralized && result.neutralized &&
                    result.neutralizationFailure == FailureStage::None, "both fences permit complete stop");
                Check(snapshot.state == State::Idle && snapshot.owner == Owner::None &&
                    snapshot.targetCount == 0, "complete stop releases coordinator ownership");
            }
        }
    }
}
void TestCompensationFences() {
    for (const auto owner : owners) for (unsigned failedApply = 0; failedApply != 2; ++failedApply)
        for (unsigned mode = 0; mode != 3; ++mode) {
            Coordinator coordinator; FakeIo io; Initialize(coordinator, io);
            if (failedApply) io.failApply = 1; else io.failEnable = 2;
            Fence fence{coordinator, io}; fence.allowPre = mode != 1; fence.allowPost = mode != 2;
            const auto result = EnableBatch(&coordinator, io.Io(), owner, targets.data(),
                targets.size(), fence.Io());
            Check(result.primaryFailure == (failedApply ? FailureStage::ApplyEnable : FailureStage::QueueEnable),
                "compensation preserves the publication failure");
            Check(fence.pre == 1 && fence.preBeforeDisables, "compensation honors the pre-fence");
            if (mode == 1) {
                Check(fence.post == 0 && io.disables == 0 && io.exact == 0,
                    "failed compensation pre-fence forbids patch cleanup");
                Check(!result.neutralized && !result.exactDisabled &&
                    result.neutralizationFailure == FailureStage::PreNeutralizeFence,
                    "failed compensation drain stays incomplete");
            } else {
                Check(fence.post == 1 && fence.postAfterDisables && result.exactDisabled,
                    "compensation post-fence follows exact disables");
                Check(result.neutralized == (mode == 0), "compensation requires post-drain success");
            }
            if (failedApply || mode != 0) {
                Check(result.result == BatchResult::Poisoned, "unsafe publication or cleanup remains poisoned");
                CheckPoison(coordinator, io, fence, owner);
            } else {
                Check(result.result == BatchResult::EnableFailedNeutralized && io.Inert(),
                    "fully compensated preparation failure is not installation success");
            }
        }
}
} // namespace
int main() {
    TestStopFences(); TestCompensationFences();
    std::printf("RecoveryBatchFenceRt0: %u/%u passed (real coordinator, simulated MinHook)\n",
        checks - failures, checks);
    return failures ? 1 : 0;
}
