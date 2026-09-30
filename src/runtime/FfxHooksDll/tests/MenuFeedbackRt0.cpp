#include <cstdio>
#include <vector>
#include <thread>
#if __has_include("../../NativeMenuShell/MenuFeedback.h")
#include "../../NativeMenuShell/MenuFeedback.h"
using namespace FfxHooks::MenuAudio;
static thread_local std::vector<int> sounds;
static int checks=0,failures=0;
static void Emit(int id){sounds.push_back(id);}
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
int main(){
    Dispatch(1,Emit);Check(sounds==std::vector<int>{1},"unscoped callers retain immediate native feedback");sounds.clear();
    {Scope frame;Dispatch(1,Emit);Dispatch(1,Emit);Check(sounds.empty(),"feedback waits for final input outcome");}
    Check(sounds==std::vector<int>{1},"hover plus navigation plus confirm emits once");sounds.clear();
    {Scope frame;Dispatch(1,Emit);Dispatch(3,Emit);Dispatch(1,Emit);}
    Check(sounds==std::vector<int>{3},"error cannot be overwritten by optimistic movement");sounds.clear();
    {Scope frame;Dispatch(1,Emit);Dispatch(4,Emit);}
    Check(sounds==std::vector<int>{4},"cancel wins over navigation in the same callback");sounds.clear();
    {Scope frame;Dispatch(1,Emit);{Scope child;Dispatch(3,Emit);}Check(sounds.empty(),"nested callback does not flush early");}
    Check(sounds==std::vector<int>{3},"nested callbacks merge into one outcome");sounds.clear();
    {Scope frame;Dispatch(1,Emit);frame.Discard();}
    Check(sounds.empty(),"deferred action drops speculative navigation");
    {Scope frame;}
    Check(sounds.empty(),"idle callbacks remain silent");
    bool workerOwn=false;{Scope frame;Dispatch(1,Emit);std::thread worker([&]{Dispatch(4,Emit);workerOwn=sounds==std::vector<int>{4};});worker.join();}
    Check(workerOwn&&sounds==std::vector<int>{1},"independent threads never share pending sounds");sounds.clear();
    try{Scope frame;Dispatch(3,Emit);throw 1;}catch(int){}
    Dispatch(1,Emit);Check(sounds==std::vector<int>({3,1}),"unwinding restores the previous feedback owner");
    sounds.clear();
    {Scope outer;Dispatch(1,Emit);{Scope child;Dispatch(3,Emit);AbandonThreadFeedback();}}
    Check(sounds.empty(),"abandoned native exception scopes never publish or restore stale owners");
    {Scope nextFrame;Dispatch(4,Emit);}
    Check(sounds==std::vector<int>{4},"the next input frame works after native exception cleanup");
    std::printf("Menu feedback RT0: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL: menu input outcome arbitration is missing");return 1;}
#endif
