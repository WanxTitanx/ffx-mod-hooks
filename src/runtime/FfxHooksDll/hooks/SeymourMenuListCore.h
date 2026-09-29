#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Supported native menu constructor: VA 0x008A8EF0, exact 78ce3439... image.
// The list at RVA 0x1441BD4 already has eight bytes before the count fields.
// This builds its data only; it does not grant membership or character abilities.
namespace FfxHooks::SeymourMenuList {
inline constexpr unsigned Capacity=8;
struct Roster {
    std::array<std::uint8_t,3> front{};
    std::array<std::uint8_t,17> reserve{};
    std::array<std::uint8_t,Capacity> flags{};
    friend bool operator==(const Roster& a,const Roster& b) noexcept {
        return a.front==b.front&&a.reserve==b.reserve&&a.flags==b.flags;
    }
};
struct List {
    std::array<std::uint8_t,Capacity> rows{};
    std::uint32_t current=0,total=0,frontline=0,selection=0;
    std::uint32_t frontMask=0,currentMask=0,totalMask=0;
    friend bool operator==(const List& a,const List& b) noexcept {
        return a.rows==b.rows&&a.current==b.current&&a.total==b.total&&
            a.frontline==b.frontline&&a.selection==b.selection&&
            a.frontMask==b.frontMask&&a.currentMask==b.currentMask&&a.totalMask==b.totalMask;
    }
};
static_assert(std::is_trivially_copyable<List>::value&&sizeof(List)==36,"native menu record layout");
static_assert(offsetof(List,current)==8&&offsetof(List,totalMask)==32,"native count/mask offsets");
inline bool ValidRoster(const Roster& r) noexcept {
    unsigned seen=0;
    const auto member=[&seen](std::uint8_t id){
        if(id==255)return true;
        if(id>=Capacity||(seen&(1u<<id)))return false;
        seen|=1u<<id;return true;
    };
    for(auto id:r.front)if(!member(id))return false;
    for(auto id:r.reserve)if(!member(id))return false;
    return true;
}
inline bool ValidList(const List& list) noexcept {
    if(list.total>Capacity||list.current>list.total||list.frontline>list.current||
       (list.total?list.selection>=list.total:list.selection!=0))return false;
    unsigned front=0,current=0,total=0;
    for(unsigned i=0;i<list.total;++i){
        const unsigned id=list.rows[i];
        if(id>=Capacity||(total&(1u<<id)))return false;
        total|=1u<<id;
        if(i<list.current)current|=1u<<id;
        if(i<list.frontline)front|=1u<<id;
    }
    return list.frontMask==front&&list.currentMask==current&&list.totalMask==total;
}
inline bool Build(bool includeSeymour,std::uint32_t mode,const Roster& roster,
                  const List& previous,List& output) noexcept {
    if(!ValidRoster(roster))return false;
    List next=previous; // Native code leaves unused list bytes untouched.
    next.current=next.total=next.frontline=next.selection=0;
    next.frontMask=next.currentMask=next.totalMask=0;
    if((mode&0xffff0000u)!=0x10000u){
        const auto append=[&](std::uint8_t id){
            if(id==255||(!includeSeymour&&id==7))return;
            next.rows[next.total++]=id;next.totalMask|=1u<<id;
        };
        for(auto id:roster.front)append(id);
        next.frontline=next.total;next.frontMask=next.totalMask;
        for(auto id:roster.reserve)append(id);
        next.current=next.total;next.currentMask=next.totalMask;
        for(unsigned id=0;id<Capacity;++id){
            if((roster.flags[id]&0x10u)&&!(next.totalMask&(1u<<id)))append(static_cast<std::uint8_t>(id));
        }
    }
    if(!ValidList(next))return false;
    output=next;return true;
}
} // namespace FfxHooks::SeymourMenuList
