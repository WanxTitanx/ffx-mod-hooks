#include "../hooks/NativeSaveEvents.h"
#include <cstdio>
#include <thread>
#include <vector>
using namespace FfxHooks::NativeSaveEvents;
static unsigned reads,writes,failures,checks;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static void Read(const wchar_t*,const unsigned char* disk,const unsigned char* loaded,std::size_t size) noexcept {
    if(size==3 && disk[0]==1 && loaded[0]==2)++reads;
}
static void Write(const wchar_t*,const unsigned char* actual,std::size_t size) noexcept {
    if(size==3 && actual[0]==2)++writes;
}
int main(){
    const unsigned char disk[]={1,3,4},loaded[]={2,3,4};
    const Observer observer{Read,Write},foreign{Read,Write};
    Check(!Requested(),"save observer starts OFF");
    ReadCompleted(L"ffx_000",disk,loaded,3);WriteCompleted(L"ffx_000",loaded,3);
    Check(reads==0 && writes==0,"OFF cannot alter or consume native I/O");
    Check(Subscribe(&observer) && Requested(),"one owner can request the existing I/O producer");
 Check(Subscribe(&foreign),"Workshop and Arcana can subscribe without replacing each other");
    ReadCompleted(L"ffx_000",disk,loaded,3);WriteCompleted(L"ffx_000",loaded,3);
 Check(reads==2 && writes==2,"both observers receive original disk and actual published bytes once");
    Unsubscribe(&foreign);Check(Requested(),"foreign teardown cannot detach the owner");
    Unsubscribe(&observer);Unsubscribe(&observer);
    ReadCompleted(L"ffx_000",disk,loaded,3);WriteCompleted(L"ffx_000",loaded,3);
 Check(!Requested() && reads==2 && writes==2,"idempotent stop closes future callback admission");
 Check(Subscribe(&foreign) && Subscribe(&observer),"reverse registration order is supported");
 Check(Subscribe(&observer),"repeated registration is idempotent");
 ReadCompleted(L"ffx_000",disk,loaded,3);
 Check(reads==4,"duplicate registration does not duplicate delivery");
 Unsubscribe(&observer);
 WriteCompleted(L"ffx_000",loaded,3);
 Check(writes==3 && Requested(),"stopping first subscriber preserves the second");
 Unsubscribe(&foreign);
 const Observer invalid{Read,nullptr};
 Check(!Subscribe(nullptr) && !Subscribe(&invalid),"invalid subscription cannot consume capacity");
 Observer slots[9]{};
 for(auto& slot:slots)slot={Read,Write};
 for(unsigned i=0;i<8;++i)Check(Subscribe(&slots[i]),"bounded subscriber slot is available");
 Check(!Subscribe(&slots[8]),"ninth subscriber is refused without replacing an owner");
 const unsigned before=reads;
 ReadCompleted(L"ffx_000",disk,loaded,3);
 Check(reads==before+8,"each admitted subscriber receives the event exactly once");
 Unsubscribe(&slots[3]);
 Check(Subscribe(&slots[8]),"released capacity can be reused");
 for(auto& slot:slots)Unsubscribe(&slot);
 std::vector<std::thread> workers;
 for(unsigned i=0;i<12;++i)workers.emplace_back([&]{Subscribe(&observer);});
 for(auto& worker:workers)worker.join();
 const unsigned beforeConcurrent=reads;
 ReadCompleted(L"ffx_000",disk,loaded,3);
 Check(reads==beforeConcurrent+1,"concurrent same-owner registration stays unique");
 Unsubscribe(&observer);
 Check(!Requested(),"all subscriptions can stop independently");
 const Observer checkpoint=[](){Observer value{Read,Write};
     value.selectRead=[](const wchar_t*,const unsigned char*,unsigned char* chosen,std::size_t size,CheckpointSelection* selection) noexcept {
         if(size!=3)return false;
         chosen[0]=9;chosen[1]=3;chosen[2]=4;selection->selected=true;return true;};
     value.checkpointRead=[](const wchar_t*,const unsigned char* original,const unsigned char* runtime,std::size_t size,const CheckpointSelection& selection) noexcept {
         Check(size==3&&original[0]==1&&runtime[0]==2&&selection.selected,"checkpoint owner retains the original disk anchor");};return value;}();
 const Observer ordinary{
     [](const wchar_t*,const unsigned char* selected,const unsigned char* runtime,std::size_t size) noexcept {
         Check(size==3&&selected[0]==9&&runtime[0]==2,"other save owners bind to the selected canonical checkpoint bytes");},Write};
 auto rival=checkpoint;unsigned char chosen[3]{};CheckpointSelection selection{};
 Check(Subscribe(&checkpoint)&&Subscribe(&ordinary)&&!Subscribe(&rival),"one checkpoint selector coexists with independent save observers");
 Check(SelectRead(L"ffx_000",disk,chosen,3,&selection)&&selection.selected,"native selection runs the owning checkpoint callback");
 ReadCompleted(L"ffx_000",disk,loaded,3,&selection,chosen);
 Unsubscribe(&checkpoint);Unsubscribe(&ordinary);
    std::printf("NativeSaveEventsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
