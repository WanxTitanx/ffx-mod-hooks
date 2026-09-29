#include "../../BattlePhotoMode/PhotoModeCore.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace P = FfxHooks::Photo;
namespace {
int checks = 0;
int failures = 0;
void Check(bool result, const char* message) {
    ++checks;
    if (!result) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", message);
    }
}
struct Fixture {
    P::Frame frame{1, 0x1000, 9};
    std::array<P::Actor, 3> actors{{
        {{0, 0x2000, 3}, {1, 2, 3, 0, 1, 2, 3}},
        {{1, 0x3000, 4}, {4, 5, 6, 0, 4, 5, 6}},
        {{2, 0x4000, 0x1001}, {7, 8, 9, 0, 7, 8, 9}}
    }};
    P::Camera camera{10, 20, 30};
    unsigned writes = 0;
    unsigned captures = 0;
    bool valid = true;
    bool failWrite = false;

    static bool Capture(void* context, P::Capture& out) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        ++f.captures;
        if (!f.valid) return false;
        out = {};
        out.frame = f.frame;
        out.count = f.actors.size();
        out.camera = f.camera;
        for (std::size_t i = 0; i < out.count; ++i) out.actors[i] = f.actors[i];
        return true;
    }
    static bool ReadActor(void* context, const P::Frame& frame,
                          const P::Identity& identity, P::Pose& out) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        if (!f.valid || !(frame == f.frame)) return false;
        for (const auto& actor : f.actors) {
            if (actor.id == identity) { out = actor.pose; return true; }
        }
        return false;
    }
    static bool WriteActor(void* context, const P::Frame& frame, const P::Identity& identity,
                           const P::Pose& expected, const P::Pose& desired) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        P::Pose current{};
        if (f.failWrite || !ReadActor(context, frame, identity, current) || !(expected == current)) return false;
        for (auto& actor : f.actors) {
            if (actor.id == identity) { actor.pose = desired; ++f.writes; return true; }
        }
        return false;
    }
    static bool ReadCamera(void* context, const P::Frame& frame, P::Camera& out) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        if (!f.valid || !(frame == f.frame)) return false;
        out = f.camera;
        return true;
    }
    static bool WriteCamera(void* context, const P::Frame& frame,
                            const P::Camera& expected, const P::Camera& desired) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        P::Camera current{};
        if (f.failWrite || !ReadCamera(context, frame, current) || !(expected == current)) return false;
        f.camera = desired;
        ++f.writes;
        return true;
    }
    P::Io Io() noexcept { return {this, Capture, ReadActor, WriteActor, ReadCamera, WriteCamera}; }
};
void UneditedObservation() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io), "unedited observation: begin");
    f.actors[0].pose.x = f.actors[0].pose.rx = 111;
    f.actors[2].pose.z = f.actors[2].pose.rz = 333;
    f.camera.x = 222;
    Check(s.Tick(io), "unedited observation: tick");
    Check(f.writes == 0, "observing an unedited scene never writes");
    Check(s.SelectedPose().x == 111, "selected pose reflects an unedited actor's current position");
    Check(s.CurrentCamera().x == 222, "camera display reflects the current engine target");
    std::string json;
    Check(s.Export(json), "unedited observation: export");
    Check(json.find("\"x\":111") != std::string::npos, "export refreshes unedited actor positions");
    Check(json.find("\"z\":333") != std::string::npos, "export refreshes nonselected actors");
    Check(json.find("\"refX\":222") != std::string::npos, "export refreshes an unedited camera");
}
void HoldDisabledObservation() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0) && s.Pan(io, 1, 0, 0), "hold off: prepare edits");
    s.SetHolding(false);
    f.actors[0].pose.x = f.actors[0].pose.rx = 444;
    f.camera.y = 555;
    const auto before = f.writes;
    Check(s.Tick(io), "hold off: observe engine drift");
    Check(f.writes == before, "hold off never reasserts a pose");
    std::string json;
    Check(s.Export(json), "hold off: export");
    Check(json.find("\"x\":444") != std::string::npos, "hold off exports the observed pose, not the old hold target");
    Check(json.find("\"refY\":555") != std::string::npos, "hold off exports observed camera drift");
    Check(!s.Reset(io), "hold off cannot claim restoration after foreign drift");
    Check(f.actors[0].pose.x == 444 && f.camera.y == 555 && f.writes == before, "restore preserves foreign actor and camera changes");
}
void RestartRestorationBarrier() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0), "restart barrier: prepare edit");
    f.actors[0].pose.x = 999;
    const auto before = f.writes;
    const auto captures = f.captures;
    Check(!s.Begin(io), "restart must surface incomplete restoration rather than silently recapture");
    Check(!s.Active(), "failed restoration closes the old session");
    Check(s.LastStatus() == P::Status::WriteConflict, "restart preserves restoration-conflict status");
    Check(f.captures == captures && f.writes == before, "restart performs no recapture or unowned write after a conflict");
    Check(!s.End(io), "repeated end preserves incomplete-restoration result");
    Check(s.LastStatus() == P::Status::WriteConflict, "repeated end cannot erase the conflict");
    Check(s.Begin(io), "a later explicit begin may capture the preserved scene");
}
void CancelledEnd() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0), "cancelled end: prepare edit");
    ++f.frame.generation;
    const auto before = f.writes;
    Check(!s.Tick(io) && !s.Active(), "generation change cancels the session");
    Check(!s.End(io), "end cannot report successful restoration for a cancelled scene");
    Check(!s.End(io), "cancellation result remains idempotently unsuccessful");
    Check(s.LastStatus() == P::Status::IdentityChanged, "end preserves the reason for cancellation");
    Check(f.writes == before, "cancelled end never dereferences stale storage for writes");
}
void FailedReadClosesAdmission() {
    for (int operation = 0; operation < 3; ++operation) {
        Fixture f; P::Session s; const auto io = f.Io();
        Check(s.Begin(io), "failed read: begin");
        if (operation < 2) ++f.actors[0].id.value; else ++f.frame.generation;
        const bool result = operation == 0 ? s.Move(io, 1, 0, 0) :
                            operation == 1 ? s.Rotate(io, 1) : s.Pan(io, 1, 0, 0);
        Check(!result && !s.Active(), "failed move/rotate/pan identity read closes admission");
        Check(s.LastStatus() == P::Status::IdentityChanged, "failed read keeps identity-change diagnosis");
        Check(f.writes == 0, "failed read performs no write");
    }
}
void TickPreflight() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0), "tick preflight: prepare first actor");
    f.actors[0].pose.x = 123;
    ++f.actors[2].id.value;
    const auto before = f.writes;
    Check(!s.Tick(io) && !s.Active(), "tick validates unedited identities before holding any actor");
    Check(f.writes == before && f.actors[0].pose.x == 123, "tick performs no partial hold when another captured actor is stale");
    Check(s.LastStatus() == P::Status::IdentityChanged, "tick preflight reports identity change");
}
void ResetPreflight() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0), "reset preflight: first edit");
    s.Select(1);
    Check(s.Move(io, 1, 0, 0), "reset preflight: second edit");
    ++f.actors[1].id.value;
    const auto before = f.writes;
    Check(!s.Reset(io) && !s.Active(), "reset cancels on captured-identity mismatch");
    Check(f.writes == before, "reset validates all captured identities before restoring the first actor");
    Check(s.LastStatus() == P::Status::IdentityChanged, "reset distinguishes stale identity from value drift");
}
void ExtremeSelection() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io), "extreme selection: begin");
    s.Select(2);
    s.Select(INT_MAX);
    Check(s.Selected() == 0, "selection normalizes large positive deltas before adding");
    s.Select(INT_MIN);
    Check(s.Selected() == 1, "selection normalizes large negative deltas without signed overflow");
}
void NoOpOwnership() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io), "no-op ownership: begin");
    Check(s.Move(io, 0, 0, 0) && s.Rotate(io, 0) && s.Pan(io, 0, 0, 0), "finite no-op edits succeed");
    Check(f.writes == 0, "no-op edits do not claim ownership or issue writes");
    f.actors[0].pose.x = 888;
    f.camera.z = 777;
    Check(s.End(io), "no-op edits do not create restoration conflicts");
    Check(f.writes == 0 && f.actors[0].pose.x == 888 && f.camera.z == 777, "no-op end preserves engine changes");
}
void MissingIo() {
    Fixture f; P::Session s; const auto io = f.Io();
    Check(s.Begin(io) && s.Move(io, 1, 0, 0), "missing io: prepare edit");
    Check(!s.Begin({}), "invalid replacement I/O is rejected before attempting restoration");
    Check(s.Active(), "invalid replacement I/O preserves the recoverable session");
    Check(s.End(io) && f.actors[0].pose.x == 1, "the original I/O can still restore the session");
}
struct Case { const char* name; void (*run)(); };
const Case cases[] = {
    {"unedited-observation", UneditedObservation}, {"hold-disabled-observation", HoldDisabledObservation},
    {"restart-restoration-barrier", RestartRestorationBarrier}, {"cancelled-end", CancelledEnd},
    {"failed-read-closes", FailedReadClosesAdmission}, {"tick-preflight", TickPreflight},
    {"reset-preflight", ResetPreflight}, {"extreme-selection", ExtremeSelection},
    {"no-op-ownership", NoOpOwnership}, {"missing-io", MissingIo}
};
}
int main(int argc, char** argv) {
    bool selected = argc == 1;
    for (const auto& test : cases) {
        if (argc == 1 || std::strcmp(argv[1], test.name) == 0) {
            selected = true;
            test.run();
        }
    }
    if (!selected) return 2;
    std::printf("RecoveryPhotoModeSafetyRt0: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
