#include "../hooks/SinAiCore.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
namespace A=FfxHooks::SinAi;
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static std::vector<std::uint8_t> Read(const char* name){std::ifstream file(name,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
struct Kernel {std::vector<std::uint8_t> bytes;};
static bool ReadKernel(void* context,std::uintptr_t at,void* output,std::size_t length){
    auto& k=*static_cast<Kernel*>(context);
    if(at==0x400000+A::kMonster2RootRva && length==4){const std::uint32_t pointer=0x10000000;std::memcpy(output,&pointer,4);return true;}
    if(at<0x10000000 || length>k.bytes.size() || at-0x10000000>k.bytes.size()-length)return false;
    std::memcpy(output,k.bytes.data()+at-0x10000000,length);return true;
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    auto bytes=Read(argv[1]);A::Pack pack;
    Check(pack.Load(bytes.data(),bytes.size())==A::PackCode::Ok && pack.ready,"complete canonical profile pack loads");
    for(const auto& proof:A::kProofs){
        const auto* view=pack.Find(proof.monster,proof.curse);
        Check(view && view->proof->hash==proof.hash && A::U32(view->bytes+0x10)<=proof.size,"every permitted actor/curse has a bounded exact AI view");
    }
    Check(!pack.Find(999,1)&&!pack.Find(3,8),"unknown monster and incompatible curse cannot select a profile");
    unsigned monster=0;
    Check(A::MonsterName("m003",&monster)&&monster==3,"native registration label resolves exactly");
    Check(!A::MonsterName("m003x",&monster)&&!A::MonsterName("x003",&monster)&&!A::MonsterName("m3",&monster),"ambiguous labels cannot acquire a script");
    auto corrupt=bytes;corrupt.back()^=1;
    Check(pack.Load(corrupt.data(),corrupt.size())==A::PackCode::InvalidPayload&&!pack.ready,"modified bytecode is rejected");
    Check(pack.Load(bytes.data(),bytes.size()-1)!=A::PackCode::Ok,"truncated last profile is rejected");
    corrupt=bytes;corrupt.push_back(0);
    Check(pack.Load(corrupt.data(),corrupt.size())==A::PackCode::InvalidSize,"trailing records are rejected");
    corrupt=bytes;corrupt[12+4]^=1;
    Check(pack.Load(corrupt.data(),corrupt.size())==A::PackCode::InvalidPayload,"source AI fingerprint cannot be changed");
    corrupt=bytes;corrupt[12+2]=8;
    Check(pack.Load(corrupt.data(),corrupt.size())==A::PackCode::UnknownProfile,"a forbidden curse cannot be relabeled onto another monster");
    Kernel kernel{Read(argv[2])};
    Check(A::CommandsReady({&kernel,ReadKernel},0x400000),"the installed Spira Reforge command records match their native dependencies");
    const auto saved=kernel.bytes;kernel.bytes[10]=0;kernel.bytes[11]=1;
    Check(!A::CommandsReady({&kernel,ReadKernel},0x400000),"missing indices never use the game's silent row-zero fallback");
    kernel.bytes=saved;kernel.bytes[20+268*92+20]^=1;
    Check(!A::CommandsReady({&kernel,ReadKernel},0x400000),"a modified command dependency disables script admission");
    kernel.bytes.resize(20);
    Check(!A::CommandsReady({&kernel,ReadKernel},0x400000),"short/unreadable command records fail closed");
    std::printf("SinAiRt0: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
