#include "../hooks/ElementScanSnapshotCache.h"
#include <cstdio>
#include <thread>
using namespace FfxHooks::ElementalScanView;
namespace {
unsigned checks=0,failures=0;thread_local std::uint64_t sourceGeneration=1;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL %s\n",message);}}
bool Source(unsigned actor,unsigned page,Snapshot& output) noexcept {
    if(actor!=18||page)return false;
    output={};output.generation=sourceGeneration;output.total=10;output.count=10;
    for(auto& row:output.rows){row.baseBp=10000;row.effectiveBp=12500;row.imperil=1;row.imperilTurns=3;}
    return true;
}
}
int main(){
    SnapshotCache cache;Snapshot snapshot{};
    Check(!cache.Copy(18,0,1,snapshot),"unpublished pages cannot advertise actor data");
    Check(cache.Publish(18,1,Source),"owner publishes a bounded admitted page set");
    bool copied=false;std::thread ui([&]{copied=cache.Copy(18,0,1,snapshot);});ui.join();
    Check(copied&&snapshot.rows[0].effectiveBp==12500&&snapshot.rows[0].imperilTurns==3,
          "a distinct render thread obtains the exact owner snapshot");
    Check(!cache.Copy(19,0,1,snapshot)&&!cache.Copy(18,1,1,snapshot),"other actors and unavailable pages fail closed");
    Check(!cache.Copy(18,0,2,snapshot),"retired generations cannot be shown");
    std::atomic<bool> done{false};std::atomic<unsigned> corrupted{0};
    std::thread reader([&]{while(!done.load()){Snapshot page{};
        if(cache.Copy(18,0,1,page)&&(page.count!=10||page.rows[0].effectiveBp!=12500||page.generation!=1))++corrupted;
    }});
    for(unsigned i=0;i<10000;++i)cache.Publish(18,1,Source);
    done=true;reader.join();Check(corrupted.load()==0,"concurrent publication never exposes a partially overwritten page");
    sourceGeneration=2;Check(cache.Publish(18,2,Source)&&cache.Copy(18,0,2,snapshot),"a new battle replaces the old page generation");
    std::printf("ELEMENT_SCAN_SNAPSHOT_CACHE_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
