#pragma once
#include "SeymourMenuListCore.h"

namespace FfxHooks::SeymourMenuList {
enum class Result:unsigned {Rejected,Unchanged,Applied,Restored,RestorePending};
struct Io {
    void* context=nullptr;
    bool (*current)(void*,bool cleanup)=nullptr;
    bool (*roster)(void*,Roster&)=nullptr;
    bool (*read)(void*,List&)=nullptr;
    // Compare the exact expected image again immediately before a bounded write.
    bool (*write)(void*,bool cleanup,const List& expected,const List& value)=nullptr;
};
inline bool SameSnapshot(const Io& io,const Roster& roster,const List& image,bool cleanup) noexcept {
    try {
        Roster observedRoster{};List observed{};
        return io.current(io.context,cleanup)&&io.roster(io.context,observedRoster)&&
            observedRoster==roster&&io.read(io.context,observed)&&observed==image&&
            io.current(io.context,cleanup);
    }catch(...){return false;}
}
inline bool OwnedMixture(const List& before,const List& planned,const List& observed) noexcept {
    const auto* a=reinterpret_cast<const unsigned char*>(&before);
    const auto* b=reinterpret_cast<const unsigned char*>(&planned);
    const auto* c=reinterpret_cast<const unsigned char*>(&observed);
    for(std::size_t i=0;i<sizeof(List);++i)if(c[i]!=a[i]&&c[i]!=b[i])return false;
    return true;
}
inline Result Restore(const Io& io,const Roster& roster,const List& before,const List& planned) noexcept {
    try {
        Roster observedRoster{};List observed{};
        if(!io.current(io.context,true)||!io.roster(io.context,observedRoster)||!(observedRoster==roster)||
           !io.read(io.context,observed)||!io.current(io.context,true))return Result::RestorePending;
        if(observed==before)return Result::Restored;
        // Never restore through a new save, a changed roster or foreign menu data.
        // A short write may contain a bytewise mixture of our two exact images.
        if(!OwnedMixture(before,planned,observed)||!io.current(io.context,true)||
           !io.write(io.context,true,observed,before)||!SameSnapshot(io,roster,before,true))
            return Result::RestorePending;
        return Result::Restored;
    }catch(...){return Result::RestorePending;}
}
// The adapter always executes the original constructor once FIRST. We extend
// only a result that exactly matches the supported seven-character semantics.
inline Result Extend(std::uint32_t mode,const Io& io) noexcept {
    if(!io.current||!io.roster||!io.read||!io.write)return Result::Rejected;
    Roster roster{};List before{},native{},planned{};bool attempted=false;
    try {
        if(!io.current(io.context,false)||!io.roster(io.context,roster)||!io.read(io.context,before)||
           !io.current(io.context,false)||!Build(false,mode,roster,before,native)||!(native==before)||
           !Build(true,mode,roster,before,planned))return Result::Rejected;
        if(planned==before)return Result::Unchanged;
        if(!SameSnapshot(io,roster,before,false))return Result::Rejected;
        attempted=true;
        if(io.write(io.context,false,before,planned)&&SameSnapshot(io,roster,planned,false))return Result::Applied;
    }catch(...){if(!attempted)return Result::Rejected;}
    return Restore(io,roster,before,planned);
}
} // namespace FfxHooks::SeymourMenuList
