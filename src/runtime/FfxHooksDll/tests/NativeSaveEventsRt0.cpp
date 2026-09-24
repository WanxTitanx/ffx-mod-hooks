#include "../hooks/NativeSaveEvents.h"
#include <cstdio>
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
    Check(!Subscribe(&foreign),"another owner cannot replace an active observer");
    ReadCompleted(L"ffx_000",disk,loaded,3);WriteCompleted(L"ffx_000",loaded,3);
    Check(reads==1 && writes==1,"observer receives original disk and actual published bytes once");
    Unsubscribe(&foreign);Check(Requested(),"foreign teardown cannot detach the owner");
    Unsubscribe(&observer);Unsubscribe(&observer);
    ReadCompleted(L"ffx_000",disk,loaded,3);WriteCompleted(L"ffx_000",loaded,3);
    Check(!Requested() && reads==1 && writes==1,"idempotent stop closes future callback admission");
    std::printf("NativeSaveEventsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
