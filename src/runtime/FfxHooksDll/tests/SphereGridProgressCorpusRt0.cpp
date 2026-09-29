// Actual saved grid fields through the companion codec. Path/layout keys below
// are explicit fixture identities; no claim of a live save subscriber.
#include "../hooks/SphereGridProgressCore.h"
#include "../hooks/RonsoPoolSave.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
namespace fs=std::filesystem;
using namespace FfxHooks::SphereGridProgress;
static unsigned checks=0,failures=0,saves=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;if(failures<20)std::printf("FAIL: %s\n",why);}}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 try {
  for(const auto& file:fs::directory_iterator(argv[1])){
   if(!file.is_regular_file()||file.file_size()!=FfxHooks::RonsoPool::kSaveSize)continue;
   FfxHooks::RonsoPool::SaveImage bytes{};
   std::ifstream in(file.path(),std::ios::binary);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
   if(!in||!FfxHooks::RonsoPool::IsValidSave(bytes))return 2;
   ++saves;Snapshot s;s.nodes.resize(1024);s.links.resize(1024);
   constexpr std::size_t base=64+0x21ec;
   for(std::size_t i=0;i<1024;++i){s.nodes[i]={bytes[base+2*i],bytes[base+2*i+1]};s.links[i]=bytes[base+0xa00+i];}
   for(std::size_t i=0;i<7;++i)s.cursors[i]=static_cast<std::uint16_t>(bytes[base+0xf00+i*2]|(unsigned(bytes[base+0xf01+i*2])<<8));
   s.tilt=bytes[base+0xf18];s.zoom=bytes[base+0xf19];
   Key k;k.path.fill(static_cast<std::uint8_t>(saves));k.image.fill(2);k.layout.fill(3);k.contents.fill(4);
   Bytes encoded;Snapshot decoded;
   bool ok=Encode(k,s,encoded);Check(ok,"native snapshot admitted without normalization");
   if(!ok){std::printf("view=%u,%u\n",s.tilt,s.zoom);continue;}
   Check(Decode(encoded,k,decoded)&&decoded==s,"native snapshot roundtrip");
   for(std::size_t n:{1025u,4096u,16384u}){
    auto ext=s;ext.nodes.resize(n,{35,0});ext.links.resize(n,0);
    ext.nodes.back().mask=64;ext.cursors[6]=static_cast<std::uint16_t>(n-1);
    Check(Encode(k,ext,encoded)&&Decode(encoded,k,decoded)&&decoded==ext,"expanded companion roundtrip");
    Check(std::equal(s.nodes.begin(),s.nodes.end(),decoded.nodes.begin()),"all original nodes unchanged");
    Check(std::equal(s.links.begin(),s.links.end(),decoded.links.begin()),"all original links unchanged");
   }
   auto other=k;other.path[0]^=128;auto old=decoded;
   Check(!Decode(encoded,other,decoded)&&decoded==old,"foreign save refused without mutation");
   FfxHooks::RonsoPool::SaveImage after{};std::ifstream verify(file.path(),std::ios::binary);
   verify.read(reinterpret_cast<char*>(after.data()),after.size());Check(static_cast<bool>(verify)&&after==bytes,"original save unchanged");
  }
  Check(saves==21,"all pinned unique FFX saves tested");
  std::printf("SphereGridProgressCorpusRt0: saves=%u checks=%u/%u passed\n",saves,checks-failures,checks);
  return failures?1:0;
 }catch(...){return 2;}
}
