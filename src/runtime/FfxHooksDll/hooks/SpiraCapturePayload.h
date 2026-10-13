#pragma once
// Jarvis-HOOK: DATA v2 permits only the existing native Capture bit, only on
// its172 active rows. This does not admit any other payload or retired ability.
namespace FfxHooks::SpiraCapturePayload {
inline constexpr unsigned Offset=0x66,Bit=4;
inline constexpr bool Allowed(unsigned id) noexcept {
    if(id>186)return false;
    switch(id){case 13:case 17:case 24:case 123:case 129:case 130:case 131:case 132:
        case 133:case 134:case 154:case 157:case 170:case 171:case 174:return false;
        default:return true;}
}
inline constexpr bool Matches(unsigned id,unsigned at,unsigned actual,unsigned expected=0) noexcept {
    return actual==expected||(at==Offset&&Allowed(id)&&actual==(expected|Bit));
}
}
