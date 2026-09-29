#!/usr/bin/env python3
"""Execute the actual GridTeach read-attempt handler with real event fanout.
Publisher readiness/stop/logging are doubles; a pthread lock error is injected.
Requires a Linux linker with --wrap. No game or save I/O occurs.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

PREAMBLE=r'''
#include "src/runtime/FfxHooksDll/hooks/GridLearnedCore.h"
#include "src/runtime/FfxHooksDll/hooks/NativeSaveEvents.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <pthread.h>
#include <cerrno>
bool failNextMutex=false;
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t*);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* lock){
 if(failNextMutex){failNextMutex=false;return EINVAL;}
 return __real_pthread_mutex_lock(lock);
}
namespace FfxHooks::GridLearned::Runtime {
std::recursive_mutex mutex;
std::atomic<bool> ready{true};
unsigned errors=0;
bool PublisherReady() noexcept{return ready.load();}
void RequestStop() noexcept{ready.store(false);}
void Emit(const char*) noexcept{++errors;}
'''
TEST=r'''
}
namespace G=FfxHooks::GridLearned::Runtime;
namespace E=FfxHooks::NativeSaveEvents;
unsigned checks=0,failed=0,primaryReads=0,primaryWrites=0,primaryResets=0,attempts=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",why);}}
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept{++primaryReads;}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept{++primaryWrites;}
void Reset() noexcept{++primaryResets;}
void Attempt(const unsigned char* data) noexcept{++attempts;G::ReadStartingEvent(data);}
const E::Observer primary{Read,Write,Reset};
const E::Observer additional=[](){E::Observer value{Read,Write};value.readStarting=Attempt;return value;}();
void Seed(const unsigned char* a,const unsigned char* b){
 G::pending={};G::ready=true;
 for(unsigned i=0;i<64;++i){auto& p=G::pending[i];p.occupied=true;p.source=reinterpret_cast<std::uintptr_t>(i%2?a:b);
  p.serial=i+1;p.identity.path.fill(static_cast<unsigned char>(i+1));p.identity.image.fill(211);p.payload.fill(37);}
}
int main(){
 unsigned char a[8]{},b[8]{};
 Check(E::Subscribe(&primary),"existing three-field primary remains valid");
 Check(E::SubscribeAdditional(&additional)&&E::SubscribeAdditional(&additional),"registration is idempotent");
 Seed(a,b);const auto before=G::pending;E::ReadStarting(a);
 Check(attempts==1,"one starting event for duplicate subscription");
 Check(primaryReads==0&&primaryWrites==0&&primaryResets==0,"attempt never impersonates primary completion or reset");
 for(unsigned i=0;i<64;++i){const auto& p=G::pending[i];
  if(i%2)Check(!p.occupied&&!p.source&&!p.serial&&p.payload==FfxHooks::GridLearned::Hash{},"every matching provenance is cleared");
  else Check(p.occupied&&p.source==before[i].source&&p.serial==before[i].serial&&p.payload==before[i].payload&&
       p.identity==before[i].identity,"unrelated save identity and serial preserved");
 }
 E::ReadStarting(nullptr);Check(attempts==1,"null address is not a reusable buffer");
 Seed(a,b);G::ready=false;E::ReadStarting(a);
 for(const auto& p:G::pending)Check(p.occupied,"stopped publisher performs no provenance mutation");
 G::ready=true;E::ReadStarting(b);
 for(unsigned i=0;i<64;++i)Check(G::pending[i].occupied==(i%2!=0),"the other buffer invalidates independently");
 E::UnsubscribeAdditional(&additional);const auto n=attempts;E::ReadStarting(a);
 Check(attempts==n,"unsubscribed future dispatch does not call handler");
 Check(E::SubscribeAdditional(&additional),"observer can register after ordinary unsubscribe");
 const auto snapshot=E::CaptureObservers();G::ready=false;E::UnsubscribeAdditional(&additional);Seed(a,b);G::ready=false;
 for(const auto* item:snapshot)if(item&&item->readStarting)item->readStarting(a);
 for(const auto& p:G::pending)Check(p.occupied,"captured callback checks readiness after unsubscribe and stop");
 Check(E::Subscribed(&primary),"Workshop observer registration remains unchanged");
 E::ReadCompleted(L"fixture",a,a,sizeof(a));E::WriteCompleted(L"fixture",a,sizeof(a));E::ResetCompleted();
 Check(primaryReads==1&&primaryWrites==1&&primaryResets==1,"original completion and reset callbacks still work");
 Check(G::errors==0,"no hidden exception during bounded invalidation");
 Seed(a,b);failNextMutex=true;G::ReadStartingEvent(a);
 Check(!G::ready.load()&&G::errors==1,"failed mutex acquisition closes learning instead of retaining admissible stale provenance");
 Check(!failNextMutex,"actual C++ mutex acquisition reached the injected OS failure");
 E::Unsubscribe(&primary);Check(!E::Requested(),"all explicit test observers retired");
 std::printf("GridReadAttemptRt0: %u/%u passed (actual handler and event fanout)\n",checks-failed,checks);
 return failed?1:0;
}
'''
def main():
    root=Path(__file__).resolve().parents[2]
    source=(root/'src/runtime/FfxHooksDll/hooks/GridLearnedRuntime.h').read_text()
    pending=source[source.index('struct Pending {'):source.index('inline void Emit(')]
    handler=source[source.index('inline void ReadStartingEvent('):source.index('inline void ReadEvent(')]
    compiler=shutil.which(os.environ.get('CXX','g++'))
    if not compiler:raise SystemExit('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='ffx-grid-read-attempt-') as temp:
        cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
        cpp.write_text(PREAMBLE+pending+handler+TEST)
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-Wl,--wrap=pthread_mutex_lock','-I',str(root),str(cpp),'-o',str(exe)],check=True,timeout=60)
        subprocess.run([str(exe)],check=True,timeout=15)
if __name__=='__main__':main()
