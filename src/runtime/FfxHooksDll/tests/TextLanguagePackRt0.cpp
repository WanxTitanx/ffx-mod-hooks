#include "../hooks/TextLanguagePack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <openssl/sha.h>
using namespace FfxHooks::TextLanguage;
namespace {
struct Host {std::filesystem::path pack, source; int failAt=-1, reads=0; bool corrupt=false;};
unsigned checks=0,failures=0;
std::string error;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<": "<<error<<'\n';}}
bool Read(void* context,const Resource& r,bool source,Bytes& b){
 auto& h=*static_cast<Host*>(context);if(h.reads++==h.failAt)return false;
 std::ifstream f((source?h.source:h.pack)/r.path,std::ios::binary);if(!f)return false;
 b=Bytes(std::istreambuf_iterator<char>(f),{});if(h.corrupt&&!source&&!b.empty())b.back()^=1;return true;
}
bool Hash(void*,const Bytes& b,std::string& out){
 unsigned char digest[SHA256_DIGEST_LENGTH]{};SHA256(b.data(),b.size(),digest);
 constexpr char hex[]="0123456789abcdef";out.clear();for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return true;
}
}
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: text_pack_rt0 PACK REFERENCE\n";return 2;}
 Host host{argv[1],argv[2]};std::ifstream f(host.pack/"manifest.json");
 std::string json(std::istreambuf_iterator<char>(f),{});PreparedPack pack;
 PackIo io{&host,Read,Hash};
 Check(AdmitPack(json,io,pack,error),"real demonstrator is admitted as a whole");
 Check(pack.manifest.locale=="pt-BR"&&pack.resources.size()>=6,"admission publishes locale plus text, metrics and four atlas pages");
 const auto expected=pack.resources;
 for(int n=0;n<static_cast<int>(pack.manifest.resources.size()*2);++n){host.failAt=n;host.reads=0;PreparedPack preserved;preserved.manifest.locale="preserved";
  const bool accepted=AdmitPack(json,io,preserved,error);
  Check(!accepted&&preserved.manifest.locale=="preserved","read failure never publishes half a pack");}
 host.failAt=-1;host.reads=0;host.corrupt=true;
 Check(!AdmitPack(json,io,pack,error)&&pack.resources==expected,"hash failure preserves previously admitted bytes");
 host.corrupt=false;host.reads=0;PreparedPack other;
 Check(AdmitPack(json,io,other,error)&&other.resources==expected,"a second admission has independent state");
 PackIo missing{};Check(!AdmitPack(json,missing,other,error),"incomplete IO rejected");
 std::cout<<"TextLanguagePack RT0: "<<checks<<" checks, "<<failures<<" failures\n";
 return failures?1:0;
}
