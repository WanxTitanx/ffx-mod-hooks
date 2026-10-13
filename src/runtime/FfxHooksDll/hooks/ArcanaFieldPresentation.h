#pragma once
#include "ArcanaCore.h"
#include "ArcanaPresentation.h"
#include "ArcanaNativeUi.h"

namespace FfxHooks::Arcana::FieldPresentation {
enum class Code {Off,Waiting,Ready,Showing,Unsupported,NativeFault,Stopped};
struct FieldContext {
    std::uint64_t generation=0;
    std::uint16_t scene=0,held=0,pressed=0;
    float x=0,y=0,z=0;
    bool available=false;
};
struct FieldPrompt {const char* text=nullptr;std::uint8_t card=0;};
struct SupplementalSource {
    bool (*capture)(std::uint16_t room,bool stable,Presentation::View&,std::uint64_t&) noexcept=nullptr;
    Error (*acknowledge)(std::uint64_t,const Presentation::View&,Presentation::Navigation) noexcept=nullptr;
    bool (*format)(const Presentation::View&,char*,std::size_t) noexcept=nullptr;
    bool (*encode)(const char*,unsigned char*,std::size_t,std::size_t&) noexcept=nullptr;
};
struct Callbacks {
    bool (*capture)(Presentation::State&,std::uint64_t&) noexcept=nullptr;
    Error (*acknowledge)(std::uint64_t,const Presentation::View&,Presentation::Navigation) noexcept=nullptr;
    void (*maintenance)() noexcept=nullptr;
    void (*images)(const NativeUi::Images&) noexcept=nullptr;
    void (*world)(const FieldContext&,FieldPrompt&) noexcept=nullptr;
    bool (*focused)() noexcept=nullptr;
    SupplementalSource supplemental{};
};
bool Profile(std::uintptr_t) noexcept;
bool Start(std::uintptr_t,bool requested,bool validateOnly,const Callbacks&,void(*log)(const char*));
void Stop() noexcept;
bool Active() noexcept;
bool OverlayAllowed(const NativeUi::Images&) noexcept;
bool Project(float x,float y,float z,float& screenX,float& screenY) noexcept;
Code Status() noexcept;
}
