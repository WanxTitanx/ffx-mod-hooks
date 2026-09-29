// Actual adapter/publisher with simulated endpoints; no game or save filesystem.
#include <cstdio>
#if !__has_include("../hooks/NativeSaveCommitAdapter.h")
int main(){std::puts("FAIL: verified save callback adapter is not implemented");return 1;}
#else
#include "../hooks/NativeSaveCommitAdapter.h"
#include <memory>
using namespace FfxHooks;
using namespace NativeSaveCommit;
static unsigned checks,failures,closes,staged,verifiedCount,aborted;
static std::uint32_t epoch=1,thread=7,error=13,seenError=0;
static int number=17,seenNumber=0,closeResult=0;
static bool ready=true,readbackOk=true,changedDuringClose=false;
static bool fileOk=true;
static FileIdentity currentFile{17,1234},diskFile{17,1234};
static RonsoPool::SaveImage image{},disk{};
static std::uint64_t stagedSerial=0;
static void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL: %s\n",text);}}
static bool Ready() noexcept{return ready;}
static std::uint32_t Epoch() noexcept{return epoch;}
static std::uint32_t Thread() noexcept{return thread;}
static std::uint32_t Error() noexcept{return error;}
static void SetError(std::uint32_t value) noexcept{error=value;}
static int* Number() noexcept{return &number;}
static bool File(void*,FileIdentity& out) noexcept{out=currentFile;error=51;number=52;return fileOk&&out.Valid();}
static bool Readback(const wchar_t*,FileIdentity& out,RonsoPool::SaveImage& bytes) noexcept{
    out=diskFile;bytes=disk;error=81;number=82;return readbackOk;
}
static void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept{}
static void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept{}
static void Staged(std::uint64_t serial,const wchar_t*,const unsigned char*,std::size_t) noexcept{
    ++staged;stagedSerial=serial;error=91;number=92;
}
static void Verified(std::uint64_t serial,const wchar_t*,const unsigned char*,std::size_t) noexcept{
    if(serial==stagedSerial&&closes==1)++verifiedCount;
    error=93;number=94;
}
static void Aborted(std::uint64_t) noexcept{++aborted;error=95;number=96;}
static int NativeClose(void*){
    ++closes;seenError=error;seenNumber=number;error=61;number=62;
    if(changedDuringClose)++epoch;
    return closeResult;
}
static Io Callbacks(){return {Ready,Epoch,Thread,File,Readback,Error,SetError,Number};}
static void Reset(){
    closes=staged=verifiedCount=aborted=0;epoch=1;thread=7;error=13;number=17;
    closeResult=0;ready=readbackOk=fileOk=true;changedDuringClose=false;
    currentFile=diskFile={17,1234};image={};RonsoPool::SealSave(image);disk=image;
}
int main(){
    NativeSaveEvents::Observer observer{Read,Write};
    observer.writeStaged=Staged;observer.writeVerified=Verified;observer.writeAborted=Aborted;
    Check(NativeSaveEvents::SubscribeAdditional(&observer),"verified observer uses existing publisher");
    for(unsigned mode=0;mode<9;++mode){
        Reset();auto runtime=std::make_unique<Runtime>();
        Check(runtime->Configure(Callbacks()),"adapter configured once");
        Check(!runtime->Configure(Callbacks()),"live callback replacement is refused");
        auto ticket=runtime->Stage(reinterpret_cast<void*>(0x1000),L"C:\\fixture\\ffx_000",image);
        Check(ticket.Valid()&&staged==1&&verifiedCount==0,"buffering does not publish verified persistence");
        Check(error==13&&number==17,"staged observers preserve caller errors");
        runtime->Finish(ticket,true);
        if(mode==0)closeResult=-1;
        if(mode==1)readbackOk=false;
        if(mode==2)disk[64]^=1;
        if(mode==3)++diskFile.index;
        if(mode==4)++currentFile.index;
        if(mode==5)changedDuringClose=true;
        if(mode==6)runtime->Stop();
        if(mode==8)fileOk=false;
        error=13;number=17;
        Check(runtime->Close(reinterpret_cast<void*>(0x1000),NativeClose)==closeResult,"native close result preserved");
        Check(closes==1&&seenError==13&&seenNumber==17,"original close executes once with caller errors");
        Check(error==61&&number==62,"readback and observers preserve native errors");
        Check(verifiedCount==(mode==7?1u:0u),"verification requires successful matching close/readback");
    }
    Reset();auto runtime=std::make_unique<Runtime>();runtime->Configure(Callbacks());runtime->Stop();
    Check(!runtime->Stage(reinterpret_cast<void*>(0x1000),L"C:\\fixture\\ffx_000",image).Valid(),"stop prevents new snapshots");
    Check(staged==0&&verifiedCount==0,"OFF cannot write persistence");
    Reset();runtime=std::make_unique<Runtime>();runtime->Configure(Callbacks());
    auto first=runtime->Stage(reinterpret_cast<void*>(0x1000),L"C:\\fixture\\ffx_000",image);
    runtime->Finish(first,true);
    fileOk=false;
    Check(!runtime->Stage(reinterpret_cast<void*>(0x1000),L"C:\\fixture\\ffx_000",image).Valid(),"failed restaging is refused");
    fileOk=true;
    runtime->Close(reinterpret_cast<void*>(0x1000),NativeClose);
    Check(verifiedCount==0&&aborted>=1,"failed restaging invalidates previous buffered metadata");
    NativeSaveEvents::Observer incomplete{Read,Write};incomplete.writeVerified=Verified;
    Check(!NativeSaveEvents::SubscribeAdditional(&incomplete),"partial verified callback contract is refused");
    NativeSaveEvents::UnsubscribeAdditional(&observer);
    std::printf("NativeSaveCommitAdapterRt0: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
#endif
