#include "../../BattlePhotoMode/PhotoModeCore.h"
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace P = FfxHooks::Photo;
namespace {
int checks = 0, failed = 0;
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failed; std::fprintf(stderr, "FAIL %s\n", label); }
}
template<class S, class = void> struct FreshExport {
    static bool Run(S& session, const P::Io&, std::string& json) { return session.Export(json); }
};
template<class S> struct FreshExport<S, std::void_t<decltype(std::declval<S&>().Export(
    std::declval<const P::Io&>(), std::declval<std::string&>()))>> {
    static bool Run(S& session, const P::Io& io, std::string& json) { return session.Export(io, json); }
};
template<class C, class = void> struct Omission {
    static void Set(C&, std::size_t) {}
};
template<class C> struct Omission<C, std::void_t<decltype(std::declval<C&>().omitted)>> {
    static void Set(C& capture, std::size_t count) { capture.omitted = count; }
};
struct Fixture {
    P::Frame frame{1, 0x1000, 9};
    P::Actor actor{{0, 0x2000, 3}, {1, 2, 3, 0, 1, 2, 3}};
    P::Camera camera{10, 20, 30};
    unsigned writes = 0;
    std::size_t omitted = 0;
    bool transitionAfterActorRead = false;
    static bool Capture(void* context, P::Capture& output) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        output = {}; output.frame = f.frame; output.count = 1;
        output.actors[0] = f.actor; output.camera = f.camera;
        Omission<P::Capture>::Set(output, f.omitted);
        return true;
    }
    static bool ReadActor(void* context, const P::Frame& frame, const P::Identity& id, P::Pose& out) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        if (!(frame == f.frame) || !(id == f.actor.id)) return false;
        out = f.actor.pose;
        if (f.transitionAfterActorRead) ++f.frame.generation;
        return true;
    }
    static bool WriteActor(void* context, const P::Frame& frame, const P::Identity& id,
                           const P::Pose& expected, const P::Pose& desired) noexcept {
        auto& f = *static_cast<Fixture*>(context); P::Pose value{};
        if (!ReadActor(context, frame, id, value) || !(value == expected)) return false;
        ++f.writes; f.actor.pose = desired; return true;
    }
    static bool ReadCamera(void* context, const P::Frame& frame, P::Camera& out) noexcept {
        auto& f = *static_cast<Fixture*>(context);
        if (!(frame == f.frame)) return false;
        out = f.camera; return true;
    }
    static bool WriteCamera(void* context, const P::Frame& frame, const P::Camera& expected,
                            const P::Camera& desired) noexcept {
        auto& f = *static_cast<Fixture*>(context); P::Camera value{};
        if (!ReadCamera(context, frame, value) || !(value == expected)) return false;
        ++f.writes; f.camera = desired; return true;
    }
    P::Io Io() { return {this, Capture, ReadActor, WriteActor, ReadCamera, WriteCamera}; }
};
}
int main() {
    Fixture f; P::Session s; const auto io = f.Io(); std::string json;
    Check(s.Begin(io) && s.Move(io, 1, 0, 0) && s.Pan(io, 1, 0, 0), "prepare held scene");
    f.actor.pose.x = f.actor.pose.rx = 901; f.camera.x = 902;
    const auto before = f.writes;
    Check(FreshExport<P::Session>::Run(s, io, json), "fresh export succeeds");
    Check(json.find("\"x\":901") != std::string::npos && json.find("\"refX\":902") != std::string::npos,
          "fresh export samples current actor and camera without needing Tick");
    Check(f.writes == before && f.actor.pose.x == 901 && f.camera.x == 902,
          "export never reasserts held poses or changes the scene");
    Check(json.find("\"truncated\":false") != std::string::npos &&
          json.find("\"omittedActors\":0") != std::string::npos, "complete capture declares zero omissions");
    auto readOnly = io; readOnly.writeActor = nullptr; readOnly.writeCamera = nullptr;
    f.actor.pose.y = 903;
    Check(FreshExport<P::Session>::Run(s, readOnly, json) && json.find("\"y\":903") != std::string::npos,
          "fresh export requires readers, not writers");
    auto missing = io; missing.readActor = nullptr;
    json = "old export";
    Check(!FreshExport<P::Session>::Run(s, missing, json) && json.empty(), "missing reader refuses export and clears old output");
    json = "old export"; f.transitionAfterActorRead = true;
    Check(!FreshExport<P::Session>::Run(s, io, json) && json.empty(), "transition after final actor read rejects a torn export");
    Check(!s.Active() && s.LastStatus() == P::Status::IdentityChanged, "export closes admission on a scene transition");
    Check(f.writes == before, "failed export never writes");
    f.transitionAfterActorRead = false; f.omitted = 7;
    Check(s.Begin(io), "truncated capture can start with an explicit bound");
    Check(FreshExport<P::Session>::Run(s, io, json) && json.find("\"truncated\":true") != std::string::npos &&
          json.find("\"omittedActors\":7") != std::string::npos, "export reports eligible actors beyond the capture limit");
    Check(s.End(io), "unedited truncated session restores cleanly");
    f.omitted = std::numeric_limits<std::size_t>::max();
    Check(!s.Begin(io), "overflowing omission metadata is rejected");
    std::printf("RecoveryPhotoExportRt0: %d/%d passed\n", checks-failed, checks);
    return failed ? 1 : 0;
}
