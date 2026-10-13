#pragma once
#include "ArcanaPresentation.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::Arcana::FieldDialog {
enum class Action {None,Close,Release,Abandon};
struct Sample {
    std::uint64_t generation=0;
    std::uint16_t scene=0,window=0,text=0;
    bool owned=false,available=false,focused=false,suspended=false;
    bool foreign=false,stopping=false,confirm=false,back=false;
    bool previous=false;
};
struct Decision {
    Action action=Action::None;
    Presentation::Navigation navigation=Presentation::Navigation::None;
    Presentation::View view{};
    std::uint64_t generation=0;
};
class Controller {
public:
    bool Begin(std::uint64_t generation,std::uint16_t scene,const Presentation::View&) noexcept;
    Decision Poll(const Sample&) noexcept;
    bool Active() const noexcept {return active_;}
    void Reset() noexcept;
private:
    bool active_=false,closing_=false,cancelArmed_=false;
    std::uint64_t generation_=0;
    std::uint16_t scene_=0;
    Presentation::View view_{};
    Presentation::Navigation completion_=Presentation::Navigation::None;
    Presentation::InputLatch input_{};
};
struct EncodedText {
    std::array<unsigned char,2048> bytes{};
    std::size_t size=0;
};
bool Encode(const char* literal,EncodedText&) noexcept;
bool Wrap(const char*,char*,std::size_t,unsigned columns) noexcept;
}
