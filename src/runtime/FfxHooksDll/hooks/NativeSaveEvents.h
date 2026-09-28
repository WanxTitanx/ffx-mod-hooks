#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace FfxHooks::NativeSaveEvents {
// The native I/O owner serializes Ronso's dormant charge before a private
// checkpoint is published, and restores that ownership before normal LoadPool.
// These four bytes are validated explicitly; no on-disk C++ bool is trusted.
struct CheckpointOwnership {
    std::uint8_t present=0,originalMax=0,charge=0,maximum=0;
};
struct CheckpointSelection {
    CheckpointOwnership pool{};
    std::array<unsigned char,32> proof{};
    bool selected=false;
};
using CheckpointSerializer=bool(*)(const unsigned char*,unsigned char*,std::size_t,
                                  CheckpointOwnership*) noexcept;
inline std::atomic<CheckpointSerializer> checkpointSerializer{nullptr};
inline bool RegisterCheckpointSerializer(CheckpointSerializer value) noexcept {
    if(!value)return false;
    CheckpointSerializer expected=nullptr;
    return checkpointSerializer.compare_exchange_strong(expected,value)||expected==value;
}
inline void UnregisterCheckpointSerializer(CheckpointSerializer value) noexcept {
    checkpointSerializer.compare_exchange_strong(value,nullptr);
}
inline bool SerializeCheckpoint(const unsigned char* input,unsigned char* output,
                                std::size_t size,CheckpointOwnership* ownership) noexcept {
    const auto fn=checkpointSerializer.load();
    return fn&&input&&output&&ownership&&fn(input,output,size,ownership);
}
// Existing save notifications remain passive. The optional checkpoint selector
// is called by the SAME I/O owner before its load transformation, never as a
// competing fread hook. Its storage/code lives until process exit.
struct Observer {
    void (*read)(const wchar_t* path,const unsigned char* disk,
                 const unsigned char* loaded,std::size_t size) noexcept=nullptr;
    void (*write)(const wchar_t* path,const unsigned char* actual,
                  std::size_t size) noexcept=nullptr;
    void (*reset)() noexcept=nullptr;
    bool (*project)(const wchar_t*,const unsigned char*,unsigned char*,std::size_t,void**) noexcept=nullptr;
    bool (*prepare)(void*,const wchar_t*,const unsigned char*,std::size_t) noexcept=nullptr;
    void (*finish)(void*,const unsigned char*,std::size_t,bool) noexcept=nullptr;
    bool (*projectionRequired)() noexcept=nullptr;
    // Receives the exact serialized bytes, after other owners' transformations,
    // before the existing single native write. It never replaces the CRT buffer.
    void (*prepareWrite)(const wchar_t* path,const unsigned char* actual,
                         std::size_t size) noexcept=nullptr;
    bool (*selectRead)(const wchar_t* path,const unsigned char* disk,
                       unsigned char* selected,std::size_t size,CheckpointSelection*) noexcept=nullptr;
    void (*checkpointRead)(const wchar_t* path,const unsigned char* disk,
                           const unsigned char* loaded,std::size_t size,
                           const CheckpointSelection&) noexcept=nullptr;
    // Rejection is not a confirmed new-game/reset event. Consumers explicitly
    // close admission without replacing their current state with a fresh save.
    void (*rejectRead)() noexcept=nullptr;
};
inline constexpr std::size_t kMaximumObservers=8;
inline std::array<std::atomic<const Observer*>,kMaximumObservers> observers{};
inline std::mutex registrationMutex;
inline bool Subscribe(const Observer* value) noexcept {
    if(!value || !value->read || !value->write || bool(value->selectRead)!=bool(value->checkpointRead))return false;
    // Registration happens outside callbacks/teardown. Serializing registrations
    // prevents the same permanent observer from acquiring two different slots.
    try {
        std::lock_guard<std::mutex> lock(registrationMutex);
        for(const auto& slot:observers)if(slot.load()==value)return true;
        if(value->selectRead)for(const auto& slot:observers){const auto* owner=slot.load();
            if(owner&&owner->selectRead)return false;
        }
        for(auto& slot:observers){
            const Observer* expected=nullptr;
            if(slot.compare_exchange_strong(expected,value))return true;
        }
    } catch(...) {return false;}
    return false;
}
inline bool Requested() noexcept {
    for(const auto& slot:observers)if(slot.load())return true;
    return false;
}
inline void Unsubscribe(const Observer* owner) noexcept {
    if(!owner)return;
    // Teardown never takes a mutex or waits for an entered callback. Observers
    // keep permanent storage/code and close their own logical admission first.
    for(auto& slot:observers){const Observer* expected=owner;slot.compare_exchange_strong(expected,nullptr);}
}
inline bool Subscribed(const Observer* owner) noexcept {
    for(const auto& slot:observers)if(slot.load()==owner)return true;
    return false;
}
template<class Callback> inline void Dispatch(Callback callback) noexcept {
    std::array<const Observer*,kMaximumObservers> snapshot{};
    for(std::size_t i=0;i<snapshot.size();++i)snapshot[i]=observers[i].load();
    for(std::size_t i=0;i<snapshot.size();++i){
        const auto* value=snapshot[i];if(!value||!Subscribed(value))continue;
        bool duplicate=false;
        for(std::size_t previous=0;previous<i;++previous)if(snapshot[previous]==value)duplicate=true;
        if(!duplicate)callback(*value);
    }
}
inline bool SelectRead(const wchar_t* path,const unsigned char* disk,unsigned char* selected,
                       std::size_t size,CheckpointSelection* selection) noexcept {
    if(!selection)return false;
    *selection={};bool valid=true;
    Dispatch([&](const Observer& value){if(value.selectRead)
        valid=value.selectRead(path,disk,selected,size,selection)&&valid;
    });
    return valid;
}
inline void ReadCompleted(const wchar_t* path,const unsigned char* disk,
                          const unsigned char* loaded,std::size_t size,
                          const CheckpointSelection* selection=nullptr,
                          const unsigned char* selectedDisk=nullptr) noexcept {
    Dispatch([&](const Observer& value){
        if(selection&&value.checkpointRead)value.checkpointRead(path,disk,loaded,size,*selection);
        else value.read(path,selection&&selection->selected&&selectedDisk?selectedDisk:disk,loaded,size);
    });
}
inline void WriteCompleted(const wchar_t* path,const unsigned char* actual,
                           std::size_t size) noexcept {
    Dispatch([&](const Observer& value){value.write(path,actual,size);});
}
inline void ResetCompleted() noexcept {Dispatch([](const Observer& value){if(value.reset)value.reset();});}
inline void ReadRejected() noexcept {Dispatch([](const Observer& value){if(value.rejectRead)value.rejectRead();});}
struct WriteTransaction {
    struct Participant {const Observer* observer=nullptr;void* cookie=nullptr;};
    std::array<Participant,kMaximumObservers> participants{};
    std::size_t count=0;bool active=false;
    WriteTransaction()=default;
    WriteTransaction(const WriteTransaction&)=delete;
    WriteTransaction& operator=(const WriteTransaction&)=delete;
    ~WriteTransaction();
};
inline bool ProjectionRequired() noexcept {
    bool required=false;
    Dispatch([&](const Observer& observer){if(observer.projectionRequired&&observer.projectionRequired())required=true;});
    return required;
}
inline void FinishWrite(WriteTransaction& tx,const unsigned char* actual,std::size_t size,bool success) noexcept {
    if(!tx.active)return;
    tx.active=false;
    // An entered transaction owns its cookies even after unsubscribe. Observer
    // code/storage is permanent; finish must release state on both outcomes.
    for(std::size_t i=0;i<tx.count;++i){const auto participant=tx.participants[i];
        tx.participants[i]={};
        if(participant.observer&&participant.observer->finish)participant.observer->finish(participant.cookie,actual,size,success);
    }
    tx.count=0;
}
inline WriteTransaction::~WriteTransaction(){FinishWrite(*this,nullptr,0,false);}
inline bool ProjectWrite(const wchar_t* path,const unsigned char* source,unsigned char* output,std::size_t size,WriteTransaction& tx) noexcept {
    if(tx.active||!path||!*path||!source||!output||!size||size>65536)return false;
    const auto a=reinterpret_cast<std::uintptr_t>(source),b=reinterpret_cast<std::uintptr_t>(output);
    if(a>UINTPTR_MAX-size||b>UINTPTR_MAX-size||(a<b+size&&b<a+size))return false;
    std::memcpy(output,source,size);tx.active=true;tx.count=0;
    bool valid=true;
    Dispatch([&](const Observer& observer){
        if(!valid||(!observer.project&&!observer.prepare&&!observer.finish))return;
        auto& participant=tx.participants[tx.count++];participant.observer=&observer;
        if(observer.project&&!observer.project(path,source,output,size,&participant.cookie))valid=false;
    });
    if(!valid){FinishWrite(tx,nullptr,0,false);std::memcpy(output,source,size);}
    return valid;
}
inline bool PrepareWrite(const wchar_t* path,const unsigned char* output,std::size_t size,WriteTransaction& tx) noexcept {
    if(!tx.active||!path||!output||!size)return false;
    for(std::size_t i=0;i<tx.count;++i){const auto& participant=tx.participants[i];
        if(participant.observer->prepare&&!participant.observer->prepare(participant.cookie,path,output,size)){
            FinishWrite(tx,nullptr,0,false);return false;
        }
    }
    return true;
}
inline void WritePrepared(const wchar_t* path,const unsigned char* actual,
                          std::size_t size) noexcept {
    Dispatch([&](const Observer& value){if(value.prepareWrite)value.prepareWrite(path,actual,size);});
}
}
