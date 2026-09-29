#pragma once
#include "SphereGridProgressCore.h"

namespace FfxHooks::SphereGridProgress8 {
// Explicit v2 companion. The seven-character v1 codec and native save layout
// remain unchanged. This is serialization only, not a save writer or Grid8 hook.
using Key=SphereGridProgress::Key;
using Bytes=SphereGridProgress::Bytes;
using Node=SphereGridProgress::Node;
namespace Detail=SphereGridProgress::Detail;
inline constexpr std::size_t kCharacters=8,kHeader=160,kTail=18,kChecksum=4;
inline constexpr std::size_t kMaxNodes=SphereGridProgress::kMaxNodes;
inline constexpr std::size_t kMaxLinks=SphereGridProgress::kMaxLinks;
inline constexpr std::size_t kMaxBytes=kHeader+kMaxNodes*2+kMaxLinks+kTail+kChecksum;
inline constexpr std::array<std::uint8_t,8> kMagic{'F','F','X','S','G','P','2',0};
struct Snapshot {
    std::vector<Node> nodes;
    std::vector<std::uint8_t> links;
    std::array<std::uint16_t,kCharacters> cursors{};
    std::uint8_t tilt=0,zoom=0;
    bool Valid() const noexcept {
        if(nodes.empty()||nodes.size()>kMaxNodes||links.size()>kMaxLinks||tilt>2||zoom>3)return false;
        for(const auto node:nodes){
            if((node.content>=130&&node.content!=255)||(node.content==255&&node.mask))return false;
        }
        for(const auto cursor:cursors){
            if(cursor!=UINT16_MAX&&cursor>=nodes.size())return false;
        }
        return true;
    }
    friend bool operator==(const Snapshot& a,const Snapshot& b) noexcept {
        return a.nodes==b.nodes&&a.links==b.links&&a.cursors==b.cursors&&a.tilt==b.tilt&&a.zoom==b.zoom;
    }
};
inline bool Encode(const Key& key,const Snapshot& state,Bytes& output) noexcept {
    if(!key.Valid()||!state.Valid())return false;
    try {
        const auto payload=state.nodes.size()*2+state.links.size()+kTail;
        Bytes bytes(kHeader+payload+kChecksum,0);
        std::copy(kMagic.begin(),kMagic.end(),bytes.begin());
        Detail::Put32(bytes,8,2);Detail::Put32(bytes,12,static_cast<std::uint32_t>(kHeader));
        Detail::Put32(bytes,16,static_cast<std::uint32_t>(state.nodes.size()));
        Detail::Put32(bytes,20,static_cast<std::uint32_t>(state.links.size()));
        Detail::Put32(bytes,24,static_cast<std::uint32_t>(kCharacters));
        Detail::Put32(bytes,28,static_cast<std::uint32_t>(payload));
        std::copy(key.path.begin(),key.path.end(),bytes.begin()+32);
        std::copy(key.image.begin(),key.image.end(),bytes.begin()+64);
        std::copy(key.layout.begin(),key.layout.end(),bytes.begin()+96);
        std::copy(key.contents.begin(),key.contents.end(),bytes.begin()+128);
        std::size_t at=kHeader;
        for(const auto node:state.nodes){bytes[at++]=node.content;bytes[at++]=node.mask;}
        for(const auto mask:state.links)bytes[at++]=mask;
        for(const auto cursor:state.cursors){
            bytes[at++]=static_cast<std::uint8_t>(cursor);
            bytes[at++]=static_cast<std::uint8_t>(cursor>>8);
        }
        bytes[at++]=state.tilt;bytes[at++]=state.zoom;
        Detail::Put32(bytes,at,Detail::Crc32(bytes.data(),at));
        output.swap(bytes);return true;
    }catch(...){return false;}
}
inline bool Decode(const Bytes& bytes,const Key& key,Snapshot& output) noexcept {
    if(!key.Valid()||bytes.size()<kHeader+2+kTail+kChecksum||bytes.size()>kMaxBytes)return false;
    if(!std::equal(kMagic.begin(),kMagic.end(),bytes.begin())||Detail::Get32(bytes,8)!=2||
       Detail::Get32(bytes,12)!=kHeader||Detail::Get32(bytes,24)!=kCharacters||!Detail::Matches(bytes,key))return false;
    const auto nodes=Detail::Get32(bytes,16),links=Detail::Get32(bytes,20);
    if(!nodes||nodes>kMaxNodes||links>kMaxLinks)return false;
    const auto payload=std::size_t(nodes)*2+links+kTail;
    if(Detail::Get32(bytes,28)!=payload||bytes.size()!=kHeader+payload+kChecksum||
       Detail::Get32(bytes,bytes.size()-kChecksum)!=Detail::Crc32(bytes.data(),bytes.size()-kChecksum))return false;
    try {
        Snapshot state;state.nodes.resize(nodes);state.links.resize(links);std::size_t at=kHeader;
        for(auto& node:state.nodes){node.content=bytes[at++];node.mask=bytes[at++];}
        for(auto& mask:state.links)mask=bytes[at++];
        for(auto& cursor:state.cursors){
            cursor=static_cast<std::uint16_t>(bytes[at]|(std::uint16_t(bytes[at+1])<<8));at+=2;
        }
        state.tilt=bytes[at++];state.zoom=bytes[at++];
        if(!state.Valid())return false;
        output=std::move(state);return true;
    }catch(...){return false;}
}
// Migration is explicit: the caller supplies a validated eighth cursor or
// UINT16_MAX. Never invent a starting node or inherit another character's path.
inline bool ImportSeven(const SphereGridProgress::Snapshot& source,std::uint16_t cursor,Snapshot& output) noexcept {
    if(!source.Valid()||(cursor!=UINT16_MAX&&cursor>=source.nodes.size()))return false;
    try {
        Snapshot state;state.nodes=source.nodes;state.links=source.links;
        std::copy(source.cursors.begin(),source.cursors.end(),state.cursors.begin());
        state.cursors[7]=cursor;state.tilt=source.tilt;state.zoom=source.zoom;
        output=std::move(state);return true;
    }catch(...){return false;}
}
// An explicit projection for native compatibility and verification. It does not
// erase or save the v2 companion; the caller must preserve its eighth progress.
inline bool ProjectSeven(const Snapshot& source,SphereGridProgress::Snapshot& output) noexcept {
    if(!source.Valid())return false;
    try {
        SphereGridProgress::Snapshot state;state.nodes=source.nodes;state.links=source.links;
        for(auto& node:state.nodes)node.mask&=0x7f;
        for(auto& mask:state.links)mask&=0x7f;
        std::copy_n(source.cursors.begin(),state.cursors.size(),state.cursors.begin());
        state.tilt=source.tilt;state.zoom=source.zoom;
        if(!state.Valid())return false;
        output=std::move(state);return true;
    }catch(...){return false;}
}
} // namespace FfxHooks::SphereGridProgress8
