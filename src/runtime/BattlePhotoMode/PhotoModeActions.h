#pragma once
// Battle pose editing uses the existing Present/menu producers. This is not a
// simulation pause or a replacement for the separate native free-camera option.
#include "PhotoModeCore.h"
#include "../FfxHooksDll/shared/Config.h"
#include "../FfxHooksDll/hooks/RecoveryNative.h"
#include "../FfxHooksDll/hooks/RecoveryBattleEpoch.h"
#include "../FfxHooksDll/shared/ffx_addresses.h"
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace PhotoMode {
namespace P=FfxHooks::Photo;
namespace N=FfxHooks::RecoveryNative;
constexpr std::uint32_t RVA_ACTIVE_CHR_COUNT=0x01FC44E0,RVA_ACTIVE_CHR_TABLE=0x01FC44E4;
constexpr std::uint32_t ACTIVE_CHR_STRIDE=0x880,RVA_CAM_REF=0x00D378A0;
extern std::uintptr_t g_base;
// Independent persisted capabilities are intentionally missing/OFF by default.
struct Capabilities {
    bool enabled=false, actors=false, camera=false, exportScene=false, hold=false;
    bool operator==(const Capabilities& other) const noexcept {
        return enabled==other.enabled && actors==other.actors && camera==other.camera &&
            exportScene==other.exportScene && hold==other.hold;
    }
};
inline constexpr const char* CapabilityKeys[]={"photo_mode.enabled","photo_mode.actor_edit",
    "photo_mode.camera_target","photo_mode.export_scene","photo_mode.hold_poses"};
inline constexpr const char* CapabilityLabels[]={"Photo Mode master","Actor editing",
    "Camera target editing","Scene JSON export","Hold edited poses (not pause)"};
inline bool ReadCapabilities(Capabilities& out) noexcept {
    Capabilities next{};
    bool* values[]={&next.enabled,&next.actors,&next.camera,&next.exportScene,&next.hold};
    for(unsigned i=0;i<5;++i){
        const auto read=FfxHooks::Config::ReadIntExact(CapabilityKeys[i],0,1);
        if(read.state==FfxHooks::Config::IntReadState::Invalid)return false;
        *values[i]=read.state==FfxHooks::Config::IntReadState::Valid && read.value==1;
    }
    out=next;return true;
}
struct State {
    std::atomic<bool> on{false},stopping{false};
    SRWLOCK lock=SRWLOCK_INIT;
    P::Session session;
    Capabilities admitted{};
    unsigned step=2;
    bool checked=false,profile=false;
    char notice[128]{};
};
extern State g_pm;
struct Lock {
    bool held=TryAcquireSRWLockExclusive(&g_pm.lock)!=FALSE;
    ~Lock(){if(held)ReleaseSRWLockExclusive(&g_pm.lock);}
};
inline bool Foreground() noexcept {
    HWND window=GetForegroundWindow();DWORD process=0;
    return window&&GetWindowThreadProcessId(window,&process)&&process==GetCurrentProcessId()&&
        IsWindowVisible(window)&&!IsIconic(window);
}
inline bool CurrentFrame(P::Frame& frame) noexcept {
    if(g_pm.stopping.load(std::memory_order_acquire)||!g_base)return false;
    const auto epoch=FfxHooks::RecoveryBattleEpoch::Read();
    if(!FfxHooks::RecoveryBattleEpoch::OwnedBy(epoch,GetCurrentThreadId()))return false;
    std::uint8_t battle=0;std::uint32_t table=0,count=0;
    if(!N::Copy(&battle,reinterpret_cast<void*>(g_base+RVA_FFX_BATTLE_ACTIVE_FLAG),1)||!battle||
       !N::Copy(&table,reinterpret_cast<void*>(g_base+RVA_ACTIVE_CHR_TABLE),4)||
       !N::Copy(&count,reinterpret_cast<void*>(g_base+RVA_ACTIVE_CHR_COUNT),4)||!table||!count||count>4096||
       !N::Range(table,std::size_t(count)*ACTIVE_CHR_STRIDE))return false;
    frame={epoch.generation,table,epoch.thread};return true;
}
inline bool ActorValid(const P::Frame& expected,const P::Identity& id) noexcept {
    P::Frame current{};if(!CurrentFrame(current)||!(current==expected))return false;
    std::uint32_t count=0;std::uint16_t value=0;std::uint8_t active=0;
    if(!N::Copy(&count,reinterpret_cast<void*>(g_base+RVA_ACTIVE_CHR_COUNT),4)||id.index>=count||
       id.pointer!=expected.table+std::uintptr_t(id.index)*ACTIVE_CHR_STRIDE||
       !N::Copy(&active,reinterpret_cast<void*>(id.pointer+2),1)||!active||
       !N::Copy(&value,reinterpret_cast<void*>(id.pointer),2)||value!=id.value)return false;
    return value<0x2000;
}
inline bool ReadActor(void*,const P::Frame& frame,const P::Identity& id,P::Pose& pose) noexcept {
    if(!ActorValid(frame,id))return false;
    std::array<float,3> position{},render{};float facing=0;
    if(!N::Copy(position.data(),reinterpret_cast<void*>(id.pointer+0x0C),12)||
       !N::Copy(render.data(),reinterpret_cast<void*>(id.pointer+0x200),12)||
       !N::Copy(&facing,reinterpret_cast<void*>(id.pointer+0x168),4))return false;
    pose={position[0],position[1],position[2],facing,render[0],render[1],render[2]};
    return P::Valid(pose)&&ActorValid(frame,id);
}
inline bool ReadCamera(void*,const P::Frame& expected,P::Camera& camera) noexcept {
    P::Frame frame{};
    return CurrentFrame(frame)&&frame==expected&&
        N::Copy(&camera,reinterpret_cast<void*>(g_base+RVA_CAM_REF),sizeof(camera))&&
        P::Valid(camera)&&CurrentFrame(frame)&&frame==expected;
}
inline bool CompareFloat(std::uintptr_t address,float expected,float desired) noexcept {
    LONG before=0,after=0;std::memcpy(&before,&expected,4);std::memcpy(&after,&desired,4);
    if(address&3)return false;
    __try {return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address),after,before)==before;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class StillOwned>
inline bool WriteFields(const std::uintptr_t* addresses,const float* expected,const float* desired,
                        std::size_t count,StillOwned&& stillOwned) noexcept {
    if(!count||count>7||!stillOwned())return false;
    for(std::size_t i=0;i<count;++i){
        float actual=0;
        if(!P::Scalar(desired[i])||!N::Range(addresses[i],4,0,false,true)||
           !N::Copy(&actual,reinterpret_cast<void*>(addresses[i]),4)||
           std::memcmp(&actual,&expected[i],4)!=0)return false;
    }
    std::size_t written=0;
    for(;written<count;++written){
        if(!stillOwned()||!CompareFloat(addresses[written],expected[written],desired[written]))break;
    }
    bool accepted=written==count&&stillOwned();
    if(accepted)for(std::size_t i=0;i<count;++i){
        float actual=0;
        if(!stillOwned()||!N::Copy(&actual,reinterpret_cast<void*>(addresses[i]),4)||
           std::memcmp(&actual,&desired[i],4)!=0){accepted=false;break;}
    }
    accepted=accepted&&stillOwned();
    if(!accepted){
        // A reused actor/table is no longer ours even if a float still matches.
        // Once ownership ends, do not touch its storage, including rollback.
        while(written&&stillOwned()){
            --written;
            (void)CompareFloat(addresses[written],desired[written],expected[written]);
        }
    }
    return accepted;
}
inline bool WriteActor(void*,const P::Frame& frame,const P::Identity& id,const P::Pose& expected,const P::Pose& desired) noexcept {
    if(!P::Valid(desired)||!ActorValid(frame,id))return false;
    const std::uintptr_t addresses[]={id.pointer+0x0C,id.pointer+0x10,id.pointer+0x14,id.pointer+0x168,
        id.pointer+0x200,id.pointer+0x204,id.pointer+0x208};
    const float before[]={expected.x,expected.y,expected.z,expected.yaw,expected.rx,expected.ry,expected.rz};
    const float after[]={desired.x,desired.y,desired.z,desired.yaw,desired.rx,desired.ry,desired.rz};
    return WriteFields(addresses,before,after,7,[&]() noexcept {return ActorValid(frame,id);});
}
inline bool WriteCamera(void*,const P::Frame& expectedFrame,const P::Camera& expected,const P::Camera& desired) noexcept {
    P::Frame frame{};if(!CurrentFrame(frame)||!(frame==expectedFrame)||!P::Valid(desired))return false;
    const std::uintptr_t addresses[]={g_base+RVA_CAM_REF,g_base+RVA_CAM_REF+4,g_base+RVA_CAM_REF+8};
    const float before[]={expected.x,expected.y,expected.z},after[]={desired.x,desired.y,desired.z};
    return WriteFields(addresses,before,after,3,[&]() noexcept {
        P::Frame current{};return CurrentFrame(current)&&current==expectedFrame;
    });
}
inline bool Capture(void*,P::Capture& output) noexcept {
    output={};if(!CurrentFrame(output.frame))return false;
    std::uint32_t count=0;
    if(!N::Copy(&count,reinterpret_cast<void*>(g_base+RVA_ACTIVE_CHR_COUNT),4)||count>4096)return false;
    for(std::uint32_t index=0;index<count;++index){
        const auto pointer=output.frame.table+std::uintptr_t(index)*ACTIVE_CHR_STRIDE;
        std::uint8_t active=0;std::uint16_t id=0;
        if(!N::Copy(&active,reinterpret_cast<void*>(pointer+2),1))return false;
        if(!active)continue;
        if(!N::Copy(&id,reinterpret_cast<void*>(pointer),2))return false;
        if(id>=0x2000)continue;
        P::Actor actor{};actor.id={index,pointer,id};
        if(!ReadActor(nullptr,output.frame,actor.id,actor.pose))return false;
        if(output.count<output.actors.size())output.actors[output.count++]=actor;
        else ++output.omitted;
    }
    std::uint32_t currentCount=0;
    return output.count&&ReadCamera(nullptr,output.frame,output.camera)&&
        N::Copy(&currentCount,reinterpret_cast<void*>(g_base+RVA_ACTIVE_CHR_COUNT),4)&&currentCount==count;
}
inline const P::Io& Io() noexcept {
    static const P::Io io{nullptr,Capture,ReadActor,WriteActor,ReadCamera,WriteCamera};return io;
}
inline bool Profile() noexcept {
    if(!g_pm.checked){
        g_pm.checked=true;
        g_pm.profile=N::Profile(g_base)&&N::Match(g_base,FfxHooks::RecoveryEvidence::PhotoPosition)&&
            N::Match(g_base,FfxHooks::RecoveryEvidence::PhotoUpdate)&&
            N::Range(g_base+RVA_CAM_REF,12,g_base,false,true);
    }
    return g_pm.profile;
}
// Caller holds the adapter lock. Restore through identity-gated low-level I/O,
// which deliberately remains available after a configuration capability is revoked.
inline bool CapabilitiesCurrent() noexcept {
    Capabilities current{};
    if(!g_pm.stopping.load(std::memory_order_acquire) &&
       !N::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY") && Foreground() &&
       ReadCapabilities(current) && current.enabled && current==g_pm.admitted)return true;
    const bool restored=g_pm.session.End(Io());
    g_pm.on.store(false,std::memory_order_release);
    strncpy_s(g_pm.notice,restored?"Capabilities changed; session stopped and restored.":
        "Session stopped; identity or write conflict prevented complete restoration.",_TRUNCATE);
    return false;
}
inline bool Enter() noexcept {
    Lock lock;if(!lock.held)return false;
    g_pm.notice[0]=0;
    if(g_pm.stopping.load()||N::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY")||!Foreground())return false;
    Capabilities configured{};
    if(!ReadCapabilities(configured)||!configured.enabled){
        if(g_pm.session.Active())(void)g_pm.session.End(Io());
        g_pm.on.store(false,std::memory_order_release);
        strncpy_s(g_pm.notice,"Photo Mode master is OFF or a capability setting is invalid.",_TRUNCATE);
        return false;
    }
    if(!Profile()){strncpy_s(g_pm.notice,"Photo Mode profile is not admitted.",_TRUNCATE);return false;}
    const bool started=g_pm.session.Begin(Io());
    if(started){g_pm.admitted=configured;g_pm.session.SetHolding(configured.hold);}
    g_pm.on.store(started,std::memory_order_release);return started;
}
inline bool Exit() noexcept {
    Lock lock;if(!lock.held)return false;
    const bool restored=g_pm.session.End(Io());g_pm.on.store(false,std::memory_order_release);return restored;
}
inline void RequestStop() noexcept {g_pm.stopping.store(true,std::memory_order_release);g_pm.on.store(false,std::memory_order_release);}
inline void Tick() noexcept {
    if(!g_pm.on.load(std::memory_order_acquire))return;
    const DWORD lastError=GetLastError();
    Lock lock;
    if(lock.held){
        if(CapabilitiesCurrent())(void)g_pm.session.Tick(Io());
        g_pm.on.store(g_pm.session.Active(),std::memory_order_release);
    }
    SetLastError(lastError);
}
inline void SelectDelta(int delta) noexcept {Lock lock;if(lock.held)g_pm.session.Select(delta);}
template<class Action>
inline bool Edit(Action&& action) noexcept {
    Lock lock;if(!lock.held)return false;
    g_pm.notice[0]=0;
    if(!g_pm.session.Active()||!CapabilitiesCurrent())return false;
    const bool result=action();
    g_pm.on.store(g_pm.session.Active(),std::memory_order_release);
    return result;
}
inline bool MoveSelected(float x,float y,float z) noexcept {return Edit([&]() noexcept {return g_pm.admitted.actors && g_pm.session.Move(Io(),x,y,z);});}
inline bool RotateSelected(float delta) noexcept {return Edit([&]() noexcept {return g_pm.admitted.actors && g_pm.session.Rotate(Io(),delta);});}
inline bool PanCamera(float x,float y,float z) noexcept {return Edit([&]() noexcept {return g_pm.admitted.camera && g_pm.session.Pan(Io(),x,y,z);});}
inline bool ResetAll() noexcept {return Edit([]() noexcept {return g_pm.session.Reset(Io());});}
inline bool Snapshot() noexcept {
    try {
        Lock lock;if(!lock.held||!g_pm.session.Active())return false;
        if(!CapabilitiesCurrent()||!g_pm.admitted.exportScene)return false;
        g_pm.notice[0]=0;
        std::string json;const bool sampled=g_pm.session.Export(Io(),json);
        g_pm.on.store(g_pm.session.Active(),std::memory_order_release);
        if(!sampled||json.size()>P::kMaximumJson)return false;
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Snapshot),&self))return false;
        wchar_t modulePath[4097]{};const DWORD count=GetModuleFileNameW(self,modulePath,4097);
        if(!count||count>=4097)return false;
        std::wstring directory(modulePath,count);const auto slash=directory.find_last_of(L"/\\");
        if(slash==std::wstring::npos)return false;
        directory.resize(slash);directory+=L"\\photo-scenes";
        if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        const auto attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        static std::atomic<unsigned> serial{0};
        const auto path=directory+L"\\scene-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(serial.fetch_add(1))+L".json";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        DWORD written=0;bool ok=WriteFile(file,json.data(),static_cast<DWORD>(json.size()),&written,nullptr)&&
            written==json.size()&&FlushFileBuffers(file);
        if(!CloseHandle(file))ok=false;
        if(!ok)DeleteFileW(path.c_str());
        if(ok)_snprintf_s(g_pm.notice,sizeof(g_pm.notice),_TRUNCATE,
            "Exported %zu actors (%zu omitted) to photo-scenes. No save changed.",
            g_pm.session.Count(),g_pm.session.Omitted());
        return ok;
    } catch(...) {return false;}
}
inline float MoveStep() noexcept {constexpr float steps[]={0.1f,0.5f,1.0f,5.0f};return steps[g_pm.step%4];}
inline bool ToggleCapability(unsigned index) noexcept {
    if(index>=5)return false;
    Lock lock;if(!lock.held||g_pm.stopping.load()||
        N::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY")||!Foreground())return false;
    const auto before=FfxHooks::Config::ReadIntExact(CapabilityKeys[index],0,1);
    // Invalid input repairs to OFF, never silently enables a writer.
    const bool next=before.state==FfxHooks::Config::IntReadState::Missing ||
        (before.state==FfxHooks::Config::IntReadState::Valid && before.value==0);
    if(g_pm.session.Active()){
        const bool restored=g_pm.session.End(Io());
        g_pm.on.store(false,std::memory_order_release);
        if(!restored){strncpy_s(g_pm.notice,"Session stopped with restoration conflict; capability unchanged.",_TRUNCATE);return false;}
    }
    const bool saved=FfxHooks::Config::SetInt(CapabilityKeys[index],next?1:0);
    const auto observed=FfxHooks::Config::ReadIntExact(CapabilityKeys[index],0,1);
    const bool verified=saved && observed.state==FfxHooks::Config::IntReadState::Valid && observed.value==(next?1:0);
    strncpy_s(g_pm.notice,verified?"Capability saved. Start a new Photo Mode session explicitly.":
        "Capability could not be saved or verified. Session remains stopped.",_TRUNCATE);
    return verified;
}
inline int MenuCount() noexcept {return 26;}
inline void MenuLabel(int row,char* output,std::size_t capacity) noexcept {
    if(!output||!capacity)return;
    Lock lock;
    if(!lock.held){strncpy_s(output,capacity,"Photo Mode is busy",_TRUNCATE);return;}
    if(row>=21&&row<26){
        const auto setting=FfxHooks::Config::ReadIntExact(CapabilityKeys[row-21],0,1);
        const char* state=setting.state==FfxHooks::Config::IntReadState::Invalid?"INVALID":
            setting.state==FfxHooks::Config::IntReadState::Valid&&setting.value?"ON":"OFF";
        _snprintf_s(output,capacity,_TRUNCATE,"%s: %s",CapabilityLabels[row-21],state);return;
    }
    if(row==0){strncpy_s(output,capacity,g_pm.session.Active()?"Stop Photo Mode and restore":"Start battle Photo Mode",_TRUNCATE);return;}
    if(row==1||row==2){const auto id=g_pm.session.SelectedIdentity();_snprintf_s(output,capacity,_TRUNCATE,
        "%s actor [%zu/%zu, ID %04X]",row==1?"Previous":"Next",g_pm.session.Count()?g_pm.session.Selected()+1:0,g_pm.session.Count(),id.value);return;}
    if(row==3){_snprintf_s(output,capacity,_TRUNCATE,"Movement step: %.1f",MoveStep());return;}
    const char* labels[]={"Move actor X -","Move actor X +","Move actor Y -","Move actor Y +","Move actor Z -","Move actor Z +",
        "Rotate actor -","Rotate actor +","Pan target X -","Pan target X +","Pan target Y -","Pan target Y +","Pan target Z -","Pan target Z +"};
    if(row>=4&&row<=17){strncpy_s(output,capacity,labels[row-4],_TRUNCATE);return;}
    if(row==18){strncpy_s(output,capacity,g_pm.session.Holding()?"Hold edited poses: ON (battle still runs)":"Hold edited poses: OFF",_TRUNCATE);return;}
    strncpy_s(output,capacity,row==19?"Reset owned actor and camera changes":row==20?"Export current scene JSON":"Unknown Photo Mode action",_TRUNCATE);
}
inline bool MenuAction(int row) noexcept {
    if(row<0||row>=MenuCount()||g_pm.stopping.load(std::memory_order_acquire))return false;
    if(row>=21)return ToggleCapability(static_cast<unsigned>(row-21));
    if(row==0)return g_pm.on.load()?Exit():Enter();
    if(row==1||row==2){SelectDelta(row==1?-1:1);return g_pm.on.load();}
    if(row==3){Lock lock;if(!lock.held)return false;g_pm.step=(g_pm.step+1)%4;return true;}
    const float step=MoveStep(),direction=(row&1)?step:-step;
    if(row>=4&&row<=9){const int axis=(row-4)/2;return MoveSelected(axis==0?direction:0,axis==1?direction:0,axis==2?direction:0);}
    if(row==10||row==11)return RotateSelected((row==10?-1.0f:1.0f)*0.05f);
    if(row>=12&&row<=17){const int axis=(row-12)/2;return PanCamera(axis==0?direction:0,axis==1?direction:0,axis==2?direction:0);}
    if(row==18)return ToggleCapability(4);
    if(row==19)return ResetAll();
    return row==20&&Snapshot();
}
inline void Detail(char* output,std::size_t capacity) noexcept {
    if(!output||!capacity)return;
    Lock lock;if(!lock.held){strncpy_s(output,capacity,"Photo Mode busy",_TRUNCATE);return;}
    if(g_pm.notice[0]){strncpy_s(output,capacity,g_pm.notice,_TRUNCATE);return;}
    if(g_pm.session.Active()&&g_pm.session.Omitted()){
        _snprintf_s(output,capacity,_TRUNCATE,"%s. %zu captured; %zu eligible actors omitted.",
            P::StatusText(g_pm.session.LastStatus()),g_pm.session.Count(),g_pm.session.Omitted());return;
    }
    strncpy_s(output,capacity,P::StatusText(g_pm.session.LastStatus()),_TRUNCATE);
}
} // namespace PhotoMode
