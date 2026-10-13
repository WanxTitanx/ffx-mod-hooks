#include "ArcanaWorldCore.h"
#include "ArcanaAcquisitionCatalog.generated.h"
#include <cmath>

namespace FfxHooks::Arcana::Acquisition {
namespace {
bool Aeon(const Facts& f,unsigned actor) noexcept {return (f.aeons&(1u<<actor))!=0;}
bool Magus(const Facts& f) noexcept {return Aeon(f,15)&&Aeon(f,16)&&Aeon(f,17);}
bool Stage(unsigned stage,const Facts& f) noexcept {
    switch(stage){
    case 0:return true;
    case 1:return Aeon(f,8);
    case 2:return Aeon(f,9);
    case 3:return f.story>=514;
    case 4:return f.story>=514&&Aeon(f,9);
    case 5:return Aeon(f,10);
    case 6:return f.story>=1085;
    case 7:return f.story>=1190;
    case 8:return f.story>=1455;
    case 9:return f.story>=1600&&Aeon(f,11);
    case 10:return f.story>=1950;
    case 11:return f.story>=2300&&Aeon(f,12);
    case 12:return f.story>2300;
    case 13:case 14:return f.story>=2651;
    case 15:return f.freeTravel;
    case 16:return f.freeTravel&&f.mirror;
    case 17:return f.freeTravel&&Aeon(f,14);
    case 18:return f.freeTravel&&Aeon(f,13);
    case 19:return f.freeTravel&&Magus(f);
    case 20:return f.freeTravel&&f.omega;
    case 21:return f.freeTravel&&Magus(f)&&f.omega;
    case 22:return f.freeTravel&&(f.darkAeons&(1u<<6));
    case 23:return f.freeTravel&&f.darkAeons==255;
    case 24:return true;
    default:return false;
    }
}
int Reading(unsigned card) noexcept {
    switch(card){
    case 5:return TempleRecords;case 14:return SupplyBundles;case 74:return HomePages;case 77:return MedicineCards;
    case 64:return VillageLedger;case 65:return AlBhedSymbols;case 66:return DjoseKits;case 72:return TheaterInscriptions;
    default:return -1;
    }
}
unsigned ReadingCount(unsigned card) noexcept {return card==77?4u:3u;}
bool Base(const State& s,const Facts& f,unsigned card) noexcept {
    if(card>=kStandardCardCount||s.acquired[card]||!Stage(Data::sites[card].stage,f))return false;
    if(card==33&&!f.wobbly)return false;
    if(card==21)for(unsigned i=0;i<kStandardCardCount;++i)if(i!=21&&!s.acquired[i])return false;
    return true;
}
bool Objectives(const Progress& p,unsigned card) noexcept {
    const auto reading=Reading(card);if(reading>=0&&p[reading]<ReadingCount(card))return false;
    if(card==18&&p[Pyreflies]!=7)return false;
    if(card==35&&p[IfritEmblems]!=7)return false;
    if((card==7||card==61)&&p[ChocoboPosts]!=31)return false;
    if(card==49&&p[CoastVisits]!=3)return false;
    return true;
}
bool Same(const Target& a,const Target& b) noexcept {
    return a.kind==b.kind&&a.card==b.card&&a.part==b.part&&a.location.room==b.location.room&&a.location.x==b.location.x&&a.location.z==b.location.z;
}
const char* ReadingText(unsigned card,unsigned step) noexcept {
    static constexpr const char* temple[]={
        "Temple record 1/3: The fayth preserve a bond between memory and the living. Each temple marks a different part of Yuna's pilgrimage.",
        "Temple record 2/3: A summoner's strength grows through the people who travel beside her. Return to this symbol for the final record.",
        "Temple record 3/3: Baaj's hidden fayth completes this archive."};
    static constexpr const char* supplies[]={
        "Relief bundle 1/3 prepared: clean water for travelers. Return to the Tarot symbol to prepare the next bundle.",
        "Relief bundle 2/3 prepared: dressings and provisions for the crossing. One bundle remains.",
        "Relief bundle 3/3 prepared: a complete set for the next journey."};
    static constexpr const char* home[]={
        "Home archive 1/3: Match the evacuation record with its destination, the airship. Return to the Tarot symbol for the next page.",
        "Home archive 2/3: Match the survivors' account with its origin, Bikanel. The remaining page records those who helped them escape.",
        "Home archive 3/3: The survivors' pages are together again."};
    static constexpr const char* medicine[]={
        "Medicine card 1/4: Healing restores resources; a cure removes a condition. Read the item's description before choosing it.",
        "Medicine card 2/4: Revival brings back a fallen ally. Recovery after revival is a separate effect.",
        "Medicine card 3/4: Protection prevents a condition or reduces a threat. It does not replace a cure for every existing condition.",
        "Medicine card 4/4: The four medicine records are complete."};
    static constexpr const char* ledger[]={
        "Supply ledger 1/3: Water belongs with the provisions. Its entry is balanced. The remaining entries are dressings and tools.",
        "Supply ledger 2/3: Dressings belong with medical supplies. That entry is balanced; the tools remain.",
        "Supply ledger 3/3: Tools belong with repairs. All three entries balance; return to the symbol to collect the shop's Tarot card."};
    static constexpr const char* symbols[]={
        "Symbol exercise 1/3. Legend: circle = home, wave = journey, star = hope. Match the circle with home, then continue at the symbol.",
        "Symbol exercise 2/3. The legend pairs the wave with the journey. The remaining star represents hope.",
        "Symbol exercise 3/3. Circle, wave, star: home, journey, hope. The fixed exercise is complete; claim the Tarot card at this symbol."};
    static constexpr const char* kits[]={
        "Djose kit 1/3: Provisions for the road are ready. Return to the symbol to separate the remaining aid kits.",
        "Djose kit 2/3: The medical supplies are ready. One kit of repair tools remains.",
        "Djose kit 3/3: The repair tools are ready. Claim the acolytes' Tarot card at the symbol."};
    static constexpr const char* inscriptions[]={
        "Theater inscription 1/3: Music preserves the moments that words cannot. Two inscriptions remain.",
        "Theater inscription 2/3: An image remembers a place, while its travelers give that place meaning.",
        "Theater inscription 3/3: Art carries memory into the future. Return to the symbol to claim the artist's Tarot card."};
    if(card==5)return temple[step-1];
    if(card==14)return supplies[step-1];
    if(card==74)return home[step-1];
    if(card==64)return ledger[step-1];
    if(card==65)return symbols[step-1];
    if(card==66)return kits[step-1];
    if(card==72)return inscriptions[step-1];
    return medicine[step-1];
}
}
const std::array<Site,kStandardCardCount>& Catalog() noexcept {return Data::sites;}
bool Eligible(const State& s,const Progress& p,const Facts& f,unsigned card) noexcept {
    return ValidProgress(p)&&Base(s,f,card)&&Objectives(p,card);
}
void ObserveTravel(Progress& p,const Facts& f,std::uint16_t room) noexcept {
    if(!ValidProgress(p)||!f.freeTravel||!f.mirror)return;
    if(room==19)p[CoastVisits]|=1;
    if(room==16||room==18)p[CoastVisits]|=2;
}
Targets Discover(const State& s,const Progress& p,const Facts& f,std::uint16_t room) noexcept {
    Targets out;if(Validate(s)!=Error::None||!ValidProgress(p))return out;
    const auto add=[&](TargetKind kind,unsigned card,unsigned part,const Location& location){
        if(location.room==room&&out.count<out.values.size())out.values[out.count++]={kind,static_cast<std::uint8_t>(card),static_cast<std::uint8_t>(part),location};
    };
    for(const auto& site:Data::sites){
        if(!Base(s,f,site.card))continue;
        if(site.card==18&&p[Pyreflies]!=7){
            for(unsigned i=0;i<3;++i)if(!(p[Pyreflies]&(1u<<i)))add(TargetKind::Pyrefly,18,i,Data::pyreflies[i]);
        }else if(site.card==35&&p[IfritEmblems]!=7){
            for(unsigned i=0;i<3;++i)if(!(p[IfritEmblems]&(1u<<i)))add(TargetKind::IfritEmblem,35,i,Data::ifritEmblems[i]);
        }else if((site.card==7||site.card==61)&&p[ChocoboPosts]!=31){
            const unsigned routeCard=s.acquired[7]?61u:7u;
            if(site.card!=routeCard)continue;
            const auto& trainer=Data::sites[7].location;
            if(!(p[ChocoboPosts]&8))add(TargetKind::RouteStart,routeCard,0,trainer);
            else if(p[ChocoboPosts]==15)add(TargetKind::RouteFinish,routeCard,0,trainer);
            else for(unsigned i=0;i<3;++i)if(!(p[ChocoboPosts]&(1u<<i)))add(TargetKind::RoutePost,routeCard,i,Data::chocoboPosts[i]);
        }else if(Reading(site.card)>=0||Objectives(p,site.card))add(TargetKind::Card,site.card,0,site.location);
    }
    if(f.freeTravel&&Data::recovery.room==room){
        for(unsigned card=0;card<kStandardCardCount;++card){
            if(!Base(s,f,card))continue;
            // Regional closures may block a new local objective as well as its
            // reward. The deck carries equivalent records; native optional
            // victories remain mandatory, and mounted route posts stay physical.
            if((card==7||card==61)&&p[ChocoboPosts]!=31){
                if(card==61&&!s.acquired[7])continue;
                if(!(p[ChocoboPosts]&8))add(TargetKind::RouteStart,card,0,Data::recovery);
                else if(p[ChocoboPosts]==15)add(TargetKind::RouteFinish,card,0,Data::recovery);
                else continue;
            }else if((card==18&&p[Pyreflies]!=7)||(card==35&&p[IfritEmblems]!=7)){
                const auto marks=p[card==18?Pyreflies:IfritEmblems];unsigned part=0;
                while(marks&(1u<<part))++part;
                add(TargetKind::RecoveryMark,card,part,Data::recovery);
            }else if(card==49&&p[CoastVisits]!=3){
                add(TargetKind::CoastalRecord,49,(p[CoastVisits]&1)?1:0,Data::recovery);
            }else if(Reading(card)>=0||Objectives(p,card))add(TargetKind::Recovery,card,0,Data::recovery);
            else continue;
            break;
        }
    }
    return out;
}
Result Interact(State& s,Progress& p,const Facts& f,const Target& target,std::uint16_t room,float x,float z,bool riding) noexcept {
    if(!std::isfinite(x)||!std::isfinite(z)||target.location.room!=room)return {};
    const float dx=x-target.location.x,dz=z-target.location.z;
    if(dx*dx+dz*dz>kInteractRadius*kInteractRadius)return {};
    const auto options=Discover(s,p,f,room);bool found=false;
    for(unsigned i=0;i<options.count;++i)if(Same(target,options.values[i]))found=true;
    if(!found)return {};
    auto next=p;
    if(target.kind==TargetKind::RouteStart){
        next[ChocoboPosts]=8;p=next;
        return {ResultKind::Progressed,target.card,"Chocobo reconnaissance started. Ride a chocobo to the three Tarot posts across the Calm Lands, then return to the trainer. There is no timer."};
    }
    if(target.kind==TargetKind::RouteFinish){
        next[ChocoboPosts]=31;p=next;
        return {ResultKind::Progressed,target.card,"Reconnaissance complete. Your return has been recorded. Claim any missing reconnaissance Tarot cards at the trainer or the airship deck."};
    }
    if(target.kind==TargetKind::CoastalRecord){
        next[CoastVisits]|=static_cast<std::uint8_t>(1u<<target.part);p=next;
        return {ResultKind::Progressed,49,target.part==0?
            "Besaid coastal record inspected. The airship's survey preserves the same observation when landing access is blocked. Inspect the Kilika record next.":
            "Kilika coastal record inspected. Both coastal observations are complete. Claim the King of Cups at the deck symbol."};
    }
    if(target.kind==TargetKind::RecoveryMark){
        const auto which=target.card==18?Pyreflies:IfritEmblems;
        next[which]|=static_cast<std::uint8_t>(1u<<target.part);p=next;
        return {ResultKind::Progressed,target.card,(next[which]&7)==7?
            "All three regional records are inspected. Claim the recovered Tarot card at the deck symbol.":
            "Regional mark inspected in the airship archive. This is the safe counterpart of the local symbol; inspect the remaining records here."};
    }
    if(target.kind==TargetKind::RoutePost&&!riding)return {ResultKind::None,target.card,"Ride a chocobo to register this reconnaissance post."};
    if(target.kind==TargetKind::RoutePost||target.kind==TargetKind::Pyrefly||target.kind==TargetKind::IfritEmblem){
        const unsigned which=target.kind==TargetKind::RoutePost?ChocoboPosts:target.kind==TargetKind::Pyrefly?Pyreflies:IfritEmblems;
        next[which]|=static_cast<std::uint8_t>(1u<<target.part);p=next;
        const bool complete=(next[which]&7)==7;
        return {ResultKind::Progressed,target.card,complete?
            "All three Tarot marks are recorded. Return to the collection symbol to claim your card.":
            "Tarot mark recorded. Find the remaining symbols in this area to complete the set."};
    }
    const auto reading=Reading(target.card);
    if((target.kind==TargetKind::Card||target.kind==TargetKind::Recovery)&&reading>=0&&next[reading]<ReadingCount(target.card)){
        ++next[reading];p=next;
        return {ResultKind::Progressed,target.card,ReadingText(target.card,next[reading])};
    }
    if(!Eligible(s,next,f,target.card))return {};
    auto award=s;if(Award(award,target.card)!=Error::None)return {};
    s=award;p=next;return {ResultKind::Awarded,target.card,nullptr};
}
}
