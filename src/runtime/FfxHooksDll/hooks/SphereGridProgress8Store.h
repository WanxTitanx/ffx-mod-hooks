#pragma once
#include "SphereGridProgress8Core.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>

namespace FfxHooks::SphereGridProgress8Disk {
using Key=SphereGridProgress8::Key;
using Snapshot=SphereGridProgress8::Snapshot;
using Bytes=SphereGridProgress8::Bytes;
enum class ReadResult {Missing,Found,Invalid,Unavailable};
enum class WriteResult {Written,Unchanged,Rejected,Unavailable,PublishedRetired,PublishedUnverified};
struct Admission {
    void* context=nullptr;
    // The owner checks the verified-save ticket, stop state, session and thread.
    // This disk layer cannot infer native-save completion from a path or hash.
    bool (*current)(void*,const Key&)=nullptr;
};

// One owner per configured root. Initialization occurs in normal context only.
// This class never reads or writes a native ffx_### save. All generated leaves
// belong to the separately versioned companion format and contain the full key.
class Store {
public:
    Store() noexcept=default;
    ~Store();
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    bool Initialize(const std::wstring& directory,bool create) noexcept;
    std::wstring RecordPath(const Key&) const;
    ReadResult Read(const Key&,Snapshot&) noexcept;
    WriteResult Publish(const Key&,const Snapshot&,const Admission&) noexcept;
private:
    bool Healthy() const noexcept;
    ReadResult ReadRaw(const std::wstring&,Bytes&) const noexcept;
    static bool Admitted(const Admission&,const Key&) noexcept;
    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    std::wstring directory_;
    HANDLE directoryHandle_=INVALID_HANDLE_VALUE;
    HANDLE writerHandle_=INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION identity_{};
};
}
