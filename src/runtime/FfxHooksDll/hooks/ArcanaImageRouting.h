#pragma once
#include "ArcanaNativeUi.h"
#include <array>
#include <cmath>

namespace FfxHooks::Arcana::ImageRouting {
enum class Source : unsigned {Menu,Message,World,Count};
inline bool Valid(const NativeUi::Images& frame) noexcept {
    if(!frame.count)return true;
    if(!frame.generation||frame.count>frame.quads.size())return false;
    for(unsigned i=0;i<frame.count;++i){const auto& q=frame.quads[i];
        if(q.resource>=kImageResourceCount||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.width)||!std::isfinite(q.height)||
           q.x<0||q.y<0||q.width<=0||q.height<=0||q.x+q.width>1.001f||q.y+q.height>1.001f)return false;
    }
    return true;
}
class Router {
    struct Channel {NativeUi::Images frame{};std::uint64_t newest=0,time=0;bool closed=false;};
    std::array<Channel,static_cast<unsigned>(Source::Count)> channels_{};
public:
    bool Publish(Source source,const NativeUi::Images& frame,std::uint64_t now,bool forcedHidden) noexcept {
        const auto index=static_cast<unsigned>(source);if(index>=channels_.size())return false;
        auto& channel=channels_[index];
        if(!Valid(frame)){channel.frame={};channel.closed=true;return false;}
        if(frame.count&&(frame.generation<channel.newest||((channel.closed||forcedHidden)&&frame.generation<=channel.newest)))return false;
        if(frame.count){channel.newest=frame.generation;channel.closed=false;}else channel.closed=true;
        channel.frame=frame;channel.time=now;return true;
    }
    Source SelectedSource(std::uint64_t now,unsigned hiddenMask,bool fieldVisible) const noexcept {
        for(unsigned i=0;i<channels_.size();++i){
            if((hiddenMask&(1u<<i))||(i!=static_cast<unsigned>(Source::Menu)&&!fieldVisible))continue;
            const auto& channel=channels_[i];
            if(!channel.closed&&channel.frame.count&&now>=channel.time&&now-channel.time<=500)return static_cast<Source>(i);
        }
        return Source::Count;
    }
    NativeUi::Images Select(std::uint64_t now,unsigned hiddenMask,bool fieldVisible) const noexcept {
        const auto source=SelectedSource(now,hiddenMask,fieldVisible);
        return source==Source::Count?NativeUi::Images{}:channels_[static_cast<unsigned>(source)].frame;
    }
};
}
