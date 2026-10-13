#include "../hooks/SinNaturalFormation.h"
#include "../hooks/OmegaEncounter.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
namespace N=FfxHooks::SinNatural;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
struct Memory {std::vector<unsigned char> bytes;bool readable=true;};
static bool Read(void* p,std::uintptr_t at,void* output,std::size_t size){
    const auto& m=*static_cast<Memory*>(p);
    if(!m.readable||at<0x10000||at-0x10000>m.bytes.size()||size>m.bytes.size()-(at-0x10000))return false;
    std::memcpy(output,m.bytes.data()+at-0x10000,size);return true;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    Memory memory{{std::istreambuf_iterator<char>(file),{}}};if(memory.bytes.empty())return 2;
    N::FormationReader reader{&memory,Read};N::Evidence input{N::kCallerRva,-1,590,0,88,0,10};unsigned char asset=255;
    Check(N::ResolveFormation(reader,0x10000,input,asset)&&asset==11,"observed Omega slot ten resolves to ordinary asset eleven");
    Check(!FfxHooks::OmegaEncounter::Eligible(590,input.formation)&&FfxHooks::OmegaEncounter::Eligible(590,asset),"slot/asset confusion explains the old false boss exclusion");
    for(unsigned slot=0;slot<18;++slot){input.formation=static_cast<unsigned char>(slot);
        Check(N::ResolveFormation(reader,0x10000,input,asset)&&asset==(slot<10?slot:slot+1),"all eighteen first-floor selections follow their actual table entries");}
    input.group=input.selectedGroup=1;input.formation=0;
    Check(N::ResolveFormation(reader,0x10000,input,asset)&&asset==10&&!FfxHooks::OmegaEncounter::Eligible(590,asset),"actual Ultima boss group remains excluded");
    input.field=591;input.nativeFieldRow=89;input.group=input.selectedGroup=0;
    for(unsigned slot=0;slot<9;++slot){input.formation=static_cast<unsigned char>(slot);
        Check(N::ResolveFormation(reader,0x10000,input,asset)&&asset==slot,"second-floor identity entries remain unchanged");}
    input.group=input.selectedGroup=1;input.formation=0;
    Check(N::ResolveFormation(reader,0x10000,input,asset)&&asset==10&&!FfxHooks::OmegaEncounter::Eligible(591,asset),"actual Omega boss group remains excluded");
    const auto valid=input;
    for(unsigned kind=0;kind<6;++kind){input=valid;
        if(kind==0)input.nativeFieldRow=65535;
        if(kind==1)input.nativeFieldRow=88;
        if(kind==2)input.group=input.selectedGroup=2;
        if(kind==3)input.formation=1;
        if(kind==4)input.caller=0;
        if(kind==5)input.selectedGroup=0;
        Check(!N::ResolveFormation(reader,0x10000,input,asset)&&asset==255,"invalid or mismatched selection cannot invent an asset");}
    input=valid;memory.readable=false;Check(!N::ResolveFormation(reader,0x10000,input,asset),"unreadable native table fails closed");memory.readable=true;
    for(unsigned off:{0u,4u,8u,12u}){const auto before=memory.bytes;std::memset(memory.bytes.data()+off,255,4);
        Check(!N::ResolveFormation(reader,0x10000,input,asset),"corrupt structural headers fail closed");memory.bytes=before;}
    Check(!N::ResolveFormation(reader,UINT32_MAX-8u,input,asset),"overflowing root rejected before any table read");
    std::printf("SinNaturalFormationRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
