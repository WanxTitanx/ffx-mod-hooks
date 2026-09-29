#!/usr/bin/env python3
"""Compile the actual Ronso read adapter control flow with bounded fake I/O.
No game, native save, DLL, installation or system filesystem is accessed by C++.
The pool codec/storage and OS are explicit dependencies, not acceptance targets.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

MOCK = r'''
#include "src/runtime/FfxHooksDll/hooks/NativeSaveEvents.h"
#include "src/runtime/FfxHooksDll/hooks/RonsoPoolSave.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#define __cdecl
using DWORD=std::uint32_t;
using namespace FfxHooks::RonsoPool;
namespace NativeSaveEvents=FfxHooks::NativeSaveEvents;
constexpr DWORD ERROR_INVALID_DATA=13;
enum class OwnerRead{Found,Missing,Invalid,Unavailable};
namespace Fake {
inline unsigned reads=0,starts=0,completed=0;
inline bool pending=true,streamOk=true,pathOk=true,bumpEpoch=false;
inline std::size_t returned=kSaveSize;
inline std::int64_t offset=0;
inline DWORD lastError=0,seenError=0;
inline std::uintptr_t caller=0x6f0228;
inline const unsigned char* startedBuffer=nullptr;
}
std::atomic<int> readiness{1};
std::atomic<std::uint32_t> stateEpoch{1},activeActor{0},actorThread{0};
std::uint32_t sessionEpoch=1;
std::mutex stateMutex;
std::atomic<bool> storageFault{false};
SaveSession session;
bool requestedGameplay=false;
void* _ReturnAddress(){return reinterpret_cast<void*>(Fake::caller);}
DWORD GetLastError(){return Fake::lastError;}
void SetLastError(DWORD value){Fake::lastError=value;}
bool IoCaller(std::uintptr_t address,bool){return address==0x6f0228;}
bool StreamInfo(void*,wchar_t* path,DWORD,std::int64_t* offset){
 std::wcscpy(path,L"C:\\fixture\\ffx_000");*offset=Fake::offset;Fake::lastError=29;return Fake::streamOk;
}
namespace OwnerStore {bool IsSavePath(const wchar_t*){return Fake::pathOk;}}
bool DataRange(std::uintptr_t address,std::size_t,bool){return address!=0;}
bool Copy(void* out,const void* in,std::size_t count){std::memcpy(out,in,count);return true;}
std::uint16_t CurrentScene(){return 23;}
void Notice(const char*){}
struct Store {OwnerRead Read(const std::wstring&,const SaveImage&,SavedOwner*){return OwnerRead::Missing;}} store;
SaveDecision FfxHooks::RonsoPool::LoadPool(bool,const SaveImage& in,const SavedOwner*,std::uint16_t,SaveSession*,SaveImage* out) noexcept {
 *out=in;return SaveDecision::Native;
}
struct IoWork {SaveImage input{},output{},selected{};NativeSaveEvents::CheckpointSelection selection{};std::wstring path;SavedOwner owner{};bool needsOwner=false,useOutput=false;std::uint32_t epoch=0;};
void ReadStartingObserver(const unsigned char* buffer) noexcept {
 ++Fake::starts;Fake::startedBuffer=buffer;Fake::pending=false;Fake::lastError=83;
 if(Fake::bumpEpoch)++stateEpoch;
}
void ReadObserver(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {++Fake::completed;}
void WriteObserver(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
const NativeSaveEvents::Observer observer=[](){NativeSaveEvents::Observer o{};o.read=ReadObserver;o.write=WriteObserver;o.readStarting=ReadStartingObserver;return o;}();
std::size_t NativeRead(void* data,std::size_t,std::size_t count,void*){
 ++Fake::reads;Fake::seenError=Fake::lastError;
 const auto got=(std::min)(Fake::returned,count);
 if(data&&got)std::memset(data,0x5a,got);
 Fake::lastError=71;return got;
}
using ReadFn=std::size_t(*)(void*,std::size_t,std::size_t,void*);
ReadFn readOriginal=NativeRead;
void Reset(){Fake::reads=Fake::starts=Fake::completed=0;Fake::pending=Fake::streamOk=Fake::pathOk=true;
 Fake::bumpEpoch=false;Fake::returned=kSaveSize;Fake::offset=0;Fake::lastError=0;Fake::seenError=0;
 Fake::caller=0x6f0228;Fake::startedBuffer=nullptr;readiness=1;stateEpoch=1;storageFault=false;readOriginal=NativeRead;NativeSaveEvents::Subscribe(&observer);}
'''
TEST = r'''
unsigned total=0,failed=0;
void Check(bool ok,const char* why){++total;if(!ok){++failed;std::printf("FAIL: %s\n",why);}}
int main(){
 SaveImage image{};
 for(unsigned mode=0;mode<6;++mode){
  Reset();
  if(mode==1)Fake::returned=37;
  if(mode==2)Fake::returned=0;
  if(mode==3)Fake::offset=3;
  if(mode==4)Fake::streamOk=false;
  if(mode==5)Fake::pathOk=false;
  const auto result=ReadShim(image.data(),1,kSaveSize,reinterpret_cast<void*>(1));
  Check(Fake::reads==1&&result==(mode<3?Fake::returned:0),"native read occurs once; unknown save identity rejects the load");
  Check(Fake::starts==1&&!Fake::pending&&Fake::startedBuffer==image.data(),"every admitted read attempt invalidates the reused buffer");
  Check(Fake::completed==(mode==0?1u:0u),"only a complete qualified save emits completion");
  Check(Fake::lastError==(mode<3?71:ERROR_INVALID_DATA)&&Fake::seenError==29,"observer preserves native errors; identity rejection reports invalid data");
 }
 for(unsigned mode=0;mode<3;++mode){Reset();if(mode==0)readiness=0;if(mode==1)Fake::caller=0x1234;
  const auto count=mode==2?kSaveSize-1:kSaveSize;
  Check(ReadShim(image.data(),1,count,reinterpret_cast<void*>(1))==count&&Fake::reads==1,"unqualified read forwards once");
  Check(Fake::starts==0&&Fake::completed==0,"unqualified reads do not publish save identities");
 }
 Reset();Fake::bumpEpoch=true;ReadShim(image.data(),1,kSaveSize,reinterpret_cast<void*>(1));
 Check(Fake::reads==1&&Fake::starts==1&&Fake::completed==0,"epoch change during notification suppresses stale completion");
 Reset();readOriginal=nullptr;
 Check(ReadShim(image.data(),1,kSaveSize,reinterpret_cast<void*>(1))==0&&Fake::starts==0,"absent native reader does not publish");
 std::printf("SaveReadStartingRt1: %u/%u passed (actual ReadShim, simulated I/O)\n",total-failed,total);
 return failed?1:0;
}
'''
def main():
    root=Path(__file__).resolve().parents[2]
    source=(root/'src/runtime/FfxHooksDll/hooks/RonsoPoolRuntime.cpp').read_text()
    start=source.index('size_t __cdecl ReadShim(')
    end=source.index('bool SerializeCheckpoint(',start)
    block=source[start:end]
    compiler=shutil.which(os.environ.get('CXX','g++'))
    if not compiler:raise SystemExit('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='ffx-save-read-start-') as temp:
        cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
        cpp.write_text(MOCK+'\n'+block+'\n'+TEST)
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-I',str(root),str(cpp),'-o',str(exe)],check=True,timeout=60)
        subprocess.run([str(exe)],check=True,timeout=15)
if __name__=='__main__':main()
