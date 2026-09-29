#!/usr/bin/env python3
"""Execute actual GridTeach load/provenance functions and real learned-state core.
Native payload hashing, sidecar I/O and Windows thread IDs are explicit doubles.
No game process, native file or save is touched. This is not gateway/ABI acceptance.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

PRE=r'''
#include "src/runtime/FfxHooksDll/hooks/GridLearnedCore.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <cstdio>
using DWORD=std::uint32_t;
thread_local DWORD threadId=9;
DWORD GetCurrentThreadId(){return threadId;}
namespace FfxHooks::RonsoPool {constexpr std::size_t kSaveSize=0x6900;}
namespace FfxHooks::GridLearned::Runtime {
std::recursive_mutex mutex;
State learned;
std::uintptr_t base=0x400000;
std::atomic<bool> enabled{true};
DWORD ownerThread=0;
std::uint64_t loadSerial=0,readSerial=0;
unsigned loadDepth=0;
bool PublisherReady() noexcept{return enabled.load();}
void RequestStop() noexcept{enabled=false;}
void Emit(const char*) noexcept{}
enum class RecordRead{Found,Missing,Invalid,Unavailable};
struct Store {
 RecordRead result=RecordRead::Found;unsigned reads=0;
 RecordRead Read(const Identity& id,Learned& words){++reads;words={};
  if(result==RecordRead::Found)words[0][(330-96)/16]=static_cast<std::uint16_t>(1u<<((330-96)%16));
  if(id.path[0]==2)words[0]={};
  return result;
 }
} store;
bool PayloadHash(const unsigned char* source,std::size_t size,Hash& hash){
 if(!source||size!=RonsoPool::kSaveSize)return false;
 hash.fill(37);return true;
}
'''
POST=r'''
}
namespace G=FfxHooks::GridLearned::Runtime;
using namespace FfxHooks::GridLearned;
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",why);}}
unsigned char a[16]{},b[16]{},unknown[16]{};
void* Destination(){return reinterpret_cast<void*>(G::base+0xD2CA90);}
Identity Id(unsigned value){Identity id{};id.path.fill(static_cast<unsigned char>(value));id.image.fill(19);return id;}
void Reset(){G::pending={};G::learned.Clear();G::enabled=true;G::ownerThread=G::loadDepth=0;G::loadSerial=G::readSerial=0;
 G::store.result=G::RecordRead::Found;G::store.reads=0;threadId=9;}
void Seed(unsigned index,const unsigned char* address,unsigned identity){
 auto& p=G::pending[index];p.identity=Id(identity);p.payload.fill(37);p.source=reinterpret_cast<std::uintptr_t>(address);p.serial=++G::readSerial;p.occupied=true;
}
int main(){
 Reset();Seed(0,a,1);Seed(1,b,2);
 auto attempt=G::BeforeLoad(Destination(),a);G::AfterLoad(attempt,true);
 Check(attempt.valid&&G::Ready()&&G::Has(0,330),"exact buffer selects its own save even when another save has identical bytes");
 Check(!G::Has(1,330),"learning remains character-specific");
 bool otherThreadReady=true;
 std::thread other([&](){threadId=17;otherThreadReady=G::Ready();});other.join();
 Check(!otherThreadReady&&G::Ready(),"real second thread cannot use the load owner's session");
 Reset();Seed(0,a,1);Seed(1,b,2);G::ReadStartingEvent(a);
 attempt=G::BeforeLoad(Destination(),a);G::AfterLoad(attempt,true);
 Check(!attempt.valid&&!G::Ready()&&G::store.reads==0,"short-read invalidation cannot fall back to another save's identical hash");
 Reset();Seed(0,b,1);attempt=G::BeforeLoad(Destination(),unknown);G::AfterLoad(attempt,true);
 Check(!attempt.valid&&!G::Ready()&&G::store.reads==0,"unobserved copied buffer cannot manufacture save identity from content alone");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::ReadStartingEvent(a);G::AfterLoad(attempt,true);
 Check(!G::Ready()&&!G::Has(0,330),"read attempt during load revokes the captured provenance before session admission");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);Seed(0,a,2);G::AfterLoad(attempt,true);
 Check(!G::Ready(),"same pointer with a newer read serial cannot accept an older load attempt");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::AfterLoad(attempt,false);
 Check(!G::Ready()&&G::loadDepth==0,"interrupted native copy never admits inherited commands");
 Reset();Seed(0,a,1);Seed(1,b,2);
 const auto outer=G::BeforeLoad(Destination(),a);const auto inner=G::BeforeLoad(Destination(),b);
 G::AfterLoad(inner,true);G::AfterLoad(outer,true);
 Check(!G::Ready()&&G::loadDepth==0,"nested load invalidates both ambiguous attempts");
 for(const auto result:{G::RecordRead::Invalid,G::RecordRead::Unavailable}){
  Reset();Seed(0,a,1);G::store.result=result;attempt=G::BeforeLoad(Destination(),a);G::AfterLoad(attempt,true);
  Check(!attempt.valid&&!G::Ready(),"invalid or unreadable sidecar does not inherit commands");
 }
 Reset();Seed(0,a,1);G::store.result=G::RecordRead::Missing;attempt=G::BeforeLoad(Destination(),a);G::AfterLoad(attempt,true);
 Check(G::Ready()&&!G::Has(0,330),"missing sidecar admits an empty identity-bound session");
 attempt=G::BeforeLoad(reinterpret_cast<void*>(0x1234),a);G::AfterLoad(attempt,true);
 Check(!attempt.tracked&&G::Ready(),"preview destination leaves the already admitted session untouched");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::RequestStop();G::AfterLoad(attempt,true);
 Check(!G::Ready()&&!G::Has(0,330),"stop during native copy closes post-load admission");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::ReadStartingEvent(b);G::AfterLoad(attempt,true);
 Check(G::Ready()&&G::Has(0,330),"unrelated buffer invalidation does not cancel a proven load");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);
 std::thread completing([&](){threadId=17;G::AfterLoad(attempt,true);});completing.join();
 Check(!G::Ready()&&!G::Has(0,330),"another thread cannot complete the captured load owner's admission");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::pending[0]={};G::AfterLoad(attempt,true);
 Check(!G::Ready(),"evicted completed-read record revokes in-flight load admission");
 Reset();Seed(0,a,1);attempt=G::BeforeLoad(Destination(),a);G::pending[0].identity=Id(2);G::AfterLoad(attempt,true);
 Check(!G::Ready(),"identity must still match even if a malformed producer reuses a read serial");
 std::printf("GridLoadProvenanceRt1: %u/%u passed (actual admission, simulated hash/store/OS IDs)\n",checks-failures,checks);
 return failures?1:0;
}
'''
def main():
 root=Path(__file__).resolve().parents[2]
 source=(root/'src/runtime/FfxHooksDll/hooks/GridLearnedRuntime.h').read_text()
 pending=source[source.index('struct Pending {'):source.index('inline void Emit(')]
 read_start=source[source.index('inline void ReadStartingEvent('):source.index('inline void ReadEvent(')]
 load=source[source.index('struct LoadAttempt {'):source.index('inline int __cdecl LoadShim(')]
 access=source[source.index('inline bool Ready()'):source.index('inline Change Set(')]
 compiler=shutil.which(os.environ.get('CXX','g++'))
 if not compiler:raise SystemExit('C++17 compiler required')
 with tempfile.TemporaryDirectory(prefix='ffx-grid-load-provenance-') as temp:
  cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test';cpp.write_text(PRE+pending+read_start+load+access+POST)
  subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-I',str(root),str(cpp),'-o',str(exe)],check=True,timeout=60)
  subprocess.run([str(exe)],check=True,timeout=15)
if __name__=='__main__':main()
