#pragma once
#include "CaptureCascadeCore.h"
#include <cstdio>
#include <ctime>
namespace FfxHooks::CaptureCascade {
inline void StampDate(const Stamp& stamp,char* output,std::size_t capacity) noexcept {
    if(!stamp.utc){std::snprintf(output,capacity,"Not recorded");return;}
    const auto value=static_cast<std::time_t>(stamp.utc);std::tm date{};
#ifdef _WIN32
    if(gmtime_s(&date,&value)){std::snprintf(output,capacity,"Invalid date");return;}
#else
    if(!gmtime_r(&value,&date)){std::snprintf(output,capacity,"Invalid date");return;}
#endif
    std::strftime(output,capacity,"%Y-%m-%d %H:%M UTC",&date);
}
inline bool ProgressLabel(const State* state,unsigned family,int detail,char* output,std::size_t capacity) noexcept {
    if(!output||!capacity||family>=kFamilies||detail>4)return false;
    const auto mask=1u<<family;const char* name=Name(static_cast<Aeon>(family));
    if(detail<0){std::snprintf(output,capacity,"%s - %s",name,!state?"Unavailable":state->captured&mask?"Captured":state->defeated&mask?"Region awakened":state->nativeSeen&mask?"Prior victory":"Not defeated");return true;}
    if(!state){std::snprintf(output,capacity,"Load a save with Capture Cascade enabled");return true;}
    char date[32]{};
    if(detail==0)std::snprintf(output,capacity,"Region: %s",state->defeated&mask?"Awakened (T7+)":"Not awakened");
    if(detail==1){StampDate(state->defeats[family],date,sizeof(date));std::snprintf(output,capacity,"Victory: %s",date);}
    if(detail==2)std::snprintf(output,capacity,"Victory build: %.31s",state->defeats[family].build.data());
    if(detail==3){StampDate(state->captures[family],date,sizeof(date));std::snprintf(output,capacity,"Capture: %s",date);}
    if(detail==4)std::snprintf(output,capacity,"Capture build: %.31s",state->captures[family].build.data());
    return true;
}
}
