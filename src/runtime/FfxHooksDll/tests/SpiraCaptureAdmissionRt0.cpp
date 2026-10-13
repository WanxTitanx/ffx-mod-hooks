// Jarvis-HOOK: real 187-row DATA v2 fixtures, old payload controls, bit-level rejects.
#include "../hooks/AeroEquipmentCore.h"
#include "../hooks/SpiraAbilityCatalog.h"
#include "../hooks/VanguardRules.h"
#include "../hooks/SpiraElementalEquipment.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <cstring>
namespace A=FfxHooks::AeroEquipment;namespace S=FfxHooks::SpiraAbilities;namespace V=FfxHooks::Vanguard;
namespace T=FfxHooks::SpiraElemental::Equipment;
static unsigned checks=0,failures=0;
void Check(bool v,const char* label){++checks;if(!v){++failures;std::printf("FAIL %s\n",label);}}
int main(int argc,char** argv){
 if(argc<2)return 2;
 for(int file=1;file<argc;++file){
  std::ifstream f(argv[file],std::ios::binary);std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(f),{}};
  if(bytes.size()<20)return 2;
  Check(A::Word(bytes.data()+10)==186,"fixture is the final187-row DATA pack");
  Check(A::Validate(bytes.data(),bytes.size()),"Aero admission accepts the exact172-Capture pack in every locale");
  Check(T::Validate(bytes.data(),bytes.size()),"all seven Touch identities use the actual localized DATA names");
  const auto mapping=V::DefaultMapping();const auto v=V::ValidateMapping(bytes.data(),bytes.size(),mapping);
  Check(v.AllValid(),"Vanguard rows preserve their identities with the approved Capture bit");
  for(unsigned i=0;i<S::Count;++i){
   const auto* row=bytes.data()+20+(S::FirstId+i)*108;
   Check(S::PayloadMatches(i,row,108),"Spira exact payload admission composes with approved Capture");
  }
  for(unsigned id=135;id<=180;++id){
   if(id==154||id==157||id==170||id==171||id==174)continue;
   auto* row=bytes.data()+20+id*108;const auto capture=row[0x66];row[0x66]=static_cast<unsigned char>(capture&~4u);
   Check(id<=147?V::ValidateMapping(bytes.data(),bytes.size(),mapping).valid[id-135]:id<=174?S::PayloadMatches(id-148,row,108):A::Validate(bytes.data(),bytes.size()),"old non-Capture payload remains accepted");
   row[0x66]=capture;
  }
  for(unsigned bit=0;bit<8;++bit)if(bit!=2){
   auto changed=bytes;changed[20+175*108+0x66]^=static_cast<unsigned char>(1u<<bit);
   Check(!A::Validate(changed.data(),changed.size()),"Capture allowance never ignores neighboring native flags");
   changed=bytes;changed[20+135*108+0x66]^=static_cast<unsigned char>(1u<<bit);
   Check(!V::ValidateMapping(changed.data(),changed.size(),mapping).valid[0],"Vanguard rejects other native flags");
   changed=bytes;changed[20+150*108+0x66]^=static_cast<unsigned char>(1u<<bit);
   Check(!S::PayloadMatches(2,changed.data()+20+150*108,108),"Spira rejects other native flags");
  }
  for(unsigned id:{154u,157u,170u,171u,174u}){
   auto row=std::vector<unsigned char>(bytes.begin()+20+id*108,bytes.begin()+20+(id+1)*108);row[0x66]|=4;
   Check(!S::PayloadMatches(id-148,row.data(),row.size()),"retired/pending/test Spira rows do not gain a Capture allowance");
  }
  for(unsigned id:T::Ids){
   const auto* original=bytes.data()+20+id*108;
   for(unsigned at=16;at<108;++at)for(unsigned bit=0;bit<8;++bit){
    if(at==0x66&&bit==2)continue;
    std::array<unsigned char,108> row{};std::memcpy(row.data(),original,row.size());row[at]^=static_cast<unsigned char>(1u<<bit);
    Check(!T::Payload(row.data(),id),"Touch rejects every unapproved native payload bit");
   }
   auto changed=bytes;changed[20+id*108]=changed[20+id*108+1]=255;
   Check(!T::Validate(changed.data(),changed.size()),"out-of-range name references fail closed");
  }
  Check(T::Touch(0x80B0)==0x40&&T::Touch(0x80B5)==1&&T::Touch(0x80BA)==0x80&&!T::Touch(0x8047),"elemental Shadow Touch never aliases native blindness Darktouch");
  Check(!T::Validate(bytes.data(),20+187*108),"missing name pool fails closed");
 }
 std::printf("SpiraCaptureAdmissionRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
