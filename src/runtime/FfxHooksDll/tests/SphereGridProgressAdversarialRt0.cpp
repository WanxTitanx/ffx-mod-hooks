// Production companion codec and admission; no save file or game process.
#include "../hooks/SphereGridProgressCore.h"
#include <cstdio>
#include <limits>
using namespace FfxHooks::SphereGridProgress;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;if(failures<20)std::printf("FAIL: %s\n",why);}}
static Key Identity(){Key k;k.path.fill(1);k.image.fill(2);k.layout.fill(3);k.contents.fill(4);return k;}
static Snapshot State(std::size_t count=8){Snapshot s;s.nodes.resize(count,{35,0});s.links.resize(count-1,0);s.cursors.fill(0);return s;}
static void Reseal(Bytes& b){Detail::Put32(b,b.size()-4,Detail::Crc32(b.data(),b.size()-4));}
int main(){
 const auto key=Identity();auto state=State();state.nodes[7].mask=64;state.cursors[6]=7;
 Bytes bytes;Check(Encode(key,state,bytes),"encode base");const auto good=bytes;
 Check(Detail::Crc32(reinterpret_cast<const std::uint8_t*>("123456789"),9)==0xCBF43926u,"standard companion CRC32 vector");
 Snapshot output=State(9);const auto unchanged=output;
 for(std::size_t size=0;size<good.size();++size){Bytes cut(good.begin(),good.begin()+size);Check(!Decode(cut,key,output)&&output==unchanged,"every truncation preserves destination");}
 auto trailing=good;trailing.push_back(0);Check(!Decode(trailing,key,output)&&output==unchanged,"trailing byte refused");
 for(std::size_t at=0;at<good.size();++at)for(unsigned bit=0;bit<8;++bit){auto bad=good;bad[at]^=static_cast<std::uint8_t>(1u<<bit);Check(!Decode(bad,key,output)&&output==unchanged,"single-bit corruption refused");}
 for(unsigned mode=0;mode<10;++mode){
  auto bad=good;
  if(mode==0)Detail::Put32(bad,8,2);
  if(mode==1)Detail::Put32(bad,12,159);
  if(mode==2)Detail::Put32(bad,16,0);
  if(mode==3)Detail::Put32(bad,16,UINT32_MAX);
  if(mode==4)Detail::Put32(bad,20,UINT32_MAX);
  if(mode==5)Detail::Put32(bad,24,8);
  if(mode==6)bad[kHeader]=130;
  if(mode==7){bad[kHeader]=255;bad[kHeader+1]=1;}
  if(mode==8)bad[kHeader+state.nodes.size()*2]=128;
  if(mode==9){bad[bad.size()-6]=3;}
  Reseal(bad);Check(!Decode(bad,key,output)&&output==unchanged,"valid checksum cannot admit malformed format/state");
 }
 for(std::size_t n:{1u,255u,256u,860u,861u,1024u,1025u,4096u,16384u}){
  auto s=State(n);s.cursors.fill(static_cast<std::uint16_t>(n-1));s.nodes.back().mask=127;
  Check(Encode(key,s,bytes)&&Decode(bytes,key,output)&&output==s,"16-bit boundary and expanded state roundtrip");
 }
 auto invalid=state;invalid.nodes.clear();Check(!Encode(key,invalid,bytes),"zero-node snapshot refused");
 invalid=state;invalid.links.resize(kMaxLinks+1);Check(!Encode(key,invalid,bytes),"oversized links refused");
 Pending p;Check(p.Begin(1,7),"session begins");auto mutableState=state;
 Check(p.Stage(1,key,mutableState,1,7),"snapshot staged");mutableState.nodes[7].mask=127;mutableState.cursors[6]=0;
 Check(p.Verify(1,key,1,7,bytes)&&Decode(bytes,key,output)&&output==state,"staging copied mutable source");
 Check(!p.Stage(1,key,state,1,7),"verified ticket cannot be restaged");
 for(std::uint64_t ticket=2;ticket<2+Pending::kCapacity;++ticket)Check(p.Stage(ticket,key,state,1,7),"bounded independent candidate");
 Check(!p.Stage(10,key,state,1,7),"capacity exhaustion rejected");
 for(std::uint64_t ticket=2;ticket<2+Pending::kCapacity;++ticket)Check(p.Verify(ticket,key,1,7,bytes),"capacity rejection does not cancel earlier candidates");
 Check(p.Stage(11,key,state,1,7),"fresh candidate");auto other=key;other.layout[0]^=1;
 bytes=good;Check(!p.Verify(11,other,1,7,bytes)&&bytes==good,"layout mismatch leaves output unchanged");
 Check(!p.Verify(11,key,1,7,bytes),"rejected candidate cannot later resurrect");
 Check(p.Stage(12,key,state,1,7),"stage before session transition");Check(p.Begin(2,7),"new session invalidates old candidates");
 Check(!p.Verify(12,key,2,7,bytes),"stale completion rejected");Check(!p.Begin(1,7),"older epoch refused");
 Check(p.Stage(13,key,state,2,7),"stage after transition");p.Abort(12);
 Check(p.Verify(13,key,2,7,bytes),"old abort does not revoke new candidate");
 p.RequestStop();Check(!p.Begin(3,7)&&!p.Stage(14,key,state,3,7),"stop is irreversible and closes admission");
 std::printf("SphereGridProgressAdversarialRt0: %u/%u checks passed\n",checks-failures,checks);
 return failures?1:0;
}
