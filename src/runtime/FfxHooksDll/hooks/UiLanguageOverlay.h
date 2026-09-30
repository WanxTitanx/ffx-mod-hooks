#pragma once
// Jarvis-HOOK: bounded menu-to-Present publication. No game memory or new hook.
#include "UiCaptionFrame.h"
#include <windows.h>
#include <atomic>
namespace FfxHooks::UiOverlay {
inline SRWLOCK Lock=SRWLOCK_INIT;
inline UiCaption::Frame Published{};
inline std::uint32_t Epoch=0;
inline std::atomic<std::uint32_t> Revision{0};
inline std::atomic<int> ReadyLocale{-1};
inline std::atomic<std::uint32_t> ReadyTick{0};
inline thread_local UiCaption::Frame* Building=nullptr;
inline UiLanguage::Locale Displayable(UiLanguage::Locale requested) noexcept {
    return ReadyLocale.load(std::memory_order_acquire)==static_cast<int>(requested)&&
        UiCaption::Fresh(ReadyTick.load(std::memory_order_acquire),GetTickCount())
        ?requested:UiLanguage::Locale::English;
}
inline void Unavailable() noexcept {ReadyLocale.store(-1,std::memory_order_release);}
inline void Clear() noexcept {
    AcquireSRWLockExclusive(&Lock);
    ReadyLocale.store(-1,std::memory_order_release);
    ++Epoch;Published.count=0;Published.locale=UiLanguage::Locale::English;Published.epoch=Epoch;
    Revision.fetch_add(1,std::memory_order_release);
    ReleaseSRWLockExclusive(&Lock);
}
inline bool WantsSurface(std::uint32_t now) noexcept {
    AcquireSRWLockShared(&Lock);
    const bool result=Published.locale!=UiLanguage::Locale::English&&UiCaption::Fresh(Published.stamp,now);
    ReleaseSRWLockShared(&Lock);return result;
}
inline bool Copy(UiCaption::Frame& out,std::uint32_t now,std::uint32_t* revision=nullptr) noexcept {
    AcquireSRWLockShared(&Lock);
    const bool valid=Published.locale!=UiLanguage::Locale::English&&UiCaption::Fresh(Published.stamp,now);
    if(valid)out=Published;else {out.count=0;out.locale=UiLanguage::Locale::English;}
    if(revision)*revision=Revision.load(std::memory_order_relaxed);
    ReleaseSRWLockShared(&Lock);return valid;
}
inline bool Current(UiLanguage::Locale locale,std::uint32_t epoch,std::uint32_t revision,
                    bool confirm=false) noexcept {
    AcquireSRWLockShared(&Lock);
    const auto now=GetTickCount();
    const bool current=locale!=UiLanguage::Locale::English&&epoch==Epoch&&
        revision==Revision.load(std::memory_order_relaxed)&&Published.locale==locale&&
        UiCaption::Fresh(Published.stamp,now);
    if(confirm){
        if(current){ReadyTick.store(now,std::memory_order_release);ReadyLocale.store(static_cast<int>(locale),std::memory_order_release);}
        else Unavailable();
    }
    ReleaseSRWLockShared(&Lock);return current;
}
inline bool SameContent(const UiCaption::Frame& a,const UiCaption::Frame& b) noexcept {
    if(a.locale!=b.locale||a.count!=b.count||a.epoch!=b.epoch)return false;
    for(unsigned i=0;i<a.count;++i){
        const auto& x=a.lines[i];const auto& y=b.lines[i];
        if(x.x!=y.x||x.y!=y.y||x.width!=y.width||x.height!=y.height||
           x.title!=y.title||x.wrap!=y.wrap||x.center!=y.center||
           std::strcmp(x.text.data(),y.text.data())!=0)return false;
    }
    return true;
}
// The native game has a small x86 stack. Keep the bounded caption buffer in TLS.
inline thread_local UiCaption::Frame Staging{};
inline thread_local bool InFrame=false;
class FrameScope {
    bool owner_=false;
    UiCaption::Frame* previous_=nullptr;
    UiLanguage::Locale previousLocale_=UiLanguage::Locale::English;
public:
    explicit FrameScope(UiLanguage::Locale locale) noexcept {
        previous_=Building;previousLocale_=UiLanguage::DisplayLocale;
        owner_=!InFrame;
        if(!owner_){Building=nullptr;UiLanguage::DisplayLocale=UiLanguage::Locale::English;return;}
        InFrame=true;Staging.count=0;Staging.locale=locale;Staging.stamp=GetTickCount();
        AcquireSRWLockShared(&Lock);Staging.epoch=Epoch;ReleaseSRWLockShared(&Lock);
        const bool ready=locale!=UiLanguage::Locale::English&&Displayable(locale)==locale;
        Building=ready?&Staging:nullptr;
        UiLanguage::DisplayLocale=ready?locale:UiLanguage::Locale::English;
    }
    ~FrameScope() noexcept {
        Building=previous_;UiLanguage::DisplayLocale=previousLocale_;
        if(!owner_)return;
        InFrame=false;
        AcquireSRWLockExclusive(&Lock);
        // A close/focus-loss callback invalidates even a draw that began earlier.
        if(Staging.epoch==Epoch){
            const bool changed=!SameContent(Published,Staging);
            Published=Staging;
            if(changed)Revision.fetch_add(1,std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&Lock);
    }
    FrameScope(const FrameScope&)=delete;FrameScope& operator=(const FrameScope&)=delete;
};
inline bool Caption(const char* text,float x,float y,float width,float height,
                    bool title=false,bool wrap=false,bool center=false) noexcept {
    return Building&&Building->Add(text,x,y,width,height,title,wrap,center);
}
}
