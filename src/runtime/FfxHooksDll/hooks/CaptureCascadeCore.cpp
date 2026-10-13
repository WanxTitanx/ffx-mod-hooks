#include "CaptureCascadeCore.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace FfxHooks::CaptureCascade {
namespace {
unsigned Index(Aeon family) noexcept {return static_cast<unsigned>(family);}
bool StampValid(const Stamp& stamp,bool present) noexcept {
    if(!present){if(stamp.utc||stamp.playSeconds)return false;for(char c:stamp.build)if(c)return false;return true;}
    if(!stamp.utc||!stamp.build[0]||stamp.build.back())return false;
    bool ended=false;
    for(unsigned char c:stamp.build){if(!c)ended=true;else if(ended||c<32||c>126)return false;}
    return true;
}
bool PagesValid(const Pages& pages,unsigned count,std::uint64_t sequence) noexcept {
    return pages.queued<=sequence&&pages.page<count&&
        (pages.queued?(!pages.complete||pages.page+1u==count):(!pages.page&&!pages.complete));
}
bool Mutable(const State& state) noexcept {
    return Valid(state)&&state.revision<UINT64_MAX&&state.sequence<UINT64_MAX;
}
void Queue(State& state,Pages& pages) noexcept {if(!pages.queued)pages.queued=++state.sequence;}
bool Same(const View& a,const View& b) noexcept {
    return a.kind==b.kind&&a.page==b.page&&a.family==b.family&&a.revision==b.revision;
}
std::uint32_t Draw(std::uint32_t& state) noexcept {
    state+=0x9E3779B9u;auto value=state;
    value=(value^(value>>16))*0x7FEB352Du;value=(value^(value>>15))*0x846CA68Bu;
    return value^(value>>16);
}
}
unsigned PageCount(Tutorial kind) noexcept {
    switch(kind){case Tutorial::Sin:return 3;case Tutorial::Cascade:return 4;case Tutorial::Region:return 1;default:return 0;}
}
bool Valid(const State& state) noexcept {
    if((state.defeated&~state.nativeSeen)||(state.regionPending&~state.defeated)||
       (state.regionSeen&~state.defeated)||(state.regionPending&state.regionSeen))return false;
    if(state.defeated){if(state.first>=kFamilies||!(state.defeated&(1u<<state.first))||!state.cascade.queued)return false;}
    else if(state.first!=kNoFamily||state.cascade.queued)return false;
    if(!PagesValid(state.sin,3,state.sequence)||!PagesValid(state.cascade,4,state.sequence)||
       (state.sin.queued&&state.sin.queued==state.cascade.queued))return false;
    for(unsigned i=0;i<kFamilies;++i)
        if(!StampValid(state.defeats[i],(state.defeated&(1u<<i))!=0)||
           !StampValid(state.captures[i],(state.captured&(1u<<i))!=0))return false;
    return true;
}
State Adopt(std::uint8_t flags) noexcept {State state;state.nativeSeen=flags;return state;}
bool ObserveVictories(State& state,std::uint8_t flags,const Stamp& stamp) noexcept {
    if(!Mutable(state)||(state.nativeSeen&~flags))return false;
    const auto fresh=static_cast<std::uint8_t>(flags&~state.nativeSeen);
    if(!fresh)return true;
    if(!StampValid(stamp,true))return false;
    auto next=state;
    for(unsigned family=0;family<kFamilies;++family)if(fresh&(1u<<family)){
        next.defeats[family]=stamp;
        if(next.first==kNoFamily)next.first=static_cast<std::uint8_t>(family);
    }
    next.nativeSeen=flags;next.defeated|=fresh;Queue(next,next.cascade);++next.revision;
    if(!Valid(next))return false;
    state=next;return true;
}
bool ObserveCapture(State& state,Aeon family,const Stamp& stamp) noexcept {
    const auto i=Index(family);if(!Mutable(state)||i>=kFamilies)return false;
    if(state.captured&(1u<<i))return true;
    if(!StampValid(stamp,true))return false;
    state.captured|=static_cast<std::uint8_t>(1u<<i);state.captures[i]=stamp;++state.revision;return true;
}
bool ObserveRegion(State& state,Aeon region,bool macalania) noexcept {
    if(!Mutable(state))return false;
    bool changed=false;
    if(macalania&&!state.sin.queued){Queue(state,state.sin);changed=true;}
    const auto i=Index(region);
    if(i<kFamilies&&(state.defeated&(1u<<i))&&!((state.regionSeen|state.regionPending)&(1u<<i))){
        state.regionPending|=static_cast<std::uint8_t>(1u<<i);changed=true;
    }
    if(changed)++state.revision;
    return true;
}
View Current(const State& state,Aeon region) noexcept {
    if(!Valid(state))return {};
    const bool sin=state.sin.queued&&!state.sin.complete,cascade=state.cascade.queued&&!state.cascade.complete;
    if(sin&&(!cascade||state.sin.queued<state.cascade.queued))return {Tutorial::Sin,state.sin.page,kNoFamily,state.revision};
    if(cascade)return {Tutorial::Cascade,state.cascade.page,state.first,state.revision};
    const auto family=Index(region);
    if(family<kFamilies&&(state.regionPending&(1u<<family)))return {Tutorial::Region,0,static_cast<std::uint8_t>(family),state.revision};
    return {};
}
bool Acknowledge(State& state,const View& view,bool previous,bool skip) noexcept {
    if(!Mutable(state)||view.kind==Tutorial::None||!Same(view,Current(state,static_cast<Aeon>(view.family))))return false;
    if(view.kind==Tutorial::Region){
        if(previous)return true;
        state.regionPending&=static_cast<std::uint8_t>(~(1u<<view.family));
        state.regionSeen|=static_cast<std::uint8_t>(1u<<view.family);
    }else{
        auto& pages=view.kind==Tutorial::Sin?state.sin:state.cascade;
        if(skip){pages.page=static_cast<std::uint8_t>(PageCount(view.kind)-1);pages.complete=true;}
        else if(previous){if(!pages.page)return true;--pages.page;}
        else if(pages.page+1u<PageCount(view.kind))++pages.page;
        else pages.complete=true;
    }
    ++state.revision;return true;
}
unsigned FamilyForMonster(unsigned model) noexcept {
    if(model>=334&&model<=338)return model-334;
    if(model==339)return Index(Aeon::Anima);
    if(model==340)return Index(Aeon::Yojimbo);
    if(model>=341&&model<=343)return Index(Aeon::Sisters);
    return kNoFamily;
}
Selection Select(const Config& config,const State& state,Aeon region,const Candidate* pool,std::size_t count,
                 const Slot* slots,std::size_t actors,std::uint32_t entropy) noexcept {
    Selection result;
    if(!slots||!actors||actors>kMaximumActors)return result;
    result.count=static_cast<unsigned>(actors);
    for(unsigned i=0;i<actors;++i)result.monsters[i]=slots[i].monster;
    if(!config.enabled||!Valid(state)||Index(region)>=kFamilies||!(state.defeated&(1u<<Index(region)))||
       !pool||!count||count>kFamilies||config.chance>10000||config.secondChance>10000)return result;
    unsigned families=0;std::array<unsigned,kMaximumActors> free{};unsigned available=0;
    for(unsigned i=0;i<actors;++i)if(slots[i].replaceable)free[available++]=i;
    for(unsigned i=0;i<count;++i){
        const auto& candidate=pool[i];const auto family=Index(candidate.family);
        if(family>=kFamilies||(families&(1u<<family))||candidate.count<1||candidate.count>3)return result;
        families|=1u<<family;
        for(unsigned part=0;part<3;++part){
            const auto model=candidate.monsters[part];
            if(part<candidate.count){if(model<=0x1000||model>=0x1400)return result;}
            else if(model)return result;
            if(model)for(unsigned j=0;j<=i;++j)for(unsigned k=0;k<pool[j].count;++k)
                if((j<i||k<part)&&pool[j].monsters[k]==model)return result;
        }
    }
    if(!available||Draw(entropy)%10000u>=config.chance)return result;
    const auto wanted=1u+(Draw(entropy)%10000u<config.secondChance?1u:0u);
    unsigned chosen=0;
    for(unsigned group=0;group<wanted;++group){
        std::array<unsigned,kFamilies> eligible{};unsigned candidates=0;
        for(unsigned i=0;i<count;++i)if(!(chosen&(1u<<i))&&(state.nativeSeen&(1u<<Index(pool[i].family)))&&pool[i].count<=available)
            eligible[candidates++]=i;
        if(!candidates)break;
        const auto index=eligible[Draw(entropy)%candidates];chosen|=1u<<index;
        const auto& candidate=pool[index];
        for(unsigned part=0;part<candidate.count;++part){
            const auto freeIndex=Draw(entropy)%available,slot=free[freeIndex];
            free[freeIndex]=free[--available];result.monsters[slot]=candidate.monsters[part];
            result.changed|=static_cast<std::uint8_t>(1u<<slot);
        }
        ++result.groups;
    }
    result.selected=result.groups!=0;return result;
}
const char* Name(Aeon family) noexcept {
    static constexpr const char* names[]={"Dark Valefor","Dark Ifrit","Dark Ixion","Dark Shiva","Dark Bahamut","Dark Yojimbo","Dark Anima","Dark Magus Sisters"};
    return Index(family)<kFamilies?names[Index(family)]:"Unknown Dark Aeon";
}
}
