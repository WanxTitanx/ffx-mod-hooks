#pragma once
#include "SeymourGearSortCore.h"

namespace FfxHooks::SeymourGearSort {
// The original counter buffer ends after fourteen WORDs. Seymour's two counts
// are kept in the local Counts value, never appended to adjacent game globals.
using NativeCounts=std::array<std::uint16_t,14>;
struct WithinIo {
    void* context=nullptr;
    bool (*read)(void*,Snapshot&)=nullptr;
    bool (*current)(void*,const Snapshot&)=nullptr;
    bool (*refreshNativeCounts)(void*)=nullptr;
    bool (*readNativeCounts)(void*,NativeCounts&)=nullptr;
    bool (*sortGroup)(void*,unsigned start,unsigned count)=nullptr;
};
inline bool WithinReady(const Snapshot& image,Counts& counts) noexcept {
    Plan plan{};
    if(!Count(image,counts)||!Build(image,Order::OwnerAndType,plan))return false;
    for(unsigned row=0;row<plan.count;++row){
        if(plan.sourceRows[row]!=row)return false;
        const auto& gear=image.gear[Slot(image.rows[row])];
        // The native helper reads slotCount WORDs starting at record+0x0e.
        // Only four abilities fit in a 22-byte native record; a fifth belongs
        // exclusively to Workshop's separate metadata, not this field.
        if(gear[4]<8&&gear[11]>4)return false;
    }
    return true;
}
inline bool NativeCountsMatch(const Counts& expected,const WithinIo& io) {
    NativeCounts actual{};
    return io.readNativeCounts(io.context,actual)&&
        std::equal(actual.begin(),actual.end(),expected.begin());
}
inline bool ReadExact(const Snapshot& expected,const WithinIo& io) {
    Snapshot observed{};
    return io.current(io.context,expected)&&io.read(io.context,observed)&&
        observed==expected&&io.current(io.context,expected);
}
// Runs the same native group helper for each of the sixteen owner/type groups.
// The adapter supplies the real entry: every swap continues through Workshop.
// A byte multiset is not proof of external item identity, so no private record
// copying or inferred metadata permutation is performed here.
inline Result SortWithin(bool requested,const WithinIo& io) {
    if(!requested)return Result::Inactive;
    if(!io.read||!io.current||!io.refreshNativeCounts||!io.readNativeCounts||!io.sortGroup)
        return Result::Rejected;
    Snapshot expected{};Counts counts{};
    if(!io.read(io.context,expected)||!WithinReady(expected,counts)||
       !ReadExact(expected,io))return Result::Rejected;
    // Once a native writer starts, every uncertainty is Partial, never a clean
    // rejection. Exceptions propagate to the adapter's finally/stop boundary.
    if(!io.refreshNativeCounts(io.context)||!ReadExact(expected,io)||
       !NativeCountsMatch(counts,io)||!io.current(io.context,expected))return Result::Partial;
    const auto initial=expected;
    unsigned start=0;
    for(const unsigned count:counts){
        if(start>expected.count||count>expected.count-start)return Result::Partial;
        if(count>1){
            if(!ReadExact(expected,io)||!NativeCountsMatch(counts,io)||
               !io.current(io.context,expected))return Result::Partial;
            if(!io.sortGroup(io.context,start,count))return Result::Partial;
            Snapshot after{};
            if(!io.current(io.context,expected)||!io.read(io.context,after)||
               !ExactGroupPermutation(expected,after,start,count)||
               !io.current(io.context,after)||!NativeCountsMatch(counts,io)||
               !io.current(io.context,after))return Result::Partial;
            expected=after;
        }
        start+=count;
    }
    if(!ReadExact(expected,io)||!NativeCountsMatch(counts,io)||
       !io.current(io.context,expected))return Result::Partial;
    return initial==expected?Result::Unchanged:Result::Applied;
}
} // namespace FfxHooks::SeymourGearSort
