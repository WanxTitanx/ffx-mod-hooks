#pragma once
#include <atomic>
#include <cstddef>

namespace FfxHooks::NativeSaveEvents {
// Passive taps in the existing owned native save-I/O producer. The observer's
// storage/code must remain alive until process exit; unsubscribe is nonblocking.
struct Observer {
    void (*read)(const wchar_t* path,const unsigned char* disk,
                 const unsigned char* loaded,std::size_t size) noexcept=nullptr;
    void (*write)(const wchar_t* path,const unsigned char* actual,
                  std::size_t size) noexcept=nullptr;
    void (*reset)() noexcept=nullptr;
};
inline std::atomic<const Observer*> observer{nullptr};
inline bool Subscribe(const Observer* value) noexcept {
    if(!value || !value->read || !value->write)return false;
    const Observer* expected=nullptr;
    return observer.compare_exchange_strong(expected,value) || expected==value;
}
inline bool Requested() noexcept {return observer.load()!=nullptr;}
inline void Unsubscribe(const Observer* owner) noexcept {
    observer.compare_exchange_strong(owner,nullptr);
}
inline void ReadCompleted(const wchar_t* path,const unsigned char* disk,
                          const unsigned char* loaded,std::size_t size) noexcept {
    const auto* value=observer.load();if(value)value->read(path,disk,loaded,size);
}
inline void WriteCompleted(const wchar_t* path,const unsigned char* actual,
                           std::size_t size) noexcept {
    const auto* value=observer.load();if(value)value->write(path,actual,size);
}
inline void ResetCompleted() noexcept {const auto* value=observer.load();if(value && value->reset)value->reset();}
}
