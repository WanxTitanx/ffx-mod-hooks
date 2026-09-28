// Jarvis-HOOK: explicit monster profiles require native identity and file proof.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if __has_include("../hooks/ElementMonsterProof.h")
#include "../hooks/ElementMonsterProof.h"
namespace E=FfxHooks::ElementalDominion;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static void W32(unsigned char* p,unsigned value){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(value>>(i*8));}
static bool Hash(const unsigned char* p,std::size_t n,std::array<unsigned char,32>& out) noexcept {
    out={};for(std::size_t i=0;i<n;++i)out[i%32]^=static_cast<unsigned char>(p[i]+i);return true;
}
static std::string Hex(const std::vector<unsigned char>& bytes){
    std::array<unsigned char,32> hash{};Hash(bytes.data(),bytes.size(),hash);std::string out;
    for(auto ch:hash){out.push_back("0123456789abcdef"[ch>>4]);out.push_back("0123456789abcdef"[ch&15]);}return out;
}
int main(){
    std::vector<unsigned char> bytes(0x100);W32(bytes.data()+4,0x30);W32(bytes.data()+8,0x40);
    W32(bytes.data()+12,0x60);W32(bytes.data()+0x20,static_cast<unsigned>(bytes.size()));bytes[0x60]=7;
    E::ActorProfile profile{};profile.kind=E::ProfileKind::Monster;profile.id=0x1156;
    profile.fileBytes=static_cast<unsigned>(bytes.size());profile.fileSha256=Hex(bytes);
    E::MonsterProof proof;E::MonsterProofError error{};
    Check(proof.Admit(profile,18,0x1156,bytes.data(),bytes.data(),bytes.size(),Hash,error),"a named profile admits the exact native monster word and complete file");
    Check(proof.Matches(18,0x1156,bytes.data(),bytes.data(),bytes.size()),"current native header and stats retain their proved identity");
    Check(!proof.Matches(19,0x1156,bytes.data(),bytes.data(),bytes.size()),"another actor slot cannot borrow the proof");
    Check(!proof.Matches(18,342,bytes.data(),bytes.data(),bytes.size()),"raw native identity is not silently masked into a species alias");
    auto clone=bytes;
    Check(!proof.Matches(18,0x1156,clone.data(),clone.data(),clone.size()),"a new allocation with equal bytes requires fresh admission");
    auto invalid=profile;invalid.fileSha256[0]=invalid.fileSha256[0]=='0'?'1':'0';
    Check(!proof.Admit(invalid,18,0x1156,bytes.data(),bytes.data(),bytes.size(),Hash,error),"an explicit but wrong full-file fingerprint is rejected");
    Check(proof.Matches(18,0x1156,bytes.data(),bytes.data(),bytes.size()),"failed admission preserves the previous immutable proof");
    invalid=profile;invalid.kind=E::ProfileKind::Aeon;
    Check(!proof.Admit(invalid,18,0x1156,bytes.data(),bytes.data(),bytes.size(),Hash,error),"an Aeon label cannot authorize a monster profile");
    for(unsigned slot:{0u,7u,8u,17u,31u,255u})
        Check(!proof.Admit(profile,slot,0x1156,bytes.data(),bytes.data(),bytes.size(),Hash,error),"only native enemy slots can use this profile class");
    for(unsigned offset:{4u,8u,12u,0x20u}){
        auto corrupt=bytes;W32(corrupt.data()+offset,1);auto authored=profile;authored.fileSha256=Hex(corrupt);
        Check(!proof.Admit(authored,18,0x1156,corrupt.data(),corrupt.data(),corrupt.size(),Hash,error),"a matching hash cannot authorize malformed section boundaries");
    }
    Check(!proof.Admit(profile,18,0x1156,bytes.data(),bytes.data(),bytes.size()-1,Hash,error),"native length and declared length must agree");
    Check(!proof.Admit(profile,18,0x1156,bytes.data(),bytes.data(),bytes.size(),nullptr,error),"missing fingerprint backend closes admission");
    bytes[0x60]^=1;
    Check(!proof.Matches(18,0x1156,bytes.data(),bytes.data(),bytes.size()),"in-place stat changes immediately invalidate the bounded native view");bytes[0x60]^=1;
    bytes[4]^=1;
    Check(!proof.Matches(18,0x1156,bytes.data(),bytes.data(),bytes.size()),"changed header offsets cannot retain an old proof");bytes[4]^=1;
    proof.Clear();Check(!proof.Matches(18,0x1156,bytes.data(),bytes.data(),bytes.size()),"retiring an incarnation removes its profile permission");
    std::printf("ELEMENT_MONSTER_PROOF_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementMonsterProof.h is missing");return 1;}
#endif
