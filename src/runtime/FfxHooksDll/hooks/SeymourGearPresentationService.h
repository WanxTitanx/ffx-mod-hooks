#pragma once
#include "SeymourGearPresentationCore.h"
namespace FfxHooks::SeymourGearPresentation {
struct Io {
    void* context=nullptr;
    const std::uint8_t* (*original)(void*,unsigned,unsigned,int,std::uint16_t*)=nullptr;
    bool (*current)(void*)=nullptr;
    bool (*model)(void*,std::uint16_t*,std::uint16_t)=nullptr;
};
// No ownership of game inventory is acquired. This replaces the native query's
// return and optional two-byte output only; retained strings are immutable.
inline const std::uint8_t* Service(bool requested,unsigned name,unsigned owner,int simplified,
                                  std::uint16_t* model,const Io& io) {
    Presentation value{};
    if(requested&&Resolve(static_cast<std::uint16_t>(name),static_cast<std::uint8_t>(owner),simplified!=0,value)&&
       io.current&&io.model&&io.current(io.context)&&io.current(io.context)&&
       io.model(io.context,model,value.model))return value.text;
    return io.original?io.original(io.context,name,owner,simplified,model):nullptr;
}
}
