#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <utility>
namespace FfxHooks::SphereGridProgress {
// Versioned companion, not native save bytes. No filesystem or game I/O here.
using Hash=std::array<std::uint8_t,32>;
using Bytes=std::vector<std::uint8_t>;
inline constexpr std::size_t kCharacters=7,kMaxNodes=16384,kMaxLinks=16384;
inline constexpr std::size_t kHeader=160,kTail=16,kChecksum=4;
inline constexpr std::size_t kMaxBytes=kHeader+kMaxNodes*2+kMaxLinks+kTail+kChecksum;
inline constexpr std::array<std::uint8_t,8> kMagic{'F','F','X','S','G','P','1',0};
struct Key {
 Hash path{},image{},layout{},contents{};
 bool Valid() const noexcept {
  auto nonzero=[](const Hash& h){return std::any_of(h.begin(),h.end(),[](auto b){return b!=0;});};
  return nonzero(path)&&nonzero(image)&&nonzero(layout)&&nonzero(contents);
 }
 friend bool operator==(const Key& a,const Key& b) noexcept {return a.path==b.path&&a.image==b.image&&a.layout==b.layout&&a.contents==b.contents;}
};
struct Node {
 std::uint8_t content=255,mask=0;
 friend bool operator==(Node a,Node b) noexcept {return a.content==b.content&&a.mask==b.mask;}
};
struct Snapshot {
 std::vector<Node> nodes;
 std::vector<std::uint8_t> links;
 std::array<std::uint16_t,kCharacters> cursors{};
 std::uint8_t tilt=0,zoom=0;
 bool Valid() const noexcept {
  if(nodes.empty()||nodes.size()>kMaxNodes||links.size()>kMaxLinks||tilt>2||zoom>3)return false;
  for(auto n:nodes)if((n.content>=130&&n.content!=255)||n.mask>127||(n.content==255&&n.mask))return false;
  for(auto mask:links)if(mask>127)return false;
  for(auto cursor:cursors)if(cursor!=UINT16_MAX&&cursor>=nodes.size())return false;
  return true;
 }
 friend bool operator==(const Snapshot& a,const Snapshot& b) noexcept {return a.nodes==b.nodes&&a.links==b.links&&a.cursors==b.cursors&&a.tilt==b.tilt&&a.zoom==b.zoom;}
};
namespace Detail {
inline void Put32(Bytes& b,std::size_t at,std::uint32_t v) noexcept {for(unsigned i=0;i<4;++i)b[at+i]=static_cast<std::uint8_t>(v>>(i*8));}
inline std::uint32_t Get32(const Bytes& b,std::size_t at) noexcept {
 std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(b[at+i])<<(i*8);return v;
}
inline std::uint32_t Crc32(const std::uint8_t* b,std::size_t size) noexcept {
 std::uint32_t crc=UINT32_MAX;
 for(std::size_t i=0;i<size;++i){crc^=b[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
 return ~crc;
}
inline bool Matches(const Bytes& b,const Key& k) noexcept {
 return std::equal(k.path.begin(),k.path.end(),b.begin()+32)&&std::equal(k.image.begin(),k.image.end(),b.begin()+64)&&std::equal(k.layout.begin(),k.layout.end(),b.begin()+96)&&std::equal(k.contents.begin(),k.contents.end(),b.begin()+128);
}
}
inline bool Encode(const Key& key,const Snapshot& s,Bytes& output) noexcept {
 if(!key.Valid()||!s.Valid())return false;
 try {
  auto payload=s.nodes.size()*2+s.links.size()+kTail;Bytes b(kHeader+payload+kChecksum,0);
  std::copy(kMagic.begin(),kMagic.end(),b.begin());
  Detail::Put32(b,8,1);Detail::Put32(b,12,160);Detail::Put32(b,16,static_cast<std::uint32_t>(s.nodes.size()));Detail::Put32(b,20,static_cast<std::uint32_t>(s.links.size()));Detail::Put32(b,24,7);Detail::Put32(b,28,static_cast<std::uint32_t>(payload));
  std::copy(key.path.begin(),key.path.end(),b.begin()+32);std::copy(key.image.begin(),key.image.end(),b.begin()+64);std::copy(key.layout.begin(),key.layout.end(),b.begin()+96);std::copy(key.contents.begin(),key.contents.end(),b.begin()+128);
  std::size_t at=kHeader;
  for(auto n:s.nodes){b[at++]=n.content;b[at++]=n.mask;}
  for(auto mask:s.links)b[at++]=mask;
  for(auto cursor:s.cursors){b[at++]=static_cast<std::uint8_t>(cursor);b[at++]=static_cast<std::uint8_t>(cursor>>8);}
  b[at++]=s.tilt;b[at++]=s.zoom;Detail::Put32(b,at,Detail::Crc32(b.data(),at));output.swap(b);return true;
 }catch(...){return false;}
}
inline bool Decode(const Bytes& b,const Key& key,Snapshot& output) noexcept {
 if(!key.Valid()||b.size()<kHeader+2+kTail+kChecksum||b.size()>kMaxBytes)return false;
 if(!std::equal(kMagic.begin(),kMagic.end(),b.begin())||Detail::Get32(b,8)!=1||Detail::Get32(b,12)!=kHeader||Detail::Get32(b,24)!=kCharacters||!Detail::Matches(b,key))return false;
 auto nodes=Detail::Get32(b,16),links=Detail::Get32(b,20);
 if(!nodes||nodes>kMaxNodes||links>kMaxLinks)return false;
 std::size_t payload=std::size_t(nodes)*2+links+kTail;
 if(Detail::Get32(b,28)!=payload||b.size()!=kHeader+payload+kChecksum||Detail::Get32(b,b.size()-4)!=Detail::Crc32(b.data(),b.size()-4))return false;
 try {
  Snapshot s;s.nodes.resize(nodes);s.links.resize(links);std::size_t at=kHeader;
  for(auto& n:s.nodes){n.content=b[at++];n.mask=b[at++];}
  for(auto& mask:s.links)mask=b[at++];
  for(auto& cursor:s.cursors){cursor=static_cast<std::uint16_t>(b[at]|(unsigned(b[at+1])<<8));at+=2;}
  s.tilt=b[at++];s.zoom=b[at++];if(!s.Valid())return false;output=std::move(s);return true;
 }catch(...){return false;}
}
class Pending {
public:
 static constexpr std::size_t kCapacity=8;
 bool Begin(std::uint64_t epoch,std::uint32_t thread) noexcept {
  if(!epoch||!thread||stopping_.load(std::memory_order_acquire))return false;
  try {std::lock_guard<std::mutex> lock(mutex_);
   if(stopping_.load(std::memory_order_acquire)||epoch<epoch_)return false;
   for(auto& slot:slots_)slot={};
   epoch_=epoch;thread_=thread;ready_=true;return true;
  }catch(...){RequestStop();return false;}
 }
 bool Stage(std::uint64_t ticket,const Key& key,const Snapshot& s,std::uint64_t epoch,std::uint32_t thread) noexcept {
  try {std::lock_guard<std::mutex> lock(mutex_);
   if(!Admitted(epoch,thread)||!ticket)return false;
   if(auto* old=Find(ticket)){*old={};return false;}
   if(ticket<=highestTicket_)return false;
   highestTicket_=ticket;Slot* free=nullptr;
   for(auto& slot:slots_)if(!slot.ticket){free=&slot;break;}
   if(!free)return false;
   Bytes bytes;if(!Encode(key,s,bytes)||!Admitted(epoch,thread))return false;
   free->key=key;free->bytes.swap(bytes);free->ticket=ticket;return true;
  }catch(...){RequestStop();return false;}
 }
 // Returns immutable bytes, not disk success. The adapter must recheck stop
 // and identity immediately before filesystem publication.
 bool Verify(std::uint64_t ticket,const Key& key,std::uint64_t epoch,std::uint32_t thread,Bytes& output) noexcept {
  try {std::lock_guard<std::mutex> lock(mutex_);auto* slot=Find(ticket);if(!slot)return false;
   if(!Admitted(epoch,thread)||!(key==slot->key)){*slot={};return false;}
   output.swap(slot->bytes);*slot={};return true;
  }catch(...){RequestStop();return false;}
 }
 void Abort(std::uint64_t ticket) noexcept {
  try {std::lock_guard<std::mutex> lock(mutex_);if(auto* slot=Find(ticket))*slot={};}catch(...){RequestStop();}
 }
 // Loader-lock fallback: no mutex, I/O, waiting or freeing of memory.
 void RequestStop() noexcept {stopping_.store(true,std::memory_order_release);}
private:
 struct Slot {std::uint64_t ticket=0;Key key{};Bytes bytes;};
 Slot* Find(std::uint64_t ticket) noexcept {
  if(ticket)for(auto& slot:slots_)if(slot.ticket==ticket)return &slot;
  return nullptr;
 }
 bool Admitted(std::uint64_t epoch,std::uint32_t thread) const noexcept {return ready_&&!stopping_.load(std::memory_order_acquire)&&epoch==epoch_&&thread==thread_;}
 std::array<Slot,kCapacity> slots_{};std::mutex mutex_;std::atomic<bool> stopping_{false};
 std::uint64_t epoch_=0,highestTicket_=0;std::uint32_t thread_=0;bool ready_=false;
};
}
