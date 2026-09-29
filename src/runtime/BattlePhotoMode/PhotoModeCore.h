#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

namespace FfxHooks::Photo {
inline constexpr std::size_t kMaximumActors = 64;
inline constexpr std::size_t kMaximumJson = 32768;

struct Frame {
    std::uint64_t generation = 0;
    std::uintptr_t table = 0;
    std::uint32_t thread = 0;
    bool operator==(const Frame& other) const noexcept {
        return generation == other.generation && table == other.table && thread == other.thread;
    }
};
struct Identity {
    unsigned index = 0;
    std::uintptr_t pointer = 0;
    std::uint16_t value = 0;
    bool operator==(const Identity& other) const noexcept {
        return index == other.index && pointer == other.pointer && value == other.value;
    }
};
struct Pose {
    float x = 0, y = 0, z = 0, yaw = 0, rx = 0, ry = 0, rz = 0;
    bool operator==(const Pose& other) const noexcept {
        return x == other.x && y == other.y && z == other.z && yaw == other.yaw &&
               rx == other.rx && ry == other.ry && rz == other.rz;
    }
};
struct Camera {
    float x = 0, y = 0, z = 0;
    bool operator==(const Camera& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};
inline bool Scalar(float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= 100000.0f;
}
inline bool Valid(const Pose& pose) noexcept {
    return Scalar(pose.x) && Scalar(pose.y) && Scalar(pose.z) && Scalar(pose.yaw) &&
           Scalar(pose.rx) && Scalar(pose.ry) && Scalar(pose.rz);
}
inline bool Valid(const Camera& camera) noexcept {
    return Scalar(camera.x) && Scalar(camera.y) && Scalar(camera.z);
}
struct Actor { Identity id{}; Pose pose{}; };
struct Capture {
    Frame frame{};
    std::array<Actor, kMaximumActors> actors{};
    std::size_t count = 0;
    Camera camera{};
    std::size_t omitted = 0; // Eligible actors beyond kMaximumActors.
};
struct Io {
    void* context = nullptr;
    bool (*capture)(void*, Capture&) noexcept = nullptr;
    bool (*readActor)(void*, const Frame&, const Identity&, Pose&) noexcept = nullptr;
    bool (*writeActor)(void*, const Frame&, const Identity&, const Pose&, const Pose&) noexcept = nullptr;
    bool (*readCamera)(void*, const Frame&, Camera&) noexcept = nullptr;
    bool (*writeCamera)(void*, const Frame&, const Camera&, const Camera&) noexcept = nullptr;
};
enum class Status { Off, Ready, Unavailable, IdentityChanged, WriteConflict, InvalidValue, ExportFailure };
inline const char* StatusText(Status status) noexcept {
    switch (status) {
    case Status::Off: return "Photo Mode is off";
    case Status::Ready: return "Photo Mode ready";
    case Status::Unavailable: return "A supported active battle is required";
    case Status::IdentityChanged: return "Scene or actor changed; stale writes cancelled";
    case Status::WriteConflict: return "A changed value was preserved; restoration is incomplete";
    case Status::InvalidValue: return "Invalid or out-of-range position";
    default: return "Unable to export this scene";
    }
}

class Session {
public:
    bool Active() const noexcept { return active_; }
    std::size_t Count() const noexcept { return count_; }
    std::size_t Omitted() const noexcept { return omitted_; }
    std::size_t Selected() const noexcept { return selected_; }
    Status LastStatus() const noexcept { return status_; }
    bool Holding() const noexcept { return hold_; }
    void SetHolding(bool value) noexcept { hold_ = value; }
    Identity SelectedIdentity() const noexcept { return count_ ? actors_[selected_].id : Identity{}; }
    Pose SelectedPose() const noexcept { return count_ ? actors_[selected_].observed : Pose{}; }
    Camera CurrentCamera() const noexcept { return cameraObserved_; }

    void Select(int delta) noexcept {
        if (!active_ || !count_) return;
        const auto count = static_cast<int>(count_);
        // Reduce before addition: callers may pass any int, including INT_MAX.
        int next = (static_cast<int>(selected_) + delta % count) % count;
        if (next < 0) next += count;
        selected_ = static_cast<std::size_t>(next);
    }

    bool Begin(const Io& io) noexcept {
        // Do not attempt to restore an existing session through missing callbacks.
        if (!io.capture || !Writable(io)) {
            status_ = Status::Unavailable;
            return false;
        }
        if (active_ && !End(io)) return false;
        active_ = false;
        count_ = selected_ = omitted_ = 0;
        cameraDirty_ = false;
        actors_ = {};
        status_ = Status::Unavailable;
        Capture capture{};
        if (!io.capture(io.context, capture) || !capture.frame.generation ||
            !capture.frame.table || !capture.frame.thread || !capture.count ||
            capture.count > kMaximumActors || capture.omitted > 4096 - capture.count ||
            !Valid(capture.camera)) return false;
        for (std::size_t i = 0; i < capture.count; ++i) {
            const auto& actor = capture.actors[i];
            if (!actor.id.pointer || actor.id.index >= 4096 || !Valid(actor.pose)) return false;
            for (std::size_t j = 0; j < i; ++j) {
                if (capture.actors[j].id.pointer == actor.id.pointer ||
                    capture.actors[j].id.index == actor.id.index) return false;
            }
        }
        frame_ = capture.frame;
        count_ = capture.count;
        omitted_ = capture.omitted;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& actor = capture.actors[i];
            actors_[i] = {actor.id, actor.pose, actor.pose, actor.pose, actor.pose, false};
        }
        cameraOriginal_ = cameraDesired_ = cameraLast_ = cameraObserved_ = capture.camera;
        hold_ = active_ = lastEndSucceeded_ = true;
        status_ = Status::Ready;
        return true;
    }

