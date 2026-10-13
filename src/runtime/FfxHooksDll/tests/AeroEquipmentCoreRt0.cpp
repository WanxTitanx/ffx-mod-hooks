#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
#if __has_include("../hooks/AeroEquipmentCore.h")
#include "../hooks/AeroEquipmentCore.h"
namespace A=FfxHooks::AeroEquipment;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* label){++checks;if(!v){++failures;std::printf("FAIL %s\n",label);}}
static void Word(unsigned char* p,unsigned value){p[0]=static_cast<unsigned char>(value);p[1]=static_cast<unsigned char>(value>>8);}
static std::vector<unsigned char> Table(bool numeric){
    std::vector<unsigned char> data(20+181*108+1);Word(data.data(),1);Word(data.data()+10,180);Word(data.data()+12,108);Word(data.data()+14,181*108);data[16]=20;
    const auto pool=data.size()-1;
    for(unsigned effect=0;effect<6;++effect){
        auto* row=data.data()+20+(175+effect)*108;Word(row,static_cast<unsigned>(data.size()-pool));std::memcpy(row+16,A::Payloads[effect].data(),92);
        char digits[8]{};std::snprintf(digits,sizeof(digits),"%u",175+effect);const auto* name=numeric?digits:A::Names[effect];
        for(unsigned n=0;name[n];++n)data.push_back(static_cast<unsigned char>(numeric?name[n]:A::Glyph(name[n])));
        data.push_back(0);
    }
    return data;
}
int main(int argc,char** argv){
    auto latin=Table(false),numeric=Table(true);
    Check(A::Validate(latin.data(),latin.size())&&A::Validate(numeric.data(),numeric.size()),"Latin and numeric DATA rows bind all six Aero identities");
    for(unsigned effect=0;effect<6;++effect)for(unsigned byte=16;byte<108;++byte){
        auto changed=latin;changed[20+(175+effect)*108+byte]^=1;Check(!A::Validate(changed.data(),changed.size()),"any native payload change closes Aero admission");
    }
    auto changed=latin;changed[20+181*108+1]^=1;Check(!A::Validate(changed.data(),changed.size()),"a foreign name cannot borrow a matching marker payload");
    Word(changed.data()+10,174);Check(!A::Validate(changed.data(),changed.size()),"old banks do not enable nonexistent Aero equipment");
    Check(!A::Validate(nullptr,0)&&!A::Validate(latin.data(),20),"truncated tables fail closed");
    unsigned successes=0;for(unsigned roll=0;roll<100;++roll)successes+=A::Touch(true,true,0,roll)?1u:0u;
    Check(successes==50&&A::Touch(true,true,0,49)&&!A::Touch(true,true,0,50),"Touch has exactly fifty accepted outcomes per hundred");
    Check(!A::Touch(false,true,0,0)&&!A::Touch(true,false,0,0)&&!A::Touch(true,true,0,100),"ineligible, unequipped and failed RNG do not proc");
    Check(!A::Touch(true,true,0x40,0),"permanent Aero Strike cannot receive a second elemental proc");
    Check(A::Sos(true,true,1)==0x40&&A::Sos(true,true,2)==0x40,"SOS uses the two actual native critical conditions");
    Check(!A::Sos(true,true,0)&&!A::Sos(true,true,3)&&!A::Sos(false,true,1)&&!A::Sos(true,false,1),"SOS stops outside critical, while dead or without equipment");
    for(int i=1;i<argc;++i){std::ifstream stream(argv[i],std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),{});
        Check(A::Validate(bytes.data(),bytes.size()),argv[i]);}
    std::printf("AERO_EQUIPMENT_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL Aero equipment runtime contract is missing");return 1;}
#endif
