#!/usr/bin/env python3
"""Photo capability acceptance probe for the actual adapter.

The baseline photo_adapter_tests.py remains separate. This probe preserves the
user-requested OFF/capability/stop contracts with explicit persistence, revocation and menu-action coverage.

Linux RT1: this verifies adapter control flow, not Windows ABI, page protection,
SEH, the actual camera offsets or in-game behavior. All memory belongs to this
short-lived test process; the fake filesystem captures JSON in memory only.
No executable, game process, real save or installed module is accessed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

MOCK = r'''
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <utility>
#include <vector>
using BOOL=int; using DWORD=std::uint32_t; using LONG=std::int32_t;
using HWND=void*; using HMODULE=void*; using HANDLE=void*; using LPCWSTR=const wchar_t*;
constexpr BOOL FALSE=0;
struct SRWLOCK { bool held=false; };
#define SRWLOCK_INIT {}
#define __try try
#define __except(x) catch(...)
#define EXCEPTION_EXECUTE_HANDLER 1
constexpr std::size_t _TRUNCATE=static_cast<std::size_t>(-1);
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=4;
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT=2;
constexpr DWORD ERROR_ALREADY_EXISTS=183, INVALID_FILE_ATTRIBUTES=0xffffffffu;
constexpr DWORD FILE_ATTRIBUTE_DIRECTORY=16, FILE_ATTRIBUTE_REPARSE_POINT=1024;
constexpr DWORD GENERIC_WRITE=0x40000000, CREATE_NEW=1, FILE_ATTRIBUTE_NORMAL=128;
inline HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
namespace Mock {
inline std::vector<std::pair<std::uintptr_t,std::size_t>> regions;
inline std::uint32_t generation=1;
inline unsigned casCalls=0;
inline bool transitionOnCameraRead=false, transitionOnCas=false, validateOnly=false;
inline std::uintptr_t camera=0;
inline std::string exported;
inline std::map<std::string,int> settings;
inline bool failConfigWrite=false;
inline bool Range(std::uintptr_t address,std::size_t size) {
    for(const auto& region:regions)
        if(address>=region.first && address-region.first<=region.second &&
           size<=region.second-(address-region.first)) return true;
    return false;
}
}
inline BOOL TryAcquireSRWLockExclusive(SRWLOCK* lock) { if(lock->held)return 0;lock->held=true;return 1; }
inline void ReleaseSRWLockExclusive(SRWLOCK* lock) {lock->held=false;}
inline HWND GetForegroundWindow(){return reinterpret_cast<HWND>(1);}
inline DWORD GetWindowThreadProcessId(HWND,DWORD* id){*id=3;return 9;}
inline DWORD GetCurrentProcessId(){return 3;}
inline DWORD GetCurrentThreadId(){return 9;}
inline BOOL IsWindowVisible(HWND){return 1;}
inline BOOL IsIconic(HWND){return 0;}
inline DWORD GetLastError(){return 0;}
inline void SetLastError(DWORD){}
inline LONG InterlockedCompareExchange(volatile LONG* address,LONG desired,LONG expected){
    const LONG actual=*address;
    ++Mock::casCalls;
    if(actual==expected)*address=desired;
    if(Mock::transitionOnCas){Mock::transitionOnCas=false;++Mock::generation;}
    return actual;
}
inline BOOL GetModuleHandleExW(DWORD,LPCWSTR,HMODULE* out){*out=reinterpret_cast<HMODULE>(1);return 1;}
inline DWORD GetModuleFileNameW(HMODULE,wchar_t* out,DWORD capacity){
    const wchar_t value[]=L"C:\\simulated\\modules\\ffx-hooks.dll";
    if(capacity<sizeof(value)/sizeof(wchar_t))return 0;
    std::wcscpy(out,value);return static_cast<DWORD>(std::wcslen(value));
}
inline BOOL CreateDirectoryW(LPCWSTR,void*){return 1;}
inline DWORD GetFileAttributesW(LPCWSTR){return FILE_ATTRIBUTE_DIRECTORY;}
inline std::uint64_t GetTickCount64(){return 100;}
inline HANDLE CreateFileW(LPCWSTR,DWORD,DWORD,void*,DWORD,DWORD,void*){return reinterpret_cast<HANDLE>(1);}
inline BOOL WriteFile(HANDLE,const void* data,DWORD size,DWORD* written,void*){
    Mock::exported.assign(static_cast<const char*>(data),size);*written=size;return 1;
}
inline BOOL FlushFileBuffers(HANDLE){return 1;}
inline BOOL CloseHandle(HANDLE){return 1;}
inline BOOL DeleteFileW(LPCWSTR){return 1;}
inline int strncpy_s(char* destination,std::size_t capacity,const char* source,std::size_t limit){
    if(!capacity)return 1;
    const auto count=(std::min)((std::min)(std::strlen(source),limit),capacity-1);
    std::memcpy(destination,source,count);destination[count]=0;return 0;
}
template<std::size_t N> int strncpy_s(char(&out)[N],const char* source,std::size_t limit){return strncpy_s(out,N,source,limit);}
template<std::size_t N> int strcpy_s(char(&out)[N],const char* source){return strncpy_s(out,N,source,_TRUNCATE);}
inline int _snprintf_s(char* out,std::size_t capacity,std::size_t,const char* format,...){
    va_list args;va_start(args,format);const int result=std::vsnprintf(out,capacity,format,args);va_end(args);return result;
}
namespace FfxHooks::RecoveryEvidence {inline constexpr int PhotoPosition=1,PhotoUpdate=2;}
namespace FfxHooks::Config {
enum class IntReadState {Missing,Valid,Invalid};
struct IntReadResult {IntReadState state;int value;};
inline IntReadResult ReadIntExact(const char* key,int minimum,int maximum){
    const auto found=Mock::settings.find(key);
    if(found==Mock::settings.end())return {IntReadState::Missing,0};
    return {found->second<minimum||found->second>maximum?IntReadState::Invalid:IntReadState::Valid,found->second};
}
inline bool SetInt(const char* key,int value){
    if(Mock::failConfigWrite)return false;
    Mock::settings[key]=value;return true;
}
}
namespace FfxHooks::RecoveryNative {
inline bool Copy(void* out,const void* source,std::size_t size) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    if(!Mock::Range(address,size))return false;
    std::memcpy(out,source,size);
    if(Mock::transitionOnCameraRead && address==Mock::camera){Mock::transitionOnCameraRead=false;++Mock::generation;}
    return true;
}
inline bool Range(std::uintptr_t address,std::size_t size,std::uintptr_t=0,bool=false,bool=false){return Mock::Range(address,size);}
inline bool Profile(std::uintptr_t){return true;}
inline bool Match(std::uintptr_t,int){return true;}
inline bool EnvironmentEnabled(const char*){return Mock::validateOnly;}
}
namespace FfxHooks::RecoveryBattleEpoch {
struct Epoch {std::uint32_t generation,thread;};
inline Epoch Read(){return {Mock::generation,9};}
inline bool OwnedBy(Epoch e,std::uint32_t t){return e.generation && e.thread==t;}
}
'''
TEST = r'''
#include "src/runtime/BattlePhotoMode/PhotoModeActions.h"
#include <sys/mman.h>
#include <new>
#include <stdexcept>
namespace PhotoMode {std::uintptr_t g_base=0;State g_pm;}
namespace PM=PhotoMode;
int checks=0,failed=0;
void Check(bool value,const char* message){++checks;if(!value){++failed;std::fprintf(stderr,"FAIL %s\n",message);}}
void* Allocate(std::size_t size){
    void* value=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    if(value==MAP_FAILED || reinterpret_cast<std::uintptr_t>(value)+size>UINT32_MAX)
        throw std::runtime_error("32-bit fixture mapping failed");
    Mock::regions.emplace_back(reinterpret_cast<std::uintptr_t>(value),size);return value;
}
void Put(std::uintptr_t address,float value){std::memcpy(reinterpret_cast<void*>(address),&value,4);}
float Get(std::uintptr_t address){float value;std::memcpy(&value,reinterpret_cast<void*>(address),4);return value;}
int main(){
    try {
        const auto image=reinterpret_cast<std::uintptr_t>(Allocate(0x2380000));
        const auto table=reinterpret_cast<std::uintptr_t>(Allocate(70*PM::ACTIVE_CHR_STRIDE));
        PM::g_base=image;Mock::camera=image+PM::RVA_CAM_REF;
        *reinterpret_cast<std::uint8_t*>(image+RVA_FFX_BATTLE_ACTIVE_FLAG)=1;
        *reinterpret_cast<std::uint32_t*>(image+PM::RVA_ACTIVE_CHR_COUNT)=70;
        *reinterpret_cast<std::uint32_t*>(image+PM::RVA_ACTIVE_CHR_TABLE)=static_cast<std::uint32_t>(table);
        for(unsigned i=0;i<70;++i){
            const auto actor=table+i*PM::ACTIVE_CHR_STRIDE;
            *reinterpret_cast<std::uint16_t*>(actor)=static_cast<std::uint16_t>(i);
            *reinterpret_cast<std::uint8_t*>(actor+2)=1;
            Put(actor+0x0c,1);Put(actor+0x10,2);Put(actor+0x14,3);
            Put(actor+0x200,1);Put(actor+0x204,2);Put(actor+0x208,3);
        }
        PM::P::Capture captured{};
        Check(PM::Capture(nullptr,captured),"capture supported fixture");
        Check(captured.count==64 && captured.omitted==6,"capture reports eligible actors beyond bound");
        PM::P::Frame frame{};Check(PM::CurrentFrame(frame),"read current frame");
        PM::P::Camera camera{};Mock::transitionOnCameraRead=true;
        Check(!PM::ReadCamera(nullptr,frame,camera),"camera validates generation after payload copy");
        Mock::validateOnly=true;
        Check(!PM::Enter() && Mock::casCalls==0,"validate-only prevents entry and writes");
        Mock::validateOnly=false;
        Check(!PM::Enter() && !PM::g_pm.on.load(),"missing master capability defaults OFF");
        if(PM::g_pm.on.load())PM::Exit();
        Mock::settings["photo_mode.enabled"]=1;
        Check(PM::Enter(),"explicit master admits read-only capture with editing OFF");
        const auto disabledWrites=Mock::casCalls;
        Check(!PM::MoveSelected(1,0,0),"actor movement requires independent capability");
        Check(!PM::RotateSelected(.1f),"actor rotation requires independent capability");
        Check(!PM::PanCamera(1,0,0),"camera target requires independent capability");
        Check(!PM::Snapshot(),"JSON export requires independent capability");
        Check(Mock::casCalls==disabledWrites && Mock::exported.empty(),"disabled capabilities perform no write or export");
        Check(!PM::g_pm.session.Holding(),"pose hold defaults OFF");
        PM::Exit();
        Mock::settings["photo_mode.actor_edit"]=1;
        Mock::settings["photo_mode.camera_target"]=1;
        Mock::settings["photo_mode.export_scene"]=1;
        Check(PM::Enter() && PM::MoveSelected(1,0,0),"start and edit a held pose");
        char detail[256]{};PM::Detail(detail,sizeof(detail));
        Check(std::strstr(detail,"6")!=nullptr && std::strstr(detail,"omitted")!=nullptr,"UI detail reports capture omissions");
        Put(table+0x0c,901);Put(table+0x200,901);
        const unsigned before=Mock::casCalls;
        Check(PM::Snapshot(),"export current scene");
        Check(Mock::casCalls==before && Get(table+0x0c)==901,"snapshot does not reassert a hold target");
        Check(Mock::exported.find("\"x\":901")!=std::string::npos,"snapshot contains observed coordinates");
        Check(Mock::exported.find("\"omittedActors\":6")!=std::string::npos,"snapshot exposes truncation");
        Check(!PM::Exit(),"foreign actor drift is preserved during exit");
        Check(PM::Enter(),"explicit re-entry can capture preserved scene");
        PM::P::Pose oldPose{},newPose{};PM::CurrentFrame(frame);
        const PM::P::Identity id{0,table,0};PM::ReadActor(nullptr,frame,id,oldPose);
        newPose=oldPose;newPose.x+=1;newPose.rx+=1;
        Mock::transitionOnCas=true;const unsigned casBefore=Mock::casCalls;
        Check(!PM::WriteActor(nullptr,frame,id,oldPose,newPose),"actor publication rejects mid-write generation change");
        Check(Mock::casCalls==casBefore+1,"no second write or stale rollback after identity ownership ends");
        Check(!PM::MoveSelected(1,0,0) && !PM::g_pm.on.load(),"wrapper closes public admission after core identity cancellation");
        Mock::settings["photo_mode.actor_edit"]=2;
        Check(!PM::Enter(),"invalid capability cannot silently enable editing");
        Mock::settings["photo_mode.actor_edit"]=1;
        Check(PM::Enter() && PM::MoveSelected(1,0,0),"start before capability removal");
        const float ownedPosition=Get(table+0x0c);
        Mock::settings["photo_mode.actor_edit"]=0;
        PM::Tick();
        Check(!PM::g_pm.on.load() && Get(table+0x0c)==ownedPosition-1,"capability removal stops and restores session");
        Check(!PM::MenuAction(-1) && !PM::MenuAction(PM::MenuCount()),"out-of-range rows have no effect");
        Mock::settings["photo_mode.enabled"]=0;
        Mock::failConfigWrite=true;
        Check(!PM::MenuAction(0) && Mock::settings["photo_mode.enabled"]==0,"failed config write cannot enable master");
        Mock::failConfigWrite=false;
        Check(!PM::g_pm.on.load(),"failed entry leaves session inactive");
        Mock::failConfigWrite=true;
        Check(!PM::MenuAction(21) && Mock::settings["photo_mode.enabled"]==0,"master toggle preserves failed persistence");
        Mock::failConfigWrite=false;
        Check(PM::MenuAction(21) && Mock::settings["photo_mode.enabled"]==1 && !PM::g_pm.on.load(),"master toggle saves without starting session");
        Mock::settings["photo_mode.actor_edit"]=2;
        Check(PM::MenuAction(22) && Mock::settings["photo_mode.actor_edit"]==0,"invalid editing setting repairs to OFF");
        Check(PM::MenuAction(22) && Mock::settings["photo_mode.actor_edit"]==1,"actor toggle persists independently");
        Check(PM::Enter() && PM::MoveSelected(1,0,0),"menu revocation starts with an owned edit");
        const float beforeRevoke=Get(table+0x0c);
        Check(PM::MenuAction(22) && !PM::g_pm.on.load() && Get(table+0x0c)==beforeRevoke-1,
            "menu capability removal restores before returning");
        Check(PM::MenuAction(22) && PM::Enter(),"capability can be explicitly re-enabled");
        Check(PM::MenuAction(18) && Mock::settings["photo_mode.hold_poses"]==1 && !PM::g_pm.on.load(),
            "hold selection persists but stops the old session");
        Check(PM::Enter() && PM::g_pm.session.Holding(),"explicit restart applies hold capability");
        for(int row=21;row<26;++row){char label[128]{};PM::MenuLabel(row,label,sizeof(label));
            Check(std::strstr(label,"ON")||std::strstr(label,"OFF"),"every capability has visible state");}
        const auto beforeStop=Mock::casCalls;
        PM::RequestStop();
        Check(!PM::Enter() && !PM::MoveSelected(1,0,0) && !PM::PanCamera(1,0,0) && !PM::Snapshot(),"process stop closes all actions");
        Check(Mock::casCalls==beforeStop,"stop request is non-writing");
        for(const auto& region:Mock::regions)munmap(reinterpret_cast<void*>(region.first),region.second);
        std::printf("PhotoAdapterSimulatedRt1: %d/%d passed (simulated OS/native dependencies)\n",checks-failed,checks);
        return failed?1:0;
    } catch(const std::exception& error){std::fprintf(stderr,"fixture error: %s\n",error.what());return 2;}
}
'''

def main() -> None:
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which(os.environ.get('CXX', 'g++'))
    if not compiler:
        raise SystemExit('A C++17 compiler is required')
    with tempfile.TemporaryDirectory(prefix='ffx-photo-adapter-rt1-') as directory:
        work = Path(directory)
        for name in ('PhotoModeActions.h', 'PhotoModeCore.h'):
            target = work / 'src/runtime/BattlePhotoMode' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / 'src/runtime/BattlePhotoMode' / name, target)
        hooks = work / 'src/runtime/FfxHooksDll/hooks'
        hooks.mkdir(parents=True)
        (hooks / 'RecoveryNative.h').write_text(MOCK)
        (hooks / 'RecoveryBattleEpoch.h').write_text('#pragma once\n')
        shared = hooks.parent / 'shared'
        shared.mkdir()
        (shared / 'Config.h').write_text('#pragma once\n')
        # Deliberately artificial offset: this harness does not prove game RVAs.
        (shared / 'ffx_addresses.h').write_text('#pragma once\n#define RVA_FFX_BATTLE_ACTIVE_FLAG 0x100\n')
        (work / 'test.cpp').write_text(TEST)
        flags = ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror']
        if os.environ.get('FFX_PHOTO_TEST_BASELINE'):
            flags.append('-Wno-error=misleading-indentation')
        subprocess.run([compiler, *flags, str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
        subprocess.run([str(work / 'test')], check=True)

if __name__ == '__main__':
    main()
