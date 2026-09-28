#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace FfxHooks::ElementalScanView {
inline constexpr unsigned PageSize=10,MaximumElements=32,MaximumPages=4;
struct Row {
    char label[64]{};
    std::int32_t baseBp=10000,effectiveBp=10000,equipmentBp=0;
    unsigned rgb=0xFFFFFF,imperil=0,ward=0,nul=0;
    unsigned imperilTurns=0,wardTurns=0,nulTurns=0,imperilResistanceBp=0;
    bool locked=false,imperilImmune=false;
};
struct Snapshot {std::array<Row,PageSize> rows{};unsigned count=0,total=0,page=0;std::uint64_t generation=0;};
struct Lines {char title[96]{},effects[96]{},restriction[96]{};};
inline unsigned VisibleCount(unsigned total,unsigned page) noexcept {
    if(!total||total>MaximumElements||page>=MaximumPages||page*PageSize>=total)return 0;
    const unsigned remaining=total-page*PageSize;return remaining<PageSize?remaining:PageSize;
}
inline bool Format(const Row& row,Lines& output) noexcept {
    output={};
    if(!std::memchr(row.label,0,sizeof(row.label))||row.baseBp<-10000||row.baseBp>25000||
       row.effectiveBp<-10000||row.effectiveBp>25000||row.baseBp%2500||row.effectiveBp%2500||
       row.equipmentBp<-350000||row.equipmentBp>350000||row.equipmentBp%2500||row.imperil>4||row.ward>4||row.nul>4||
       row.imperilTurns>255||row.wardTurns>255||row.nulTurns>255||row.imperilResistanceBp>10000)return false;
    char label[21]{};unsigned n=0;
    for(;n<20&&row.label[n];++n){const auto ch=static_cast<unsigned char>(row.label[n]);label[n]=ch>=32&&ch<=126?static_cast<char>(ch):'?';}
    if(row.label[n]){label[17]='.';label[18]='.';label[19]='.';}
    std::snprintf(output.title,sizeof(output.title),"%s %d%% > %d%%",label,row.baseBp/100,row.effectiveBp/100);
    std::snprintf(output.effects,sizeof(output.effects),"Gear %+dpp I%u/%u W%u/%u N%u/%u",row.equipmentBp/100,
        row.imperil,row.imperilTurns,row.ward,row.wardTurns,row.nul,row.nulTurns);
    if(row.imperilImmune)std::snprintf(output.restriction,sizeof(output.restriction),"%sImperil immune",row.locked?"LOCK | ":"");
    else if(row.imperilResistanceBp%100)std::snprintf(output.restriction,sizeof(output.restriction),"%sImperil resist %u.%02u%%",
        row.locked?"LOCK | ":"",row.imperilResistanceBp/100,row.imperilResistanceBp%100);
    else std::snprintf(output.restriction,sizeof(output.restriction),"%sImperil resist %u%%",row.locked?"LOCK | ":"",row.imperilResistanceBp/100);
    return true;
}
struct Provider {bool (*read)(unsigned actor,unsigned page,Snapshot&) noexcept=nullptr;};
inline std::atomic<const Provider*> provider{nullptr};
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->read)return false;
    const Provider* empty=nullptr;
    return provider.compare_exchange_strong(empty,value)||empty==value;
}
inline bool Unregister(const Provider* value) noexcept {
    if(!value)return false;
    const Provider* expected=value;return provider.compare_exchange_strong(expected,nullptr);
}
inline bool Capture(unsigned actor,unsigned page,Snapshot& output) noexcept {
    output={};if(actor>=31||page>=MaximumPages)return false;
    const auto* source=provider.load();if(!source||!source->read)return false;
    Snapshot candidate{};
    if(!source->read(actor,page,candidate)||provider.load()!=source||candidate.page!=page||
       !candidate.count||candidate.count!=VisibleCount(candidate.total,page))return false;
    for(unsigned i=0;i<candidate.count;++i){Lines lines;if(!Format(candidate.rows[i],lines))return false;}
    output=candidate;return true;
}
} // namespace FfxHooks::ElementalScanView
