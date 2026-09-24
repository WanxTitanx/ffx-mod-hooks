#pragma once
#include "CustomMixUltraCore.h"
#include <cstdio>
#include <cstddef>
#include <cstring>

namespace FfxHooks::ArenaBrowser {
inline void FormatSummary(const CustomMixUltra::SelectionInput& selection,unsigned required,
                          const char* status,char* output,std::size_t capacity) noexcept {
    if(!output || !capacity)return;
    const auto result=CustomMixUltra::BuildSelection(selection);
    const char* mode=!selection.positions.enabled?"Native":selection.positions.automatic?"Auto":"Manual";
    // Names belong in the formation pane. Their combined length must never reach
    // the CRT invalid-parameter handler when Back rebuilds this 64-byte subtitle.
    std::snprintf(output,capacity,"Slots %u/%u  [%.16s]  %s",
        static_cast<unsigned>(result.expanded.monsterCount),required?required:8u,
        status?status:"UNKNOWN",mode);
    output[capacity-1]=0;
}
inline unsigned char Fold(unsigned char value) noexcept {
    return value>='A'&&value<='Z'?static_cast<unsigned char>(value-'A'+'a'):value;
}
inline bool Contains(const char* text,const char* word,std::size_t length) noexcept {
    if(!text)return false;
    for(const char* start=text;*start;++start){
        std::size_t n=0;
        while(n<length && start[n] && Fold(static_cast<unsigned char>(start[n]))==Fold(static_cast<unsigned char>(word[n])))++n;
        if(n==length)return true;
    }
    return length==0;
}
inline bool Matches(const char* query,const char* name,const char* key,const char* category="") noexcept {
    if(!query)return true;
    while(*query){
        while(*query==' ')++query;
        const char* word=query;while(*query && *query!=' ')++query;
        const auto length=static_cast<std::size_t>(query-word);
        if(length && !Contains(name,word,length) && !Contains(key,word,length) && !Contains(category,word,length))return false;
    }
    return true;
}
}