    bool Move(const Io& io, float dx, float dy, float dz) noexcept {
        if (!active_ || !count_ || !RequireWritable(io)) return false;
        if (!Scalar(dx) || !Scalar(dy) || !Scalar(dz)) return InvalidValue();
        auto& actor = actors_[selected_];
        Pose current{};
        if (!io.readActor(io.context, frame_, actor.id, current) || !Valid(current)) return CancelIdentity();
        auto desired = current;
        desired.x += dx; desired.y += dy; desired.z += dz;
        desired.rx += dx; desired.ry += dy; desired.rz += dz;
        return Update(io, actor, current, desired);
    }

    bool Rotate(const Io& io, float delta) noexcept {
        if (!active_ || !count_ || !RequireWritable(io)) return false;
        if (!Scalar(delta)) return InvalidValue();
        auto& actor = actors_[selected_];
        Pose current{};
        if (!io.readActor(io.context, frame_, actor.id, current) || !Valid(current)) return CancelIdentity();
        auto desired = current;
        if (delta != 0) desired.yaw = std::remainder(current.yaw + delta, 6.2831853071795864769f);
        return Update(io, actor, current, desired);
    }

    bool Pan(const Io& io, float dx, float dy, float dz) noexcept {
        if (!active_ || !RequireWritable(io)) return false;
        if (!Scalar(dx) || !Scalar(dy) || !Scalar(dz)) return InvalidValue();
        Camera current{};
        if (!io.readCamera(io.context, frame_, current) || !Valid(current)) return CancelIdentity();
        const Camera desired{current.x + dx, current.y + dy, current.z + dz};
        if (!Valid(desired)) return InvalidValue();
        cameraObserved_ = current;
        if (!(current == desired)) {
            if (!io.writeCamera(io.context, frame_, current, desired)) return WriteConflict();
            cameraDesired_ = cameraLast_ = cameraObserved_ = desired;
            cameraDirty_ = true;
        }
        status_ = Status::Ready;
        return true;
    }

    bool Tick(const Io& io) noexcept {
        if (!active_ || !RequireWritable(io)) return false;
        std::array<Pose, kMaximumActors> current{};
        Camera camera{};
        if (!ReadScene(io, current, camera)) return false;
        // Preflight includes unedited actors. An identity mismatch must not be
        // discovered only after reasserting another actor's hold target.
        for (std::size_t i = 0; i < count_; ++i) {
            auto& actor = actors_[i];
            if (hold_ && actor.dirty && !(current[i] == actor.desired)) {
                if (!io.writeActor(io.context, frame_, actor.id, current[i], actor.desired)) return WriteConflict();
                actor.last = actor.observed = actor.desired;
            }
        }
        if (hold_ && cameraDirty_ && !(camera == cameraDesired_)) {
            if (!io.writeCamera(io.context, frame_, camera, cameraDesired_)) return WriteConflict();
            cameraLast_ = cameraObserved_ = cameraDesired_;
        }
        status_ = Status::Ready;
        return true;
    }

    bool Reset(const Io& io) noexcept {
        if (!active_ || !RequireWritable(io)) return false;
        std::array<Pose, kMaximumActors> current{};
        Camera camera{};
        if (!ReadScene(io, current, camera)) return false;
        bool restored = true;
        for (std::size_t i = 0; i < count_; ++i) {
            auto& actor = actors_[i];
            if (!actor.dirty) continue;
            if (!(current[i] == actor.last) ||
                (!(current[i] == actor.original) &&
                 !io.writeActor(io.context, frame_, actor.id, current[i], actor.original))) {
                restored = false;
                continue;
            }
            actor.desired = actor.last = actor.observed = actor.original;
            actor.dirty = false;
        }
        if (cameraDirty_) {
            if (!(camera == cameraLast_) ||
                (!(camera == cameraOriginal_) &&
                 !io.writeCamera(io.context, frame_, camera, cameraOriginal_))) {
                restored = false;
            } else {
                cameraDesired_ = cameraLast_ = cameraObserved_ = cameraOriginal_;
                cameraDirty_ = false;
            }
        }
        status_ = restored ? Status::Ready : Status::WriteConflict;
        return restored;
    }

