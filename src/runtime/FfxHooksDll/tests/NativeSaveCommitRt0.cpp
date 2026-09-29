// Native save close/readback admission, without opening a game or real save.
#include <cstdio>
#if !__has_include("../hooks/NativeSaveCommitCore.h")
int main(){std::puts("FAIL: native close/readback admission is not implemented");return 1;}
#else
#include "../hooks/NativeSaveCommitCore.h"
#include <memory>
using namespace FfxHooks::NativeSaveCommit;
using namespace FfxHooks::RonsoPool;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL: %s\n",why);}}
int main(){
    auto tracker=std::make_unique<Tracker>();
    SaveImage image{};SealSave(image);
    const FileIdentity file{17,1234};
    auto begin=[&](std::uintptr_t stream=0x1000){return tracker->BeginWrite(stream,file,1,7,L"C:\\fixture\\ffx_000",image);};
    Check(!tracker->BeginWrite(0,file,1,7,L"C:\\fixture\\ffx_000",image).Valid(),"null stream refused");
    Check(!tracker->BeginWrite(0x1000,{},1,7,L"C:\\fixture\\ffx_000",image).Valid(),"missing file identity refused");
    auto ticket=begin();Check(ticket.Valid(),"complete candidate gets unique ticket");
    Check(!tracker->BeginClose(0x1000,file,1,7).valid,"close during incomplete write is rejected");
    Check(!tracker->FinishWrite(ticket,true),"cancelled write cannot become complete later");
    ticket=begin();Check(tracker->FinishWrite(ticket,false),"failed native write retires its ticket");
    Check(!tracker->BeginClose(0x1000,file,1,7).valid,"short write is never a commit");
    for(unsigned mode=0;mode<6;++mode){
        ticket=begin();Check(tracker->FinishWrite(ticket,true),"native write completed once");
        auto attempt=tracker->BeginClose(0x1000,file,1,7);Check(attempt.valid,"verified close candidate captured");
        auto actual=image;auto id=file;auto epoch=1u;auto thread=7u;bool closed=true;
        if(mode==0)closed=false;
        if(mode==1)actual[64]^=1;
        if(mode==2)++id.index;
        if(mode==3)++epoch;
        if(mode==4)++thread;
        const bool accepted=tracker->CompleteClose(attempt,closed,id,epoch,thread,actual);
        Check(accepted==(mode==5),"only exact post-close identity/image/owner can commit");
        Check(!tracker->CompleteClose(attempt,true,file,1,7,image),"completion is never replayed");
    }
    for(unsigned mode=0;mode<3;++mode){
        ticket=begin();tracker->FinishWrite(ticket,true);
        auto id=file;if(mode==0)++id.index;
        Check(!tracker->BeginClose(0x1000,id,mode==1?2:1,mode==2?8:7).valid,"foreign close never acquires pending save");
    }
    ticket=begin();Check(!begin().Valid(),"overlapping write invalidates both candidates");
    Check(!tracker->FinishWrite(ticket,true),"overlapped ticket remains cancelled");
    ticket=begin();tracker->FinishWrite(ticket,true);auto old=tracker->BeginClose(0x1000,file,1,7);
    tracker->Reset();auto fresh=begin();Check(fresh.serial!=old.ticket.serial,"same FILE address gets a new serial");
    Check(!tracker->CompleteClose(old,true,file,1,7,image),"stale close cannot publish after reset");
    Check(tracker->FinishWrite(fresh,true),"stale close cannot cancel the fresh writer");
    tracker->Reset();
    for(unsigned i=0;i<Tracker::kCapacity;++i)Check(begin(0x1000+i).Valid(),"bounded independent stream admitted");
    Check(!begin(0x2000).Valid(),"pending save capacity is enforced");
    tracker->Reset();Check(!tracker->HasStream(0x1000),"reset clears all pending identities");
    std::printf("NativeSaveCommitRt0: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
#endif
