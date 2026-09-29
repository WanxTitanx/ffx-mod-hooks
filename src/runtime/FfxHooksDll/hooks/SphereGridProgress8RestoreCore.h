#pragma once
#include "SphereGridProgress8Core.h"

namespace FfxHooks::SphereGridProgress8 {
// Jarvis-HOOK. Grid8 restores only bit 7 and the eighth cursor. Session identity
// alone does not authorize reverting the native seven or shared layout state.
// Logical-node expansion needs its own restoration contract, not an exception
// to this guard. This predicate has no allocation and never changes its inputs.
inline bool CanRestoreEighth(const Snapshot& current,const Snapshot& saved) noexcept {
    if(!current.Valid()||!saved.Valid()||current.nodes.size()!=saved.nodes.size()||
       current.links.size()!=saved.links.size()||current.tilt!=saved.tilt||current.zoom!=saved.zoom)return false;
    for(std::size_t i=0;i<current.nodes.size();++i){
        if(current.nodes[i].content!=saved.nodes[i].content||
           (current.nodes[i].mask&0x7fu)!=(saved.nodes[i].mask&0x7fu))return false;
    }
    for(std::size_t i=0;i<current.links.size();++i){
        if((current.links[i]&0x7fu)!=(saved.links[i]&0x7fu))return false;
    }
    for(std::size_t i=0;i<7;++i){
        if(current.cursors[i]!=saved.cursors[i])return false;
    }
    return true;
}
}
