#include "../hooks/SpiraAbilityCatalog.h"
#include "../hooks/AeroEquipmentCore.h"
#include "../hooks/VanguardRules.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
static unsigned checks=0,failed=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
static std::vector<unsigned char> Read(const char* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 if(argc!=3)return 2;auto before=Read(argv[1]),after=Read(argv[2]);
 using namespace FfxHooks;namespace S=SpiraAbilities;
 const auto baseline=S::Validate(before.data(),before.size(),S::DefaultMapping(),nullptr,0,S::NameEncoding::Latin,~0u);
 const auto translated=S::Validate(after.data(),after.size(),S::DefaultMapping(),nullptr,0,S::NameEncoding::Latin,~0u);
 for(unsigned i=0;i<S::Count;++i){Check(baseline[i].rowVerified,"source Spira identity is admitted");Check(translated[i].rowVerified,"translated Spira name preserves the same effect admission");}
 Check(AeroEquipment::Validate(before.data(),before.size()),"source Aero identity is admitted");
 Check(AeroEquipment::Validate(after.data(),after.size()),"translated Aero names preserve payload admission");
 unsigned last=0;std::size_t pool=0;Check(S::Table(after.data(),after.size(),last,pool),"translated table has native bounds");
 for(unsigned id=135;id<=180;++id){const unsigned offset=S::Word(after.data()+20+id*108);const auto* name=after.data()+pool+offset;const auto available=after.size()-pool-offset;
  Check(UiAbilityNameProof::Matches(id,name,available),"reviewed translated name is bound to its own mod identity");
  auto changed=after;changed[pool+offset]^=1;
  Check(!UiAbilityAliases::Matches(id,changed.data()+pool+offset,available),"one altered glyph is not admitted as a reviewed alias");
  Check(!UiAbilityAliases::Matches(id,name,std::strlen(reinterpret_cast<const char*>(name))),"name bounds include the verified terminator");
 }
 for(unsigned id=175;id<=180;++id){auto changed=after;changed[20+id*108+17]^=1;
  Check(!AeroEquipment::Validate(changed.data(),changed.size()),"translated names never relax an Aero payload field");}
 for(unsigned i=0;i<S::Count;++i){auto changed=after;changed[20+(S::FirstId+i)*108+33]^=1;
  const auto bad=S::Validate(changed.data(),changed.size(),S::DefaultMapping(),nullptr,0,S::NameEncoding::Latin,~0u);
  Check(!bad[i].rowVerified,"translated names never relax a Spira payload field");}
 std::printf("WorkshopLanguageAdmissionRt0 %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
