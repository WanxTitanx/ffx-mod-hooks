// Jarvis-HOOK: exact native formation preservation and bounded optional selection.
#include <cstdio>
#if __has_include("../hooks/OmegaUltimaCore.h")
#include "../hooks/OmegaUltimaCore.h"
#include <cstring>
namespace O=FfxHooks::OmegaUltima;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static void Put(std::vector<unsigned char>& v,unsigned at,unsigned n,unsigned width){for(unsigned i=0;i<width;++i)v[at+i]=static_cast<unsigned char>(n>>(i*8));}
static std::vector<unsigned char> Formation(unsigned index=0){
    std::vector<unsigned char> b(128,0xA5);Put(b,0,8,4);Put(b,12,48,4);Put(b,16,80,4);Put(b,32,128,4);
    const bool mimic=index>=13&&index<=16;
    for(unsigned i=0;i<8;++i)Put(b,60+2*i,i<(mimic?6u:5u)?0x10BCu+i:0xFFFF,2);
    if(mimic){Put(b,62,0x10D3,2);Put(b,64,O::ReservedMimic(index),2);}
    return b;
}
struct Rolls {unsigned calls=0,chance=0,slot=0;bool fail=false;};
static bool Draw(void* p,unsigned bound,unsigned& value) noexcept {
    auto& r=*static_cast<Rolls*>(p);++r.calls;if(r.fail)return false;
    value=r.calls==1?r.chance:r.slot;return value<bound;
}
int main(){
    Check(O::EligibleFormation(590,8)&&O::EligibleFormation(590,9)&&O::EligibleFormation(591,8),"observed native Omega fields are admitted");
    Check(!O::EligibleFormation(1076,8)&&!O::EligibleFormation(1077,8),"package identifiers are not native encounter authority");
    for(unsigned flag=0;flag<256;++flag)Check(O::UltimaResolved(static_cast<unsigned char>(flag))==bool(flag&2),"only the actual Ultima bit unlocks the clone");
    Check(!O::UltimaResolved(4)&&O::UltimaResolved(6),"Omega alone is not Ultima; unrelated bits do not remove completion");
    unsigned formations=0;
    for(unsigned field:{590u,591u})for(unsigned index=0;index<32;++index){
        const bool expected=(field==590?index<=18&&index!=10:index<=8);
        Check(O::EligibleFormation(field,index)==expected,"exact normal Omega catalog excludes both bosses and unused names");formations+=expected;
    }
    Check(formations==27&&!O::EligibleFormation(591,10)&&!O::EligibleFormation(1073,0),"only 27 normal Omega formations are admitted");
    auto b=Formation();O::FormationProof proof{1076,0,static_cast<unsigned>(b.size()),O::Hash(b.data(),b.size())};O::Layout layout;
    Check(O::Inspect(proof,b.data(),b.size(),layout)&&layout.eligible==31&&layout.slots==60,"five initial combatants are eligible");
    Put(b,60,0x1031,2);proof.hash=O::Hash(b.data(),b.size());
    Check(O::Inspect(proof,b.data(),b.size(),layout)&&layout.eligible==31,"Defender Z is eligible without a species whitelist");
    Put(b,62,0x10D3,2);proof.hash=O::Hash(b.data(),b.size());
    Check(O::Inspect(proof,b.data(),b.size(),layout)&&layout.eligible==29,"a chest is excluded wherever its active slot appears");
    for(unsigned i=13;i<=16;++i){b=Formation(i);proof={1076,i,static_cast<unsigned>(b.size()),O::Hash(b.data(),b.size())};
        Check(O::Inspect(proof,b.data(),b.size(),layout)&&layout.eligible==0x39,"all four transforming encounters preserve chest and reserved Mimic slots");
        Put(b,64,0x10BC,2);proof.hash=O::Hash(b.data(),b.size());Check(!O::Inspect(proof,b.data(),b.size(),layout),"a changed reveal contract fails closed");}
    b=Formation();proof={1076,0,128,O::Hash(b.data(),b.size())};O::Inspect(proof,b.data(),b.size(),layout);
    Rolls rolls;O::Random random{&rolls,Draw};
    Check(!O::Select(false,2,layout.eligible,random).selected&&!rolls.calls,"OFF draws nothing");
    Check(!O::Select(true,4,layout.eligible,random).selected&&!rolls.calls,"before Ultima completion no draw or replacement occurs");
    Check(!O::Select(true,2,0,random).selected&&!rolls.calls,"empty eligibility consumes no RNG");
    unsigned hits=0;for(unsigned chance=0;chance<100;++chance){rolls={0,chance,4,false};const auto p=O::Select(true,2,31,random);
        Check(p.selected==(chance<10)&&rolls.calls==(chance<10?2u:1u),"exact 10 percent tickets and one/two draw budget");if(p.selected){++hits;Check(p.slot==4,"slot selection uses its separate draw");}}
    Check(hits==10,"ten of one hundred incidence tickets select a clone");
    for(unsigned mask:{31u,29u,0x39u}){unsigned seen=0;
        for(unsigned n=0;n<O::Count(mask);++n){rolls={0,0,n,false};const auto p=O::Select(true,2,mask,random);Check(p.selected&&((1u<<p.slot)&mask)&&!(seen&(1u<<p.slot)),"every eligible ordinal selects one distinct slot uniformly");seen|=1u<<p.slot;}
        Check(seen==mask,"selection cannot reach chest/reserved/empty actors");}
    rolls={0,100,0,false};Check(!O::Select(true,2,31,random).selected&&rolls.calls==1,"invalid incidence output is rejected");
    rolls={0,0,5,false};Check(!O::Select(true,2,31,random).selected&&rolls.calls==2,"invalid slot output is rejected");
    rolls={0,0,0,true};Check(!O::Select(true,2,31,random).selected&&rolls.calls==1,"RNG failure preserves native encounter");
    for(unsigned slot=0;slot<5;++slot){std::vector<unsigned char> view{7};const auto before=b;
        Check(O::Build(proof,b.data(),b.size(),slot,view)&&view.size()==b.size()&&b==before,"replacement preserves original resource and extent");
        unsigned changed=0;for(unsigned i=0;i<b.size();++i)if(view[i]!=b[i]){++changed;Check(i==60+2*slot||i==61+2*slot,"only the selected formation WORD changes");}
        Check(changed&&O::Word(view.data()+60+2*slot)==0x138E,"native loader sees species 910 before monster initialization");}
    std::vector<unsigned char> output{7};
    Check(!O::Build(proof,b.data(),b.size(),7,output)&&output==std::vector<unsigned char>{7},"empty slot leaves output unchanged");
    b[40]^=1;Check(!O::Build(proof,b.data(),b.size(),0,output),"foreign formation bytes cannot inherit the approved binding");b[40]^=1;
    Put(b,60,0x138E,2);proof.hash=O::Hash(b.data(),b.size());Check(!O::Inspect(proof,b.data(),b.size(),layout),"an existing clone cannot produce a second clone");
    for(unsigned raw:{0u,9u,10u,99u,100u,0xFFFFFFFEu,0xFFFFFFFFu}){unsigned value=123;
        const bool ok=O::Bounded(raw,100,value);Check(!ok||value<100,"bounded entropy never leaves its requested interval");}
    unsigned value=0;Check(!O::Bounded(0,0,value)&&!O::Bounded(0xFFFFFFFF,100,value),"empty range and biased tail fail closed without another draw");
    std::printf("OmegaUltimaRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL Omega Ultima policy is not implemented");return 1;}
#endif
