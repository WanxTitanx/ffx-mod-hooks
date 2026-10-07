#include "../hooks/SinProducerCore.h"
#include <array>
#include <cstdio>
#include <thread>
using namespace FfxHooks::SinProducer;
namespace {
int checks=0,failures=0,naturalCalls=0,nativeCalls=0;
int seenField=0,seenGroup=0,seenResult=0,seenId=0,seenOverkill=0,seenExtra=0;
float seenDistance=0;
std::uintptr_t seenCaller=0;
void* seenActor=nullptr;
const void* seenLoot=nullptr;
std::array<unsigned char,280> original{},view{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
void Natural(int field,int group,float distance,int result,std::uintptr_t caller){
    ++naturalCalls;seenField=field;seenGroup=group;seenDistance=distance;seenResult=result;seenCaller=caller;
}
const void* Reward(const void* loot){return loot==original.data()?view.data():loot;}
const void* NullReward(const void*){return nullptr;}
void Native(int id,void* actor,const void* loot,int overkill,int extra){
    ++nativeCalls;seenId=id;seenActor=actor;seenLoot=loot;seenOverkill=overkill;seenExtra=extra;
}
}
int main(){
    Slot slot;const Observers both{Natural,Reward},other{Natural,Reward},empty{},nullView{nullptr,NullReward};
    original.fill(7);view=original;view[0]=9;int actor=0;
    slot.Natural(330,2,1.5f,-1,0x871BB4);
    Check(naturalCalls==0 && slot.Reward(original.data())==original.data(),"default producer has no consumer or reward mutation");
    Check(!slot.Attach(nullptr) && !slot.Attach(&empty),"empty consumer cannot activate a producer");
    Check(slot.Attach(&both) && slot.Attach(&both),"the same resident consumer attaches idempotently");
    Check(!slot.Attach(&other),"a competing consumer cannot replace the installed consumer");
    slot.Natural(330,2,1.5f,-1,0x871BB4);
    Check(naturalCalls==1 && seenField==330 && seenGroup==2 && seenDistance==1.5f && seenResult==-1 && seenCaller==0x871BB4,
          "natural result and original caller survive producer composition exactly once");
    ForwardRewards(slot,Native,12,&actor,original.data(),1,0x12345678);
    Check(nativeCalls==1 && seenId==12 && seenActor==&actor && seenLoot==view.data() && seenOverkill==1 && seenExtra==0x12345678,
          "all five native reward arguments survive the read-only view transformation");
    Check(original[0]==7 && original[279]==7,"reward selection preserves native source bytes");
    slot.Detach(&other);
    Check(slot.Reward(original.data())==view.data(),"a foreign stop cannot detach the owner");
    std::thread stop([&]{slot.Detach(&both);});stop.join();
    slot.Natural(330,2,1.5f,-1,0x871BB4);
    ForwardRewards(slot,Native,13,&actor,original.data(),0,17);
    Check(naturalCalls==1 && nativeCalls==2 && seenLoot==original.data() && seenExtra==17,
          "stop removes notifications and preserves the original reward consumer");
    Check(slot.Attach(&nullView) && slot.Reward(original.data())==original.data(),"an absent consumer view falls back to the original input");
    slot.Detach(&nullView);
    std::array<unsigned char,5> jump{0xE9,0,0,0,0};
    const std::uint32_t site=0x780D10,target=0x34501234,delta=target-(site+5u);
    std::memcpy(jump.data()+1,&delta,sizeof(delta));
    Check(OwnsJump(jump.data(),site,target),"an exact x86 jump identifies the installed producer");
    Check(!OwnsJump(jump.data(),site,target+1),"a foreign producer target fails ownership admission");
    jump[0]=0x90;Check(!OwnsJump(jump.data(),site,target),"an unpatched entry is not treated as an owned hook");
    Check(!OwnsJump(nullptr,site,target),"unreadable bytes fail ownership admission");
    jump[0]=0xE9;
    const std::uint32_t back=0x400000-(site+5u);std::memcpy(jump.data()+1,&back,sizeof(back));
    Check(OwnsJump(jump.data(),site,0x400000),"backward x86 jumps use native 32-bit displacement arithmetic");
    Check(!OwnsJump(jump.data(),UINT32_MAX,target),"overflowing instruction addresses fail admission");
    std::printf("SinProducerRt0: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);
    return failures?1:0;
}
