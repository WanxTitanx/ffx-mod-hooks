#include "../hooks/NativeSaveEvents.h"
#include <array>
#include <cstdio>
#include <cstring>
using namespace FfxHooks::NativeSaveEvents;
static unsigned checks=0,failures=0,prepared=0,committed=0,aborted=0;
static bool rejectProject=false,rejectPrepare=false;
static unsigned char token=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
static void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
static bool Required() noexcept {return true;}
static bool Project(const wchar_t*,const unsigned char* source,unsigned char* output,std::size_t n,void** cookie) noexcept {
    *cookie=&token;if(n!=3||source[0]!=9)return false;output[0]=1;return !rejectProject;
}
static bool Prepare(void* cookie,const wchar_t*,const unsigned char* output,std::size_t n) noexcept {
    if(cookie==&token&&n==3&&output[0]==1&&output[2]==7)++prepared;
    return !rejectPrepare;
}
static void Finish(void* cookie,const unsigned char* actual,std::size_t size,bool success) noexcept {
    if(cookie!=&token)return;
    if(success&&actual&&size==3&&actual[0]==1)++committed;else ++aborted;
}
int main(){
    const Observer owner{Read,Write,nullptr,Project,Prepare,Finish,Required};
    std::array<unsigned char,3> source{{9,2,3}},output{};WriteTransaction tx;
    Check(!ProjectionRequired(),"no active owner requires projection when OFF");
    Check(Subscribe(&owner)&&ProjectionRequired(),"temporary native fields require a projection-capable producer");
    Check(ProjectWrite(L"ffx_000",source.data(),output.data(),3,tx)&&source[0]==9&&output[0]==1,"projection edits a copied image, never live native bytes");
    output[2]=7;
    Check(PrepareWrite(L"ffx_000",output.data(),3,tx)&&prepared==1,"prepare observes final bytes after caller checksum sealing");
    Unsubscribe(&owner);
    FinishWrite(tx,output.data(),3,true);
    Check(committed==1&&!ProjectionRequired(),"an entered transaction finishes safely after owner admission closes");
    FinishWrite(tx,output.data(),3,true);Check(committed==1,"completion is idempotent");
    Subscribe(&owner);rejectProject=true;
    const auto before=aborted;
    Check(!ProjectWrite(L"ffx_000",source.data(),output.data(),3,tx)&&aborted==before+1&&output==source,"failed projection rolls back private output and releases its cookie");
    rejectProject=false;rejectPrepare=true;
    Check(ProjectWrite(L"ffx_000",source.data(),output.data(),3,tx),"new transaction can follow a rejected projection");
    const auto beforePrepare=aborted;
    Check(!PrepareWrite(L"ffx_000",output.data(),3,tx)&&aborted==beforePrepare+1,"prepare failure aborts the prepared metadata transaction");
    rejectPrepare=false;
    const auto beforeDestructor=aborted;
    {WriteTransaction pending;Check(ProjectWrite(L"ffx_000",source.data(),output.data(),3,pending),"pending cookie can be owned by an I/O work item");}
    Check(aborted==beforeDestructor+1,"discarding an I/O work item aborts its pending cookie");
    Check(!ProjectWrite(L"ffx_000",source.data(),source.data(),3,tx)&&source[0]==9,"in-place native-buffer projection is forbidden");
    Check(!ProjectWrite(L"ffx_000",source.data(),source.data()+1,2,tx),"overlapping source/output buffers are rejected");
    Unsubscribe(&owner);
    Check(ProjectWrite(L"ffx_000",source.data(),output.data(),3,tx)&&output==source,"OFF copies the native bytes exactly");
    Check(PrepareWrite(L"ffx_000",output.data(),3,tx),"OFF has no metadata preparation side effects");
    FinishWrite(tx,output.data(),3,true);
    std::printf("NativeSaveProjectionRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
