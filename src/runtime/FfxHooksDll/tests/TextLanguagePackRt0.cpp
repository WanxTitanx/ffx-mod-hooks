#include "../hooks/TextLanguagePack.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <openssl/sha.h>
using namespace FfxHooks::TextLanguage;
namespace {
struct Failure {std::string id{};Family family=Family::Menu;bool source=false;bool hit=false;};
struct Host {
 std::filesystem::path pack, source; int failAt=-1, reads=0; bool corrupt=false; int corruptAt=-1;
 std::string corruptSource{}; bool corruptAllTextSources=false; Failure failure{}; std::vector<std::string> retained{};
 std::map<std::string,std::string> reasons{};
};
unsigned checks=0,failures=0;
std::string error;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<": "<<error<<'\n';}}
bool IsText(Family f){return f==Family::Menu||f==Family::Battle||f==Family::Event;}
bool IsFont(Family f){return f==Family::Metrics||f==Family::Atlas;}
bool Read(void* context,const Resource& r,bool source,Bytes& b){
 auto& h=*static_cast<Host*>(context);const auto position=h.reads++;
 if(position==h.failAt){h.failure={r.id,r.family,source,true};return false;}
 std::ifstream f((source?h.source:h.pack)/r.path,std::ios::binary);if(!f)return false;
 b=Bytes(std::istreambuf_iterator<char>(f),{});
 if((h.corrupt||position==h.corruptAt)&&!source&&!b.empty())b.back()^=1;
 if(source&&!b.empty()&&(r.id==h.corruptSource||(h.corruptAllTextSources&&IsText(r.family))))b.front()^=1;
 return true;
}
bool Hash(void*,const Bytes& b,std::string& out){
 unsigned char digest[SHA256_DIGEST_LENGTH]{};SHA256(b.data(),b.size(),digest);
 constexpr char hex[]="0123456789abcdef";out.clear();for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return true;
}
void Retain(void* context,const Resource& r,const char* reason){
 auto& h=*static_cast<Host*>(context);h.retained.push_back(r.id);h.reasons[r.id]=reason?reason:"";
}
std::size_t IndexOf(const PreparedPack& p,const std::string& id){
 for(std::size_t n=0;n<p.manifest.resources.size();++n)if(p.manifest.resources[n].id==id)return n;
 return p.manifest.resources.size();
}
// A retained resource keeps no payload; every other payload is the admitted bytes.
bool OnlyRetained(const PreparedPack& p,const std::vector<Bytes>& expected,const std::set<std::string>& ids){
 if(p.retained.size()!=p.manifest.resources.size()||p.resources.size()!=expected.size()||p.retainedCount!=ids.size())return false;
 for(std::size_t n=0;n<p.resources.size();++n){
  const bool kept=ids.count(p.manifest.resources[n].id)!=0;
  if(kept!=(p.retained[n]!=0))return false;
  if(kept?!p.resources[n].empty():p.resources[n]!=expected[n])return false;
 }
 return true;
}
}
int main(int argc,char** argv){
 if(argc<3||argc>4){std::cerr<<"Usage: text_pack_rt0 PACK REFERENCE [EXPECTED_RETAINED_ID,...]\n";return 2;}
 Host host{argv[1],argv[2]};std::ifstream f(host.pack/"manifest.json");
 std::set<std::string> expectedRetained;
 if(argc==4){std::string list=argv[3];for(std::size_t at=0;at<=list.size();){const auto end=std::min(list.find(',',at),list.size());if(end>at)expectedRetained.insert(list.substr(at,end-at));at=end+1;}}
 std::string json(std::istreambuf_iterator<char>(f),{});PreparedPack pack;
 PackIo io{&host,Read,Hash,Retain};
 Check(AdmitPack(json,io,pack,error),"real demonstrator is admitted");
 Check(pack.manifest.locale=="pt-BR"&&pack.resources.size()>=6,"admission publishes locale plus text, metrics and four atlas pages");
 std::set<std::string> retained;for(std::size_t n=0;n<pack.retained.size();++n)if(pack.retained[n])retained.insert(pack.manifest.resources[n].id);
 for(const auto& id:retained)std::cout<<"retained native: "<<id<<'\n';
 Check(retained==expectedRetained,"exactly the expected resources keep their native file");
 Check(std::set<std::string>(host.retained.begin(),host.retained.end())==retained&&host.retained.size()==retained.size(),"every retained resource is reported once");
 bool limits=true,families=true;
 for(std::size_t n=0;n<pack.manifest.resources.size();++n){const auto& r=pack.manifest.resources[n];
  const auto limit=NativeKernelLimit(r.request,false);
  if(!pack.retained[n]&&limit&&r.size>limit)limits=false;
  if(pack.retained[n]&&IsFont(r.family))families=false;}
 Check(limits,"every published kernel table fits the native loader");
 Check(families,"font pages are never retained individually");
 {
  // With the 32-bit loader adapter armed, only the 16-bit size deferrals change.
  const auto narrowReasons=host.reasons;
  host.reads=0;host.retained.clear();host.reasons.clear();PreparedPack wide;const PackIo extended{&host,Read,Hash,Retain,true};
  Check(AdmitPack(json,extended,wide,error),"the pack is admitted for the 32-bit kernel loader");
  bool widened=wide.retained.size()==pack.retained.size(),bounded=true;unsigned admittedByAdapter=0;
  for(std::size_t n=0;widened&&n<wide.manifest.resources.size();++n){const auto& r=wide.manifest.resources[n];
   const auto wideLimit=NativeKernelLimit(r.request,true);const auto reason=narrowReasons.find(r.id);
   const bool sizeDeferral=pack.retained[n]&&reason!=narrowReasons.end()&&
       reason->second.find("kernel table size")!=std::string::npos&&r.size<=wideLimit;
   if(sizeDeferral){admittedByAdapter+=!wide.retained[n];if(wide.retained[n])widened=false;}
   else if((wide.retained[n]!=0)!=(pack.retained[n]!=0))widened=false;
   if(!wide.retained[n]&&wideLimit&&r.size>wideLimit)bounded=false;
  }
  Check(widened,"only tables deferred by the 16-bit loader are published when the adapter owns them");
  Check(bounded,"every table published for the adapter fits its native staging buffer");
  std::cout<<"admitted by the 32-bit kernel adapter: "<<admittedByAdapter<<'\n';
 }
 const auto expected=pack.resources;
 std::map<Family,std::vector<std::size_t>> byFamily;
 for(std::size_t n=0;n<pack.manifest.resources.size();++n)if(!pack.retained[n])byFamily[pack.manifest.resources[n].family].push_back(n);
 std::set<int> boundaries;
 if(pack.manifest.resources.size()<=256){
  for(int n=0;n<static_cast<int>(pack.manifest.resources.size()*2);++n)boundaries.insert(n);
 }else{
  // Full corpus packs share one IO/transaction path. Read positions are two per
  // admitted resource; cover first/middle/last of each family plus every font page.
  for(const auto& group:byFamily){const auto& v=group.second;
   for(auto n:{v.front(),v[v.size()/2],v.back()}){boundaries.insert(static_cast<int>(n*2));boundaries.insert(static_cast<int>(n*2+1));}
   if(IsFont(group.first))for(auto n:v){boundaries.insert(static_cast<int>(n*2));boundaries.insert(static_cast<int>(n*2+1));}
  }
 }
 unsigned retainedBoundaries=0;
 for(int n:boundaries){host.failAt=n;host.reads=0;host.failure={};host.retained.clear();
  PreparedPack preserved;preserved.manifest.locale="preserved";
  const bool accepted=AdmitPack(json,io,preserved,error);
  if(!host.failure.hit)continue;
  if(host.failure.source&&!IsFont(host.failure.family)){
   // A text or UI-texture source that cannot be read stays native; nothing else changes.
   auto ids=retained;ids.insert(host.failure.id);++retainedBoundaries;
   Check(accepted&&OnlyRetained(preserved,expected,ids),"an unreadable text source keeps only that resource native");
  }else Check(!accepted&&preserved.manifest.locale=="preserved","a pack or font read failure never publishes half a pack");
 }
 host.failAt=-1;host.reads=0;host.corrupt=true;
 Check(!AdmitPack(json,io,pack,error)&&pack.resources==expected,"pack payload hash failure preserves previously admitted bytes");
 host.corrupt=false;host.corruptAt=-1;
 const auto text=byFamily[Family::Battle].empty()?byFamily[Family::Menu].front():byFamily[Family::Battle].front();
 const auto font=byFamily[Family::Atlas].front();
 host.reads=0;host.corruptSource=pack.manifest.resources[text].id;host.retained.clear();
 {PreparedPack changed;auto ids=retained;ids.insert(host.corruptSource);
  Check(AdmitPack(json,io,changed,error)&&OnlyRetained(changed,expected,ids),"a changed mod source keeps only that resource native");
  Check(host.retained.size()==ids.size(),"the changed source is reported");
  Check(changed.retained[IndexOf(changed,host.corruptSource)]!=0,"the changed source is marked native");}
 host.reads=0;host.corruptSource=pack.manifest.resources[font].id;
 {PreparedPack preserved;preserved.manifest.locale="preserved";
  Check(!AdmitPack(json,io,preserved,error)&&preserved.manifest.locale=="preserved","a changed font page rejects the whole pack");}
 host.reads=0;host.corruptSource.clear();host.corruptAllTextSources=true;
 {PreparedPack preserved;preserved.manifest.locale="preserved";
  Check(!AdmitPack(json,io,preserved,error)&&preserved.manifest.locale=="preserved","a pack without any matching text is rejected");}
 host.corruptAllTextSources=false;host.reads=0;PreparedPack other;
 Check(AdmitPack(json,io,other,error)&&other.resources==expected,"a second admission has independent state");
 PackIo missing{};Check(!AdmitPack(json,missing,other,error),"incomplete IO rejected");
 std::cout<<"TextLanguagePack RT0: "<<checks<<" checks, "<<failures<<" failures; "<<boundaries.size()
          <<" read failure boundaries ("<<retainedBoundaries<<" retained)\n";
 return failures?1:0;
}