    bool End(const Io& io) noexcept {
        // A cancelled or incompletely restored session stays distinguishable
        // from a clean stop even when End is called repeatedly by teardown/UI.
        if (!active_) return lastEndSucceeded_;
        if (!RequireWritable(io)) return false;
        const bool restored = Reset(io);
        active_ = false;
        lastEndSucceeded_ = restored;
        if (restored) status_ = Status::Off;
        return restored;
    }

    // Export must not call Tick: reasserting held poses would mutate the scene
    // while performing a read-only operation. Readers validate this frame again.
    bool Export(const Io& io, std::string& json) {
        json.clear();
        if (!active_) return false;
        std::array<Pose, kMaximumActors> current{};
        Camera camera{};
        if (!ReadScene(io, current, camera)) return false;
        return Export(json);
    }

    // Serialize the last validated observation, never an old edit/hold target.
    bool Export(std::string& json) const {
        json.clear();
        if (!active_ || !count_ || count_ > kMaximumActors) return false;
        std::string output;
        output.reserve(256 + count_ * 192);
        output = "{\"schema\":\"ffx-hooks.photo-scene\",\"version\":1,\"camera\":{\"refX\":";
        if (!Float(output, cameraObserved_.x)) return false;
        output += ",\"refY\":";
        if (!Float(output, cameraObserved_.y)) return false;
        output += ",\"refZ\":";
        if (!Float(output, cameraObserved_.z)) return false;
        output += "},\"actors\":[";
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& actor = actors_[i];
            if (i) output += ',';
            output += "{\"id\":" + std::to_string(actor.id.value) + ",\"kind\":\"" +
                      (actor.id.value >= 0x1000 ? std::string("monster") : std::string("party")) + "\",\"x\":";
            if (!Float(output, actor.observed.x)) return false;
            output += ",\"y\":";
            if (!Float(output, actor.observed.y)) return false;
            output += ",\"z\":";
            if (!Float(output, actor.observed.z)) return false;
            output += ",\"yaw\":";
            if (!Float(output, actor.observed.yaw)) return false;
            output += '}';
            if (output.size() > kMaximumJson) return false;
        }
        output += "],\"omittedActors\":" + std::to_string(omitted_) +
                  ",\"truncated\":" + (omitted_ ? "true" : "false") + "}\n";
        if (output.size() > kMaximumJson) return false;
        json.swap(output);
        return true;
    }

private:
    struct OwnedActor {
        Identity id{};
        Pose original{}, desired{}, last{}, observed{};
        bool dirty = false;
    };
    static bool Readable(const Io& io) noexcept { return io.readActor && io.readCamera; }
    static bool Writable(const Io& io) noexcept {
        return Readable(io) && io.writeActor && io.writeCamera;
    }
    bool RequireWritable(const Io& io) noexcept {
        if (Writable(io)) return true;
        status_ = Status::Unavailable;
        return false;
    }
    bool InvalidValue() noexcept { status_ = Status::InvalidValue; return false; }
    bool WriteConflict() noexcept { status_ = Status::WriteConflict; return false; }
    bool CancelIdentity() noexcept {
        active_ = false;
        lastEndSucceeded_ = false;
        status_ = Status::IdentityChanged;
        return false;
    }
    bool ReadScene(const Io& io, std::array<Pose, kMaximumActors>& poses, Camera& camera) noexcept {
        if (!Readable(io)) {
            status_ = Status::Unavailable;
            return false;
        }
        if (!io.readCamera(io.context, frame_, camera) || !Valid(camera)) return CancelIdentity();
        for (std::size_t i = 0; i < count_; ++i) {
            if (!io.readActor(io.context, frame_, actors_[i].id, poses[i]) || !Valid(poses[i])) return CancelIdentity();
        }
        // Validate the frame after the final actor read as well; a transition
        // cannot turn a partially sampled scene into a successful export/reset.
        if (!io.readCamera(io.context, frame_, camera) || !Valid(camera)) return CancelIdentity();
        for (std::size_t i = 0; i < count_; ++i) actors_[i].observed = poses[i];
        cameraObserved_ = camera;
        return true;
    }
    bool Update(const Io& io, OwnedActor& actor, const Pose& current, const Pose& desired) noexcept {
        if (!Valid(desired)) return InvalidValue();
        actor.observed = current;
        if (!(current == desired)) {
            if (!io.writeActor(io.context, frame_, actor.id, current, desired)) return WriteConflict();
            actor.desired = actor.last = actor.observed = desired;
            actor.dirty = true;
        }
        status_ = Status::Ready;
        return true;
    }
    static bool Float(std::string& output, float value) {
        if (!Scalar(value)) return false;
        char buffer[48]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 9);
        if (result.ec != std::errc()) return false;
        output.append(buffer, result.ptr);
        return true;
    }
    Frame frame_{};
    std::array<OwnedActor, kMaximumActors> actors_{};
    std::size_t count_ = 0, selected_ = 0, omitted_ = 0;
    Camera cameraOriginal_{}, cameraDesired_{}, cameraLast_{}, cameraObserved_{};
    bool active_ = false, cameraDirty_ = false, hold_ = true, lastEndSucceeded_ = true;
    Status status_ = Status::Off;
};
} // namespace FfxHooks::Photo
