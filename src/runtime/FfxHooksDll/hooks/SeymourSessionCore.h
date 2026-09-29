#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::SeymourSession {
inline constexpr std::size_t Capacity=16;
struct Token {
    std::uint64_t revision=0;
    std::uint32_t thread=0;
    bool Valid() const noexcept {return revision!=0&&thread!=0;}
    friend bool operator==(const Token& a,const Token& b) noexcept {
        return a.revision==b.revision&&a.thread==b.thread;
    }
};
struct Attempt {
    std::uint64_t cookie=0,revision=0;
    std::uint32_t thread=0;
    bool Valid() const noexcept {return cookie&&revision&&thread;}
    friend bool operator==(const Attempt& a,const Attempt& b) noexcept {
        return a.cookie==b.cookie&&a.revision==b.revision&&a.thread==b.thread;
    }
};
// The adapter serializes this bounded state. A file preview is deliberately not
// an input: only an actual load-copy attempt or completed new-game reset enters.
class Core {
public:
    bool Poisoned() const noexcept {return poisoned_;}
    void Invalidate() noexcept {
        active_={};
        if(revision_==UINT64_MAX){poisoned_=true;return;}
        ++revision_;
    }
    Attempt Begin(std::uint64_t cookie,std::uint32_t thread) noexcept {
        Invalidate();
        if(poisoned_||!cookie||!thread||cookie<=lastCookie_)return {};
        lastCookie_=cookie;
        for(auto& slot:pending_)if(!slot.cookie){
            slot={cookie,revision_,thread};return slot;
        }
        poisoned_=true;return {};
    }
    bool End(const Attempt& input,bool completed,std::uint32_t thread) noexcept {
        if(!input.cookie)return false;
        for(auto& slot:pending_)if(slot.cookie==input.cookie){
            const auto expected=slot;slot={};
            if(!(input==expected)||expected.thread!=thread){Invalidate();return false;}
            if(poisoned_||stopped_||!completed||Pending()||expected.revision!=revision_)return false;
            active_={revision_,thread};return true;
        }
        // Replaying a completed callback cannot revoke a later valid session.
        return false;
    }
    bool Reset(std::uint32_t thread) noexcept {
        Invalidate();
        if(poisoned_||stopped_||Pending()||!thread)return false;
        active_={revision_,thread};return true;
    }
    Token Capture(std::uint32_t thread,bool cleanup=false) const noexcept {
        return Current(active_,thread,cleanup)?active_:Token{};
    }
    bool Current(const Token& token,std::uint32_t thread,bool cleanup=false) const noexcept {
        return token.Valid()&&token==active_&&token.thread==thread&&
               !poisoned_&&!Pending()&&(!stopped_||cleanup);
    }
    // Loader-lock stop belongs in the adapter's atomic admission. This normal
    // context operation leaves only current-session cleanup admissible.
    void Stop() noexcept {stopped_=true;}
private:
    bool Pending() const noexcept {
        for(const auto& slot:pending_)if(slot.cookie)return true;
        return false;
    }
    std::array<Attempt,Capacity> pending_{};
    Token active_{};
    std::uint64_t revision_=0,lastCookie_=0;
    bool stopped_=false,poisoned_=false;
};
} // namespace FfxHooks::SeymourSession
