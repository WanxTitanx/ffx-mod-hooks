#pragma once
#include <windows.h>
#include "ElementNameSettings.h"

namespace FfxHooks::ElementNameInput {
inline SRWLOCK lock=SRWLOCK_INIT;
inline char draft[65]{};
inline bool selectAll=false;
inline volatile LONG active=0,confirmed=0,cancelled=0,rejected=0;
inline bool Active() noexcept {return InterlockedCompareExchange(&active,0,0)!=0;}
inline bool Acceptable() noexcept {return InterlockedCompareExchange(&rejected,0,0)==0;}
inline void Abort() noexcept {
    InterlockedExchange(&active,0);InterlockedExchange(&confirmed,0);
    InterlockedExchange(&cancelled,0);InterlockedExchange(&rejected,0);
}
inline void Begin(const char* initial) noexcept {
    Abort();AcquireSRWLockExclusive(&lock);
    std::snprintf(draft,sizeof(draft),"%s",initial?initial:"");selectAll=true;
    ReleaseSRWLockExclusive(&lock);InterlockedExchange(&active,1);
}
inline void Copy(char (&out)[65]) noexcept {
    AcquireSRWLockShared(&lock);std::memcpy(out,draft,sizeof(draft));ReleaseSRWLockShared(&lock);
}
inline bool Consume(char (&out)[65],bool& cancel) noexcept {
    cancel=InterlockedExchange(&cancelled,0)!=0;
    if(!cancel&&!InterlockedExchange(&confirmed,0))return false;
    if(!cancel&&!Acceptable())return false;
    if(!cancel)Copy(out);Abort();return true;
}
inline bool Message(UINT message,WPARAM value) noexcept {
    if(!Active())return false;
    if(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!value)){
        InterlockedExchange(&active,0);InterlockedExchange(&cancelled,1);return false;
    }
    if(message==WM_KEYDOWN){
        if(value==VK_RETURN)InterlockedExchange(&confirmed,1);
        else if(value==VK_ESCAPE)InterlockedExchange(&cancelled,1);
        else if(value=='A'&&(GetAsyncKeyState(VK_CONTROL)&0x8000)){
            AcquireSRWLockExclusive(&lock);selectAll=true;ReleaseSRWLockExclusive(&lock);
        }else if(value==VK_DELETE){
            AcquireSRWLockExclusive(&lock);draft[0]=0;selectAll=false;ReleaseSRWLockExclusive(&lock);InterlockedExchange(&rejected,0);
        }
        return true;
    }
    if(message==WM_KEYUP)return true;
    if(message!=WM_CHAR)return false;
    AcquireSRWLockExclusive(&lock);std::size_t length=std::strlen(draft);
    if(value==8){if(selectAll)length=0;else if(length)--length;draft[length]=0;selectAll=false;InterlockedExchange(&rejected,0);}
    else if(value>=32){
        if(value<=126&&ElementNames::Character(static_cast<unsigned char>(value))){
            if(selectAll){length=0;draft[0]=0;selectAll=false;}
            if(length<ElementNames::MaximumLength){draft[length]=static_cast<char>(value);draft[length+1]=0;}
            else InterlockedExchange(&rejected,1);
        }else InterlockedExchange(&rejected,1);
    }
    ReleaseSRWLockExclusive(&lock);return true;
}
}
