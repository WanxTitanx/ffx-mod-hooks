#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include "../hooks/SphereGridProgressCore.h"
#if __has_include("../hooks/SphereGridProgress8Core.h")
#include "../hooks/SphereGridProgress8Core.h"
namespace S=FfxHooks::SphereGridProgress;
namespace E=FfxHooks::SphereGridProgress8;
unsigned checks=0,failed=0;
#define CHECK(x) do{++checks;if(!(x)){if(++failed<=20)std::printf("FAIL line %u: %s\n",__LINE__,#x);}}while(false)
S::Key Key(){S::Key k;unsigned v=1;for(auto* h:{&k.path,&k.image,&k.layout,&k.contents})for(auto& b:*h)b=static_cast<std::uint8_t>(v++);return k;}
S::Snapshot Seven(unsigned n,unsigned links){
 S::Snapshot s;s.nodes.resize(n);s.links.resize(links);
 for(unsigned i=0;i<n;++i)s.nodes[i]={static_cast<std::uint8_t>(i%130),static_cast<std::uint8_t>(i%128)};
 for(unsigned i=0;i<links;++i)s.links[i]=static_cast<std::uint8_t>(i%128);
 for(unsigned i=0;i<7;++i)s.cursors[i]=static_cast<std::uint16_t>((i*257)%n);
 s.tilt=2;s.zoom=3;return s;
}
void Reseal(S::Bytes& b){S::Detail::Put32(b,b.size()-4,S::Detail::Crc32(b.data(),b.size()-4));}
int main(){
 const auto key=Key();
 for(unsigned n:{1u,860u,861u,1003u,1024u,1025u,4096u,16384u})for(unsigned links:{0u,1u,1024u,16384u}){
  const auto original=Seven(n,links);S::Bytes oldBytes;CHECK(S::Encode(key,original,oldBytes));const auto frozen=oldBytes;
  E::Snapshot s;CHECK(E::ImportSeven(original,static_cast<std::uint16_t>(n-1),s));
  CHECK(s.cursors[7]==n-1&&s.nodes==original.nodes&&s.links==original.links);
  for(auto& node:s.nodes)node.mask|=0x80;
  for(auto& mask:s.links)mask|=0x80;
  S::Bytes bytes;CHECK(E::Encode(key,s,bytes));
  CHECK(bytes.size()==160+n*2+links+18+4);
  CHECK(bytes[6]=='2'&&S::Detail::Get32(bytes,8)==2&&S::Detail::Get32(bytes,24)==8);
  E::Snapshot decoded;CHECK(E::Decode(bytes,key,decoded)&&decoded==s);
  S::Snapshot projected;CHECK(E::ProjectSeven(s,projected)&&projected==original);
  S::Bytes reencoded;CHECK(S::Encode(key,projected,reencoded)&&reencoded==frozen);
  CHECK(!S::Decode(bytes,key,projected)&&projected==original);
  const auto keep=decoded;CHECK(!E::Decode(oldBytes,key,decoded)&&decoded==keep);
  CHECK(oldBytes==frozen);
 }
 const auto legacy=Seven(37,19);E::Snapshot seed;CHECK(E::ImportSeven(legacy,UINT16_MAX,seed)&&seed.cursors[7]==UINT16_MAX);
 seed.nodes[36].mask=0x80;seed.links[18]=0x80;S::Bytes bytes;CHECK(E::Encode(key,seed,bytes));
 for(std::size_t bit=0;bit<bytes.size()*8;++bit){
  auto bad=bytes;bad[bit/8]^=static_cast<std::uint8_t>(1u<<(bit%8));auto out=seed;
  CHECK(!E::Decode(bad,key,out)&&out==seed);
 }
 for(std::size_t length=0;length<bytes.size();++length){S::Bytes bad(bytes.begin(),bytes.begin()+length);auto out=seed;CHECK(!E::Decode(bad,key,out)&&out==seed);}
 {auto bad=bytes;bad.push_back(0);auto out=seed;CHECK(!E::Decode(bad,key,out)&&out==seed);}
 // Recompute CRC on malformed records: structural and identity gates must not
 // pass merely because adversarial bytes carry their own valid checksum.
 for(unsigned offset:{8u,12u,16u,20u,24u,28u})for(std::uint32_t value:{0u,1u,7u,8u,0xffffffffu}){
  if(S::Detail::Get32(bytes,offset)==value)continue;
  auto bad=bytes;S::Detail::Put32(bad,offset,value);Reseal(bad);auto out=seed;
  CHECK(!E::Decode(bad,key,out)&&out==seed);
 }
 for(unsigned h=0;h<4;++h){
  auto wrong=key;auto* hash=h==0?&wrong.path:h==1?&wrong.image:h==2?&wrong.layout:&wrong.contents;(*hash)[17]^=0x40;
  auto out=seed;CHECK(!E::Decode(bytes,wrong,out)&&out==seed);
  auto bad=bytes;bad[32+h*32+17]^=0x40;Reseal(bad);CHECK(!E::Decode(bad,key,out)&&out==seed);
 }
 for(unsigned content=130;content<255;++content){auto bad=bytes;bad[160]=static_cast<std::uint8_t>(content);Reseal(bad);auto out=seed;CHECK(!E::Decode(bad,key,out)&&out==seed);}
 {auto bad=bytes;bad[160]=255;bad[161]=128;Reseal(bad);auto out=seed;CHECK(!E::Decode(bad,key,out)&&out==seed);}
 const auto cursor=160+seed.nodes.size()*2+seed.links.size()+14;
 for(unsigned value:{37u,255u,16384u,65534u}){auto bad=bytes;bad[cursor]=static_cast<std::uint8_t>(value);bad[cursor+1]=static_cast<std::uint8_t>(value>>8);Reseal(bad);auto out=seed;CHECK(!E::Decode(bad,key,out)&&out==seed);}
 for(unsigned kind=0;kind<7;++kind){auto bad=seed;
  if(kind==0)bad.nodes.clear();
  if(kind==1)bad.nodes.resize(16385);
  if(kind==2)bad.links.resize(16385);
  if(kind==3)bad.cursors[7]=37;
  if(kind==4)bad.tilt=3;
  if(kind==5)bad.zoom=4;
  if(kind==6)bad.nodes[0]={255,128};
  S::Bytes out{9,8,7};const auto before=out;CHECK(!E::Encode(key,bad,out)&&out==before);
 }
 {E::Snapshot out=seed;auto bad=legacy;bad.nodes[0].mask=128;CHECK(!E::ImportSeven(bad,0,out)&&out==seed);
  CHECK(!E::ImportSeven(legacy,37,out)&&out==seed);S::Key badKey{};S::Bytes kept{1,2};CHECK(!E::Encode(badKey,seed,kept)&&kept==S::Bytes({1,2}));}
 {auto empty=seed;empty.nodes[0]={255,0};S::Bytes encoded;CHECK(E::Encode(key,empty,encoded));E::Snapshot out;CHECK(E::Decode(encoded,key,out)&&out==empty);}
 std::printf("SphereGridProgress8Rt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: explicit eight-character companion codec is missing");return 1;}
#endif
