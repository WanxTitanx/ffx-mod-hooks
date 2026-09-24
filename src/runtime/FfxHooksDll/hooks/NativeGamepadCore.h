#pragma once
#include <array>
#include <cstdint>
#include <cstdio>

namespace FfxHooks::NativeGamepad {
enum Button : std::uint32_t {
    DpadUp=1,DpadDown=2,DpadLeft=4,DpadRight=8,StartButton=0x10,Back=0x20,
    LeftStick=0x40,RightStick=0x80,LeftShoulder=0x100,RightShoulder=0x200,
    A=0x1000,B=0x2000,X=0x4000,Y=0x8000,LeftTrigger=0x10000,RightTrigger=0x20000
};
inline constexpr std::uint32_t kAllowedButtons=0x3F3FFu;
inline bool ValidCombo(std::uint32_t mask) noexcept {
    if(!mask)return true;
    return !(mask&~kAllowedButtons) && (mask&(mask-1u))!=0;
}
inline std::uint32_t Buttons(std::uint16_t buttons,unsigned char left,unsigned char right) noexcept {
    return (buttons&0xF3FFu)|(left>30?LeftTrigger:0u)|(right>30?RightTrigger:0u);
}
struct EdgeState {std::uint32_t previous=0;};
inline bool Pressed(EdgeState& state,std::uint32_t current,std::uint32_t binding,bool admitted) noexcept {
    const auto prior=state.previous;state.previous=current;
    return admitted && binding && ValidCombo(binding) && current==binding && (prior&binding)!=binding;
}
struct CaptureResult {bool done=false,valid=false;std::uint32_t mask=0;};
class Capture {
public:
    void Begin(std::uint32_t held) noexcept {active_=true;waitingRelease_=held!=0;gathered_=0;}
    void Cancel() noexcept {active_=false;waitingRelease_=false;gathered_=0;}
    bool Active() const noexcept {return active_;}
    std::uint32_t Gathered() const noexcept {return gathered_;}
    CaptureResult Sample(std::uint32_t held) noexcept {
        if(!active_)return {};
        if(waitingRelease_){if(!held)waitingRelease_=false;return {};}
        if(held){gathered_|=held;return {};}
        if(!gathered_)return {};
        const auto mask=gathered_;Cancel();return {true,ValidCombo(mask),mask};
    }
private:
    bool active_=false,waitingRelease_=false;
    std::uint32_t gathered_=0;
};
inline constexpr std::array<std::uint16_t,10> kRemappable={A,B,X,Y,LeftShoulder,RightShoulder,Back,StartButton,LeftStick,RightStick};
inline constexpr std::array<const char*,10> kButtonNames={"A","B","X","Y","LB","RB","Back","Start","L-stick","R-stick"};
using ButtonMap=std::array<unsigned char,kRemappable.size()>;
inline ButtonMap IdentityMap() noexcept {return {0,1,2,3,4,5,6,7,8,9};}
inline bool ValidMap(const ButtonMap& map) noexcept {
    unsigned seen=0;
    for(auto to:map){if(to>=map.size() || (seen&(1u<<to)))return false;seen|=1u<<to;}
    return true;
}
inline bool Changed(const ButtonMap& map) noexcept {for(unsigned i=0;i<map.size();++i)if(map[i]!=i)return true;return false;}
inline bool SwapDestination(ButtonMap& map,unsigned from,unsigned destination) noexcept {
    if(!ValidMap(map) || from>=map.size() || destination>=map.size())return false;
    for(unsigned i=0;i<map.size();++i)if(map[i]==destination){map[i]=map[from];map[from]=static_cast<unsigned char>(destination);return true;}
    return false;
}
inline std::uint16_t Remap(std::uint16_t input,const ButtonMap& map) noexcept {
    if(!ValidMap(map))return input;
    std::uint16_t output=input;
    for(const auto bit:kRemappable)output=static_cast<std::uint16_t>(output&~bit);
    for(unsigned i=0;i<map.size();++i)if(input&kRemappable[i])output|=kRemappable[map[i]];
    return output;
}
inline bool Format(std::uint32_t mask,char* output,std::size_t capacity) noexcept {
    if(!output || !capacity || (mask&~kAllowedButtons))return false;
    if(!mask){const auto n=std::snprintf(output,capacity,"Unassigned");return n>=0 && static_cast<std::size_t>(n)<capacity;}
    constexpr std::array<std::uint32_t,16> bits={Back,StartButton,LeftShoulder,RightShoulder,LeftTrigger,RightTrigger,LeftStick,RightStick,DpadUp,DpadDown,DpadLeft,DpadRight,A,B,X,Y};
    constexpr std::array<const char*,16> names={"Back","Start","LB","RB","LT","RT","L-stick","R-stick","Up","Down","Left","Right","A","B","X","Y"};
    std::size_t used=0;output[0]=0;
    for(unsigned i=0;i<bits.size();++i)if(mask&bits[i]){
        const int n=std::snprintf(output+used,capacity-used,"%s%s",used?"+":"",names[i]);
        if(n<0 || static_cast<std::size_t>(n)>=capacity-used)return false;
        used+=static_cast<std::size_t>(n);
    }
    return true;
}
} // namespace FfxHooks::NativeGamepad
