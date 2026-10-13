#pragma once
// Jarvis-HOOK: this opt-in Omega adjunct is distinct from normal status chance
// and elemental Imperil. The native adapter owns actor/action and property ABI.
#include <cstdint>
namespace FfxHooks::OmegaLowMagic {
struct Context {
    bool naturalOmega=false,monster=false,hostile=false,alive=false;
    bool offensiveMagic=false,landed=false,absorbed=false;
    unsigned magic=255,nativeElements=0;bool bio=false;
};
struct Effect {unsigned field=0,value=0,offset=0,mask=0;bool duration=false;};
inline constexpr Effect Mapping[]={
    {0x29,1,0x606,0x100,false}, // Fire: Confusion
    {0x39,3,0x614,0,true},      // Ice: Slow
    {0x2F,3,0x60A,0,true},     // Lightning: Darkness
    {5,1,0x606,8,false},       // Water: Poison
    {0x2D,3,0x608,0,true},     // Aero: Sleep
    {0x2E,3,0x609,0,true},     // Holy: Silence
    {7,1,0x606,2,false},       // Darkness: Zombie (field0x23 is SENSOR, never use it)
    {0x28,1,0x606,0x80,false}  // Neutral: Mental Break
};
inline constexpr unsigned Bits[]={1,2,4,8,0x40,0x10,0x80};
inline Effect Select(const Context& c) noexcept {
    if(!c.naturalOmega||!c.monster||!c.hostile||!c.alive||!c.offensiveMagic||!c.landed||c.absorbed||c.magic>=80)return {};
    for(unsigned i=0;i<7;++i)if(c.nativeElements&Bits[i])return Mapping[i];
    return c.bio?Mapping[3]:Mapping[7];
}
inline bool Needed(const Effect& effect,unsigned current) noexcept {
    return effect.field&&(effect.duration?current<effect.value:(current&effect.mask)==0);
}
}
