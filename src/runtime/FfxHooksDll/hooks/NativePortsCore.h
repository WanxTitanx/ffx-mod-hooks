#pragma once
#include <cstdint>

namespace FfxHooks::NativePorts {
struct Settings {
    bool blockWindows=false, backgroundInput=false, filterIme=false;
    bool borderless=false, clipCursor=false, hideCursor=false, performance=false;
};
enum class MessageAction : std::uint8_t { Forward, Drop, DefaultProcess };
struct CameraScope { bool focused=false,menu=false,battle=false,fieldReady=false; };
struct IdleCursorState {
    bool sampled=false;
    int x=0,y=0;
    std::uint32_t lastMotion=0;
};
inline bool HideIdleCursor(IdleCursorState& state,std::uint32_t now,int x,int y,bool eligible) noexcept {
    if(!eligible){state.sampled=false;return false;}
    if(!state.sampled || state.x!=x || state.y!=y){state={true,x,y,now};return false;}
    return static_cast<std::uint32_t>(now-state.lastMotion)>=2000u;
}
inline bool FreeCameraAllowed(CameraScope scope) noexcept {return scope.focused && !scope.menu && scope.battle;}
inline bool FreezeSceneAllowed(CameraScope scope) noexcept {return scope.focused && !scope.menu && !scope.battle && scope.fieldReady;}
inline bool BlockWindowsKey(const Settings& config, bool focused, unsigned key) noexcept {
    return config.blockWindows && focused && (key==0x5Bu || key==0x5Cu);
}
inline MessageAction FilterMessage(const Settings& config, bool focused,
    bool textEditor, unsigned message) noexcept {
    if (config.backgroundInput && !focused) {
        if (message==0x00FFu) return MessageAction::DefaultProcess; // WM_INPUT cleanup
        if ((message>=0x0100u && message<=0x0109u) ||
            (message>=0x0200u && message<=0x020Eu)) return MessageAction::Drop;
    }
    if (config.filterIme && !textEditor &&
        ((message>=0x010Du && message<=0x010Fu) ||
         (message>=0x0281u && message<=0x0291u))) return MessageAction::Drop;
    return MessageAction::Forward;
}
struct StyleOwner { bool owned=false; std::uint32_t original=0, applied=0; };
struct StylePlan { bool write=false, conflict=false; std::uint32_t value=0; };
inline StylePlan PlanStyle(std::uint32_t observed,bool enabled,const StyleOwner& owner) noexcept {
    if (owner.owned && observed!=owner.applied) return {false,true,observed};
    const std::uint32_t desired=enabled ? ((observed & ~0x00CF0000u)|0x80000000u)
        : (owner.owned ? owner.original : observed);
    return {desired!=observed,false,desired};
}
inline void CommitStyle(std::uint32_t original,std::uint32_t applied,StyleOwner& owner) noexcept {
    if (!owner.owned) owner.original=original;
    owner.applied=applied;owner.owned=true;
}
} // namespace FfxHooks::NativePorts
