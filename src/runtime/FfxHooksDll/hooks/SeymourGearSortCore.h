#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#define FFX_SEYMOUR_GEAR_PERMUTATION 1

// Eight-owner grouping informed by cxldalyy/playable-seymour-mod gear.cs, MIT.
// This core plans native swaps; it never copies inventory records into the game.
namespace FfxHooks::SeymourGearSort {
inline constexpr unsigned Capacity=200,Players=18,Types=2;
using Gear=std::array<std::uint8_t,22>;
using Counts=std::array<std::uint16_t,16>;
enum class Order:unsigned {Owner,OwnerAndType};
enum class Result:unsigned {Inactive,Unchanged,Applied,Rejected,Partial};
struct Snapshot {
    std::uint64_t generation=0;
    std::uint32_t thread=0,count=0;
    std::array<std::uint32_t,Capacity> rows{};
    std::array<Gear,Capacity> gear{};
    std::array<std::uint8_t,Players*Types> equipped{};
    friend bool operator==(const Snapshot& a,const Snapshot& b) noexcept {
        return a.generation==b.generation&&a.thread==b.thread&&a.count==b.count&&
               a.rows==b.rows&&a.gear==b.gear&&a.equipped==b.equipped;
    }
};
inline unsigned Slot(std::uint32_t row) noexcept {return row&0xfffu;}
inline std::uint16_t Handle(std::uint32_t row) noexcept {return static_cast<std::uint16_t>(row);}
inline bool Valid(const Snapshot& value) noexcept {
    if(!value.generation||!value.thread||value.count>Capacity)return false;
    std::array<bool,Capacity> seen{};
    for(unsigned i=0;i<value.count;++i){
        const auto row=value.rows[i];const unsigned slot=Slot(row);
        // Only saved-inventory handles. Shop/reward pointers may alias a fallback
        // native record and must not be admitted as mutable inventory slots.
        if((row&0xf000u)!=0x5000u||slot>=Capacity||seen[slot])return false;
        seen[slot]=true;const auto& g=value.gear[slot];
        if(g[2]!=1||g[4]>=Players||g[5]>=Types||(g[6]!=255&&g[6]>=Players))return false;
        if(g[6]!=255&&(g[6]!=g[4]||value.equipped[g[6]*Types+g[5]]!=slot))return false;
    }
    for(unsigned i=0;i<value.equipped.size();++i){
        const auto slot=value.equipped[i];if(slot==255)continue;
        if(slot>=Capacity)return false;
        const auto& g=value.gear[slot];
        if(g[2]!=1||g[4]!=i/Types||g[5]!=i%Types||g[6]!=i/Types)return false;
    }
    return true;
}
inline bool Count(const Snapshot& value,Counts& output) noexcept {
    if(!Valid(value))return false;
    Counts candidate{};
    for(unsigned i=0;i<value.count;++i){
        const auto& g=value.gear[Slot(value.rows[i])];
        if(g[4]<8)++candidate[g[4]*Types+g[5]];
    }
    output=candidate;return true;
}
// Validate the native within-group sort as a bounded permutation, not a new
// inventory. This proves bytes/references only; Workshop identity still relies
// on every mutation going through the existing native swap entry.
inline bool ExactGroupPermutation(const Snapshot& before,const Snapshot& after,
                                  unsigned start,unsigned count) noexcept {
    if(!Valid(before)||!Valid(after)||before.generation!=after.generation||
       before.thread!=after.thread||before.count!=after.count||before.rows!=after.rows||
       start>before.count||count>before.count-start)return false;

    std::array<bool,Capacity> selected{},matched{};
    for(unsigned i=0;i<count;++i)selected[Slot(before.rows[start+i])]=true;
    for(unsigned slot=0;slot<Capacity;++slot){
        if(!selected[slot]&&before.gear[slot]!=after.gear[slot])return false;
    }
    // Consume each match once. Byte-identical items must retain their exact
    // multiplicity; a set/hash alone would accept duplication or item loss.
    for(unsigned i=0;i<count;++i){
        const auto& old=before.gear[Slot(before.rows[start+i])];
        unsigned j=0;
        for(;j<count;++j){
            if(!matched[j]&&old==after.gear[Slot(after.rows[start+j])])break;
        }
        if(j==count)return false;
        matched[j]=true;
    }
    for(unsigned i=0;i<before.equipped.size();++i){
        const unsigned old=before.equipped[i],now=after.equipped[i];
        if(old==255||!selected[old]){
            if(now!=old)return false;
        }else if(now==255||!selected[now]||before.gear[old]!=after.gear[now])return false;
    }
    return true;
}
struct Plan {
    std::array<std::uint16_t,Capacity> sourceRows{};
    unsigned count=0;
};
inline unsigned Key(const Gear& gear,Order order) noexcept {
    if(gear[4]>=8)return 16; // Preserve unsupported owners as a stable tail.
    return order==Order::Owner?gear[4]*Types:gear[4]*Types+gear[5];
}
inline bool Build(const Snapshot& value,Order order,Plan& output) noexcept {
    if(!Valid(value)||(order!=Order::Owner&&order!=Order::OwnerAndType))return false;
    Plan plan{};plan.count=value.count;
    // Bounded stable insertion sort. No allocation and at most 19,900 shifts.
    for(unsigned i=0;i<value.count;++i){
        unsigned position=i;
        const auto key=Key(value.gear[Slot(value.rows[i])],order);
        while(position&&Key(value.gear[Slot(value.rows[plan.sourceRows[position-1]])],order)>key){
            plan.sourceRows[position]=plan.sourceRows[position-1];--position;
        }
        plan.sourceRows[position]=static_cast<std::uint16_t>(i);
    }
    output=plan;return true;
}
struct Io {
    void* context=nullptr;
    bool (*read)(void*,Snapshot&)=nullptr;
    bool (*current)(void*,const Snapshot&)=nullptr;
    // Calls the native swap entry, including its existing Workshop owner, ONCE.
    // Success must still be verified against full bytes and equipped indices.
    bool (*swap)(void*,std::uint16_t,std::uint16_t)=nullptr;
};
inline Result Apply(bool requested,Order order,const Io& io) {
    if(!requested)return Result::Inactive;
    if(!io.read||!io.current||!io.swap)return Result::Rejected;
    Snapshot expected{};Plan plan{};
    if(!io.read(io.context,expected)||!Build(expected,order,plan)||!io.current(io.context,expected))return Result::Rejected;
    std::array<std::uint16_t,Capacity> positions{};
    for(unsigned i=0;i<plan.count;++i)positions[i]=static_cast<std::uint16_t>(i);
    bool changed=false;
    for(unsigned i=0;i<plan.count;++i){
        if(positions[i]==plan.sourceRows[i])continue;
        unsigned j=i+1;while(j<plan.count&&positions[j]!=plan.sourceRows[i])++j;
        if(j==plan.count)return changed?Result::Partial:Result::Rejected;
        Snapshot observed{};
        if(!io.current(io.context,expected)||!io.read(io.context,observed)||!(observed==expected))
            return changed?Result::Partial:Result::Rejected;
        const unsigned a=Slot(expected.rows[i]),b=Slot(expected.rows[j]);
        if(!io.swap(io.context,Handle(expected.rows[i]),Handle(expected.rows[j]))){
            // Do not promise rollback after a native/external failure. A partial
            // ordering is safe only as an explicitly failed operation, not success.
            return changed||!io.read(io.context,observed)||!(observed==expected)?Result::Partial:Result::Rejected;
        }
        std::swap(expected.gear[a],expected.gear[b]);
        for(auto& slot:expected.equipped){
            if(slot==a)slot=static_cast<std::uint8_t>(b);
            else if(slot==b)slot=static_cast<std::uint8_t>(a);
        }
        changed=true;
        if(!io.current(io.context,expected)||!io.read(io.context,observed)||!(observed==expected))return Result::Partial;
        std::swap(positions[i],positions[j]);
    }
    Snapshot final{};
    if(!io.current(io.context,expected)||!io.read(io.context,final)||!(final==expected))return changed?Result::Partial:Result::Rejected;
    return changed?Result::Applied:Result::Unchanged;
}
} // namespace FfxHooks::SeymourGearSort
