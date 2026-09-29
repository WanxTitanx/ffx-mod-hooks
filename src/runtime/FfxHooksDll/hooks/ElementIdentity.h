#pragma once
#include <array>

namespace FfxHooks::ElementIdentity {
inline constexpr unsigned NativeCount=8,HookCount=2,Count=NativeCount+HookCount;
inline constexpr unsigned NativeBits[NativeCount]={1,2,4,8,0x10,0x80,0x20,0x40};
struct Definition {const char* key;const char* label;unsigned nativeBit,rgb;};
// Display aliases never change these identities, native bits or command bindings.
inline constexpr std::array<Definition,Count> Defaults{{
    {"native.fire","Fire",1,0xFFFFFF},
    {"native.ice","Ice",2,0xFFFFFF},
    {"native.thunder","Thunder",4,0xFFFFFF},
    {"native.water","Water",8,0xFFFFFF},
    {"native.holy","Holy",0x10,0xFFE080},
    {"native.darkness","Darkness",0x80,0xA35CEF},
    {"native.custom01","Earth",0x20,0x5FCF7E},
    {"native.custom02","Wind",0x40,0x6FB5FF},
    {"hook.custom03","Poison",0,0xFFFFFF},
    {"hook.custom04","Gravity",0,0xFFFFFF}
}};
}
