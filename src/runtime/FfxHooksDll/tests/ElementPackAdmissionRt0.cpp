// Jarvis-HOOK: admission binds authored fingerprints to exact loaded bank bytes.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if __has_include("../hooks/ElementPackAdmission.h")
#include "../hooks/ElementPackAdmission.h"
namespace E=FfxHooks::ElementalDominion;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void W16(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
static void W32(unsigned char* p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
// This deterministic fixture digest is injected only into the portable boundary
// test; the native adapter must provide its existing SHA-256 implementation.
static bool Digest(const unsigned char* data,std::size_t size,std::array<unsigned char,32>& out) noexcept {
    out={};for(std::size_t i=0;i<size;++i)out[i%32]^=static_cast<unsigned char>(data[i]+i);
    return true;
}
static std::string Hash(const unsigned char* data,std::size_t size){
    std::array<unsigned char,32> digest{};Digest(data,size,digest);std::string text;
    for(auto b:digest){text.push_back("0123456789abcdef"[b>>4]);text.push_back("0123456789abcdef"[b&15]);}return text;
}
int main(){
    std::vector<unsigned char> bytes(20+3*96+12);W16(bytes.data(),1);W16(bytes.data()+10,2);
    W16(bytes.data()+12,96);W16(bytes.data()+14,3*96);W32(bytes.data()+16,20);
    bytes[20+96+0x20]=2;bytes[20+96+0x23]=1;bytes[20+96+0x28]=4;
    E::Pack pack{};pack.packageId="tests.admission";pack.version=1;
    E::PackBank bank{};bank.key="table.command";bank.kind=E::BankKind::Command;bank.locale="us";
    bank.bytes=static_cast<unsigned>(bytes.size());bank.sha256=Hash(bytes.data(),bytes.size());
    bank.sections.push_back({0,2,96,20});pack.banks.push_back(bank);
    E::CommandBinding command{};command.key="spell.example";command.bank=0;command.index=1;
    command.encoded=0x3001;command.rowSha256=Hash(bytes.data()+116,96);pack.commands.push_back(command);
    E::LoadedBank loaded{E::BankKind::Command,"us",bytes.data(),bytes.size()};
    E::PackAdmission admission;E::AdmissionProblem problem{};
    Check(admission.Prepare(pack,&loaded,1,Digest,problem),"verified bank and row admit together");
    Check(admission.Size()==1&&admission.Command(0x3001,bytes.data()+116,96),"exact encoded command and loaded row resolve");
    Check(!admission.Command(0x3001,bytes.data()+20,96),"same ID cannot borrow another row pointer");
    Check(!admission.Command(1,bytes.data()+116,96)&&!admission.Command(0x4001,bytes.data()+116,96),"table group and encoded identity are mandatory");
    Check(!admission.Command(0x3001,bytes.data()+116,95),"a truncated row cannot match admission");
    auto copy=bytes;
    Check(!admission.Command(0x3001,copy.data()+116,96),"a same-byte replacement bank requires fresh admission");
    E::LoadedBank copied{E::BankKind::Command,"us",copy.data(),copy.size(),bytes.data()};
    E::PackAdmission safe;
    Check(safe.Prepare(pack,&copied,1,Digest,problem),"an immutable safe bank copy can retain its original native identity");
    Check(safe.Command(0x3001,bytes.data()+116,copy.data()+116,96)!=nullptr,
          "native identity and bounded safe row bytes are validated separately");
    Check(!safe.Command(0x3001,copy.data()+116,copy.data()+116,96),
          "the scratch copy cannot pretend to be the admitted live command address");
    bytes[117]^=1;
    Check(!admission.Command(0x3001,bytes.data()+116,96),"in-place command changes invalidate the cached proof");bytes[117]^=1;
    const auto good=admission;
    auto invalid=pack;invalid.banks[0].sha256[0]=invalid.banks[0].sha256[0]=='0'?'1':'0';
    Check(!admission.Prepare(invalid,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::BankHash,"whole-bank fingerprint mismatch rejects admission");
    Check(admission.Command(0x3001,bytes.data()+116,96)!=nullptr,"rejected candidate preserves the previous admission");
    invalid=pack;invalid.commands[0].rowSha256[0]=invalid.commands[0].rowSha256[0]=='0'?'1':'0';
    Check(!admission.Prepare(invalid,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::RowHash,"row hash is checked independently of its bank");
    loaded.locale="jp";
    Check(!admission.Prepare(pack,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::Locale,"wrong loaded locale cannot borrow another locale's hashes");loaded.locale="us";
    --loaded.size;
    Check(!admission.Prepare(pack,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::Size,"declared and loaded lengths must match exactly");++loaded.size;
    Check(!admission.Prepare(pack,nullptr,0,Digest,problem),"missing bank never yields a partially active pack");
    Check(!admission.Prepare(pack,&loaded,1,nullptr,problem),"no hashing backend means no admission");
    std::array<E::LoadedBank,2> duplicates{loaded,loaded};
    Check(!admission.Prepare(pack,duplicates.data(),2,Digest,problem)&&problem.code==E::AdmissionError::Duplicate,"duplicate loaded bank identities are rejected");
    for(unsigned offset:{0u,8u,10u,12u,14u,16u,18u}){
        bytes[offset]^=1;invalid=pack;invalid.banks[0].sha256=Hash(bytes.data(),bytes.size());
        Check(!admission.Prepare(invalid,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::Layout,
              "authored hash cannot authorize malformed native section metadata");bytes[offset]^=1;
    }
    invalid=pack;invalid.commands[0].encoded=0x4001;
    Check(!admission.Prepare(invalid,&loaded,1,Digest,problem),"compiled bindings are cross-checked before pointer publication");
    invalid=pack;invalid.commands.push_back(invalid.commands[0]);
    Check(!admission.Prepare(invalid,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::Duplicate,"duplicate command identities fail at the admission boundary too");
    Check(admission.Prepare(pack,&loaded,1,Digest,problem)&&problem.code==E::AdmissionError::None,"valid retry clears the previous error");
    const auto* accepted=admission.Command(0x3001,bytes.data()+116,96);
    Check(accepted&&accepted->bindingIndex==0&&accepted->width==96&&accepted->row[0x28]==4,"admitted immutable row retains its exact payload");
    admission.Clear();Check(!admission.Size()&&!admission.Command(0x3001,bytes.data()+116,96),"closing admission removes every published binding");
    Check(good.Command(0x3001,bytes.data()+116,96)!=nullptr,"independent candidate storage is not accidentally shared");
    std::printf("ELEMENT_PACK_ADMISSION_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementPackAdmission.h is missing");return 1;}
#endif
