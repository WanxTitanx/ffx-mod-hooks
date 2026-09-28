#pragma once
#include "ArcanaUiCore.h"

namespace FfxHooks::Arcana::NativeUi {
struct Quad {unsigned resource=79;float x=0,y=0,width=0,height=0;};
struct Images {std::uint64_t generation=0;unsigned count=0;std::array<Quad,4> quads{};};
struct Callbacks {
    bool (*capture)(State&,std::uint64_t& session) noexcept=nullptr;
    Error (*equip)(std::uint64_t session,std::uint64_t revision,unsigned actor,unsigned slot,std::int16_t card,bool transfer) noexcept=nullptr;
    Error (*mode)(std::uint64_t session,std::uint64_t revision,Mode,bool releaseThird) noexcept=nullptr;
    void (*images)(const Images&) noexcept=nullptr;
    void (*tick)() noexcept=nullptr;
    const char* (*detail)() noexcept=nullptr;
};
bool Start(std::uintptr_t base,bool requested,bool validateOnly,const Callbacks&,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
bool TextDrawingActive() noexcept;
}
