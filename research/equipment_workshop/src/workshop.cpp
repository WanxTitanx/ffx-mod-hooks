#include "workshop.h"
#include "customize_recipes.h"
#include "customize_constraints.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace workshop {
namespace {
std::uint16_t Read16(const std::uint8_t* p){return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));}
void Write16(std::uint8_t* p,std::uint16_t v){p[0]=static_cast<std::uint8_t>(v);p[1]=static_cast<std::uint8_t>(v>>8);}
bool ValidAbility(std::uint16_t id){return id==Empty || (id>=0x8000 && id<=0x8FFF);}
unsigned Capacity(const Piece& p){return p.native[11]+p.fifthUnlocked;}
bool Special(const Piece& p){return !IsAeon(p)&&((p.native[3]&0x0C)!=0 || p.native[4]>6 || p.native[5]>1);}
bool Equipped(const Piece& p){return p.native[6]!=0xFF;}
void SetAbility(Piece& p,unsigned slot,std::uint16_t id){if(slot==4)p.fifth=id;else Write16(p.native+14+slot*2,id);}
std::uint64_t Id(State& s){return s.nextId++;}
void Initialize(State& s,Piece& p,const std::uint8_t* native){
    p=Piece{};std::memcpy(p.native,native,22);p.fifth=Empty;
    if(!p.native[2])return;
    p.id=Id(s);
    for(unsigned i=0;i<4;++i)if(Ability(p,i)!=Empty)p.abilities[i]=Id(s);
}
// SplitMix64 state and successful-roll counter persist in the sidecar. Rejection
// sampling removes modulo bias. Preview works on a copy, so cancel spends no RNG.
std::uint64_t Next(State& s){
    auto z=(s.rng+=0x9E3779B97F4A7C15ULL);z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;
    z=(z^(z>>27))*0x94D049BB133111EBULL;return z^(z>>31);
}
unsigned Uniform(State& s,unsigned count){
    const auto threshold=(std::uint64_t(0)-count)%count;std::uint64_t n;
    do{n=Next(s);}while(n<threshold);return static_cast<unsigned>(n%count);
}
bool Charge(Plan& p,unsigned item,unsigned amount){
    if(item>=ItemCount || amount>65535u-p.costs[item])return false;
    p.costs[item]=static_cast<std::uint16_t>(p.costs[item]+amount);return true;
}
bool RankCost(Plan& p,std::uint16_t word,unsigned rank){
    unsigned item=0,quantity=0;
    return RefinementCost(word,rank,p.policy,item,quantity,&p.catalog)&&Charge(p,item,quantity);
}
void IndividualRanks(Piece& p){
    // Old A sidecars expand losslessly. Changing global policy must never fill
    // missing B ranks or discard the extra ranks on an uneven B equipment.
    if(p.mode==1)for(unsigned i=0;i<5;++i)p.ranks[i]=Ability(p,i)==Empty?0:p.rank;
    p.rank=0;p.mode=2;
}
bool Evolves(std::uint16_t old,std::uint16_t replacement){
    if(old<0x8000 || replacement<0x8000)return false;
    const unsigned a=old-0x8000,b=replacement-0x8000;
    if(a>=98 && a<121 && ((a-98)%4)<3)return b==a+1;
    return (a>=47 && a<=75 && (a-47)%4==0 && b+1==a);
}
bool Affordable(const State& state,const Plan& plan,bool unlocking=false,unsigned owner=7){
    if(plan.policy.devFreeMaterials && unlocking){
        if(owner>=8&&owner<18){
            for(unsigned item=75;item<=79;++item)if(!state.items[item]&&!state.items[80])return false;
            return true;
        }
        const unsigned item=FifthSphere(owner);
        return item<ItemCount && (state.items[item]!=0 || state.items[80]!=0);
    }
    for(unsigned i=0;i<ItemCount;++i){
        const unsigned required=plan.requirements[i];
        if(required && (plan.policy.devFreeMaterials?state.items[i]==0:required>state.items[i]))return false;
    }
    return true;
}
Error Edit(const State& before,const Request& r,Plan& out){
    auto& s=out.after;auto& p=s.pieces[r.slot];
    if(r.op==Op::Create){
        if(r.gearTemplate[4]>=8&&r.gearTemplate[4]<18)return Error::Protected;
        if(p.id || !r.gearTemplate[2] || r.gearTemplate[6]!=0xFF || r.gearTemplate[11]>4)return Error::InvalidRequest;
        Initialize(s,p,r.gearTemplate);return Error::Ok;
    }
    if(!p.id)return Error::EmptyPiece;
    // Event operations model host-observed inventory changes. Workshop mutators
    // require unequipped gear, before any material or metadata changes.
    if(r.op!=Op::Swap && Equipped(p)&&!IsAeon(p))return Error::Equipped;
    switch(r.op){
    case Op::Swap:{
        if(r.other>=GearCount || r.other==r.slot)return Error::InvalidRequest;
        if(s.pieces[r.other].id!=r.otherId)return Error::Stale;
        std::swap(p,s.pieces[r.other]);return Error::Ok;
    }
    case Op::Retire:
        if(Special(p)||IsAeon(p))return Error::Protected;
        {std::array<std::uint8_t,22> native{};std::copy(p.native,p.native+22,native.begin());native[2]=0;Initialize(s,p,native.data());}
        return Error::Ok;
    case Op::Reforge:{
        if(IsAeon(p))return Error::Protected;
        if(p.fifthUnlocked && p.native[4]!=r.gearTemplate[4])return Error::Protected;
        if(p.fifthUnlocked && p.fifth!=Empty && p.native[5]!=r.gearTemplate[5])return Error::Protected;
        if(Special(p) || r.gearTemplate[4]>6 || r.gearTemplate[5]>1 || (r.gearTemplate[3]&0x0C))return Error::Protected;
        if(!r.gearTemplate[2] || r.gearTemplate[6]!=0xFF)return Error::InvalidRequest;
        if(p.native[4]==r.gearTemplate[4] && p.native[5]==r.gearTemplate[5] && Read16(p.native+12)==Read16(r.gearTemplate+12))return Error::NoChange;
        // Native presentation/weapon mechanics come from a verified host catalog.
        for(auto at:{0u,1u,4u,5u,8u,9u,10u,12u,13u})p.native[at]=r.gearTemplate[at];
        return Charge(out,80,1)?Error::Ok:Error::InvalidRequest;
    }
    case Op::Fuse:{
        if(r.other>=GearCount || r.other==r.slot || r.count<1 || r.count>2)return Error::InvalidRequest;
        auto& donor=s.pieces[r.other];
        if(!donor.id || donor.id!=r.otherId)return Error::Stale;
        if(Special(donor)||IsAeon(donor))return Error::Protected;
        if(Equipped(donor))return Error::Equipped;
        if(r.count==2 && (r.from[0]==r.from[1] || r.to[0]==r.to[1]))return Error::Duplicate;
        Piece combination=p;
        for(unsigned i=0;i<r.count;++i){
            if(r.from[i]>=donor.native[11]||r.to[i]>=p.native[11])return Error::InvalidRequest;
            if(ProtectedAbility(p,r.to[i]))return Error::Protected;
            SetAbility(combination,r.to[i],Empty);
        }
        for(unsigned i=0;i<r.count;++i){
            const auto ability=Ability(donor,r.from[i]);
            const auto allowed=CustomizeEligibility(combination,r.to[i],ability,&out.catalog);if(allowed!=Error::Ok)return allowed;
            SetAbility(combination,r.to[i],ability);
        }
        IndividualRanks(p);IndividualRanks(donor);
        for(unsigned i=0;i<r.count;++i){const unsigned src=r.from[i],dst=r.to[i];
            if(src>=donor.native[11] || dst>=p.native[11] || Ability(donor,src)==Empty)return Error::InvalidRequest;
            const auto ability=Ability(donor,src);
            unsigned item=0,quantity=0;bool native=false;
            if(!CustomizeCost(ability,out.policy,item,quantity,native,&out.catalog))return Error::UnsupportedAbility;
            if(!Charge(out,item,(quantity+out.policy.fusionDivisor-1)/out.policy.fusionDivisor))return Error::InvalidRequest;
            SetAbility(p,dst,ability);p.abilities[dst]=donor.abilities[src];
            p.ranks[dst]=donor.ranks[src];
        }
        out.gilCost=out.policy.fusionGilPerAbility*r.count;
        {std::array<std::uint8_t,22> native{};std::copy(donor.native,donor.native+22,native.begin());native[2]=0;Initialize(s,donor,native.data());}
        return Error::Ok;
    }
    case Op::Expand:{
        if(Special(p))return Error::Protected;
        if(r.value<=p.native[11] || r.value>4 || r.policy>1)return Error::InvalidRequest;
        for(unsigned c=p.native[11]+1;c<=r.value;++c){
            // Never revive hidden native words as free abilities.
            if(Ability(p,c-1)!=Empty)return Error::InvalidState;
            if(!Charge(out,80+c,r.policy?c:1))return Error::InvalidRequest;
        }
        p.native[11]=static_cast<std::uint8_t>(r.value);return Error::Ok;
    }
    case Op::Clear:{
        if(Special(p))return Error::Protected;
        if(r.value>=Capacity(p) || Ability(p,r.value)==Empty)return Error::InvalidRequest;
        if(ProtectedAbility(p,r.value))return Error::Protected;
        SetAbility(p,r.value,Empty);p.abilities[r.value]=0;p.ranks[r.value]=0;
        return Charge(out,95,1)?Error::Ok:Error::InvalidRequest;
    }
    case Op::Evolve:{
        if(Special(p))return Error::Protected;
        const unsigned slot=r.to[0];if(slot>=4 || slot>=Capacity(p) || !Evolves(Ability(p,slot),r.value))return Error::InvalidRequest;
        if(ProtectedAbility(p,slot))return Error::Protected;
        const auto allowed=CustomizeEligibility(p,slot,r.value,&out.catalog);if(allowed!=Error::Ok)return allowed;
        unsigned item=0,quantity=0;bool native=false;
        if(!CustomizeCost(r.value,out.policy,item,quantity,native,&out.catalog)||!native)return Error::UnsupportedAbility;
        // Preserve other legacy A ranks without charging catch-up refinement.
        IndividualRanks(p);
        SetAbility(p,slot,r.value);p.abilities[slot]=Id(s);p.ranks[slot]=0;
        out.gilCost=25000;
        return Charge(out,item,(quantity+1)/2)?Error::Ok:Error::InvalidRequest;
    }
    case Op::Mode:
        if(Special(p))return Error::Protected;
        if(r.value<1 || r.value>2)return Error::InvalidRequest;
        if(p.mode==r.value)return Error::NoChange;
        if(p.mode!=0)return Error::Protected; // No mode-conversion rank exploit.
        p.mode=static_cast<std::uint8_t>(r.value);return Error::Ok;
    case Op::Refine:{
        if(Special(p))return Error::Protected;
        IndividualRanks(p);
        std::array<unsigned,5> eligible{};unsigned count=0,occupied=0;
        for(unsigned i=0;i<Capacity(p);++i)if(Ability(p,i)!=Empty){
            if(ProtectedAbility(p,i))continue;
            ++occupied;if(!SupportedRefinement(Ability(p,i),&out.catalog))return Error::UnsupportedAbility;
            if(p.ranks[i]<10)eligible[count++]=i;
        }
        if(!occupied)return Error::InvalidRequest;
        if(!count)return Error::Maximum;
        unsigned total=0;for(unsigned i=0;i<5;++i)total+=AbilityRank(p,i);
        if(out.policy.mode==1){
            if(!Charge(out,out.policy.baseItem,out.policy.baseAmount*count))return Error::InvalidRequest;
            for(unsigned n=0;n<count;++n){const unsigned i=eligible[n];
                if(!RankCost(out,Ability(p,i),++p.ranks[i]))return Error::InvalidRequest;
                out.gilCost+=RefinementGil(++total);
            }
        }else{
            for(unsigned n=0;n<count;++n){const unsigned i=eligible[n];unsigned item=0,quantity=0;
                if(!RefinementCost(Ability(p,i),unsigned(p.ranks[i])+1,out.policy,item,quantity,&out.catalog))return Error::UnsupportedAbility;
                if(quantity>out.requirements[item])out.requirements[item]=static_cast<std::uint16_t>(quantity);
            }
            out.requirements[out.policy.baseItem]=static_cast<std::uint16_t>(out.requirements[out.policy.baseItem]+out.policy.baseAmount);
            // Check EVERY possible outcome before drawing. Never bias the pool
            // toward cheap candidates just because another ingredient is absent.
            if(!Affordable(before,out))return Error::Materials;
            if(!Charge(out,out.policy.baseItem,out.policy.baseAmount))return Error::InvalidRequest;
            const unsigned slot=eligible[Uniform(s,count)];++p.ranks[slot];++s.rolls;
            if(!RankCost(out,Ability(p,slot),p.ranks[slot]))return Error::InvalidRequest;
            out.chosenAbility=p.abilities[slot];out.gilCost=RefinementGil(total+1);
        }
        return Error::Ok;
    }
    case Op::UnlockFifth:{
        if(Special(p))return Error::Protected;
        if(p.fifthUnlocked)return Error::NoChange;
        if(p.native[11]!=4)return Error::InvalidRequest;
        if(IsAeon(p)){
            unsigned missing=0;
            for(unsigned item=75;item<=79;++item){
                const unsigned specific=(std::min)(4u,unsigned(before.items[item]));
                if(!Charge(out,item,specific))return Error::InvalidRequest;
                missing+=4-specific;
            }
            if(!Charge(out,80,missing))return Error::InvalidRequest;
        }else{
            const unsigned item=FifthSphere(p.native[4]);
            if(item>=ItemCount)return Error::InvalidRequest;
            const unsigned specific=(std::min)(10u,unsigned(before.items[item]));
            if(!Charge(out,item,specific)||!Charge(out,80,10-specific))return Error::InvalidRequest;
        }
        p.fifthUnlocked=1;return Error::Ok;
    }
    case Op::SetFifth:{
        if(Special(p))return Error::Protected;
        if(ProtectedAbility(p,4))return Error::Protected;
        unsigned item=0,quantity=0;
        if(!p.fifthUnlocked || !FifthCost(p,r.value,out.policy,item,quantity,&out.catalog))return Error::UnsupportedAbility;
        if(!NativeSlotsFilled(p))return Error::InvalidRequest;
        if(p.fifth==r.value)return Error::NoChange;
        const auto allowed=CustomizeEligibility(p,4,r.value,&out.catalog);if(allowed!=Error::Ok)return allowed;
        IndividualRanks(p); // Preserve old A ranks without granting them to a new ability.
        if(!Charge(out,item,quantity))return Error::InvalidRequest;
        out.gilCost=200000;
        p.fifth=r.value;p.abilities[4]=Id(s);p.ranks[4]=0;return Error::Ok;
    }
    default:break;
    }
    (void)before;return Error::InvalidRequest;
}
}
bool IsAeon(const Piece& p){return p.id&&p.native[2]&&p.native[4]>=8&&p.native[4]<18&&p.native[5]<2;}
bool ProtectedAbility(const Piece& p,unsigned slot){return IsAeon(p)&&Ability(p,slot)==0x807B;}
unsigned AeonCrest(unsigned owner){
    // Macalania PUSHAR/POPAR half-powered bits, not the differently ordered obtained bits.
    constexpr unsigned bits[]={2,16,8,32,0,0,4,0,0,0};
    return owner>=8&&owner<18?bits[owner-8]:0;
}
const char* AeonRequirement(unsigned owner){
    switch(owner){
    case 8:return "Nirvana with its Crest applied";
    case 9:return "World Champion with its Crest applied";
    case 10:return "Spirit Lance with its Crest applied";
    case 11:return "Onion Knight with its Crest applied";
    case 14:return "Masamune with its Crest applied";
    default:return "Obtain this Aeon (innate Break Damage Limit)";
    }
}
bool ReadAeonProgress(const std::uint8_t* payload,std::size_t size,AeonProgress& out){
    if(!payload||size<0x55CC+18*0x94)return false;
    AeonProgress progress{};progress.crests=payload[0xC6C];
    // Unknown progression is rejected by AeonAccess, not by ordinary gear edits.
    for(unsigned owner=8;owner<18;++owner){
        const auto* ply=payload+0x55CC+owner*0x94;
        if(ply[0x2C]&0x10)progress.obtained|=1u<<owner;
        for(unsigned kind=0;kind<2;++kind)progress.gear[2*(owner-8)+kind]=ply[0x2D+kind];
    }
    out=progress;return true;
}
Error AeonAccess(const Piece& p,unsigned slot,const AeonProgress& progress){
    if(!IsAeon(p))return Error::Ok;
    if((progress.obtained&~0x3FF00u)||(progress.crests&~0x7Fu))return Error::InvalidState;
    const unsigned owner=p.native[4],crest=AeonCrest(owner);
    if(!(progress.obtained&(1u<<owner))||(crest&&!(progress.crests&crest)))return Error::Locked;
    if(slot>=GearCount||progress.gear[2*(owner-8)+p.native[5]]!=slot||p.native[6]!=owner||
       (p.native[3]&0x0Cu)!=(p.native[5]?0u:4u))return Error::Protected;
    if(!p.native[5]){
        bool immunity=false;for(unsigned i=0;i<4;++i)immunity|=Ability(p,i)==0x807B;
        if(!immunity)return Error::Protected;
    }
    return Error::Ok;
}
std::uint16_t Ability(const Piece& p,unsigned slot){
    if(slot>4)return Empty;
    const auto word=slot==4?p.fifth:Read16(p.native+14+slot*2);
    // Both native loops skip0000 and00FF. Preserve source bytes while presenting
    // one logical empty value to the extension and material rules.
    return word==0?Empty:word;
}
unsigned AbilityRank(const Piece& p,unsigned slot){return slot<5&&Ability(p,slot)!=Empty?(p.mode==1?p.rank:p.mode==2?p.ranks[slot]:0):0;}
bool ValidPolicy(Policy p){
    return (p.mode==1||p.mode==2)&&p.baseItem>=70&&p.baseItem<=73&&p.baseAmount>=1&&p.baseAmount<=99&&
        p.refinementDivisor>=1&&p.refinementDivisor<=100&&p.fusionDivisor>=1&&p.fusionDivisor<=100&&
        p.fusionGilPerAbility>=1&&p.fusionGilPerAbility<=100000000&&p.modRecipeQuantity>=1&&p.modRecipeQuantity<=255&&
        p.devFreeMaterials<=1&&p.devFreeGil<=1&&p.devIgnoreProgression<=1;
}
bool ValidCatalog(const Catalog& catalog){
    bool any=false;
    for(unsigned i=0;i<13;++i){const auto& entry=catalog.entries[i];
        if(!entry.word){if(entry.kind||entry.item||entry.reserved)return false;continue;}
        any=true;
        if(entry.word<0x8087||entry.word>0x8FFF||entry.kind<1||entry.kind>2||entry.item>=ItemCount||entry.reserved)return false;
        for(unsigned j=0;j<i;++j)if(catalog.entries[j].word==entry.word)return false;
    }
    return !any||catalog.proof!=0;
}
const CatalogEntry* FindCatalogEntry(std::uint16_t word,const Catalog* catalog){
    if(word<0x8087||word>0x8FFF||!catalog||!catalog->proof||!ValidCatalog(*catalog))return nullptr;
    for(const auto& entry:catalog->entries)if(entry.word==word)return &entry;
    return nullptr;
}
bool ValidFifthWord(std::uint16_t word){
    // Keep paid metadata recoverable while a kernel is unavailable. This check
    // grants neither authoring nor native effect admission.
    return word==Empty||(word>=0x8000&&word<=0x8082)||(word>=0x8087&&word<=0x8FFF);
}
bool CustomizeCost(std::uint16_t word,Policy policy,unsigned& item,unsigned& quantity,bool& native,const Catalog* catalog){
    if(!ValidPolicy(policy)||!SupportedRefinement(word,catalog))return false;
    if(const auto* extra=FindCatalogEntry(word,catalog)){item=extra->item;quantity=policy.modRecipeQuantity;native=false;return true;}
    const auto row=CustomizeRecipes[word-0x8000];native=row.quantity!=0;
    item=row.item;quantity=native?row.quantity:policy.modRecipeQuantity;return true;
}
bool RefinementCost(std::uint16_t word,unsigned nextRank,Policy policy,unsigned& item,unsigned& quantity,const Catalog* catalog){
    bool native=false;unsigned customize=0;
    if(nextRank<1||nextRank>10||!CustomizeCost(word,policy,item,customize,native,catalog))return false;
    quantity=nextRank*((customize+policy.refinementDivisor-1)/policy.refinementDivisor);return true;
}
// Native main-menu option 7 admission, VA 8E1DF4; zero is its debug/all-menus case.
bool NativeCustomizeUnlocked(unsigned story){return story<=65535 && (story==0 || story>=0x448);}
unsigned FifthSphere(unsigned owner){
    constexpr unsigned spheres[]={77,78,75,76,77,79,76};
    return owner<7?spheres[owner]:ItemCount;
}
std::uint32_t RefinementGil(unsigned nextTotal){
    if(!nextTotal||nextTotal>50)return 0;
    std::uint32_t result=0;
    for(unsigned n=1;n<=nextTotal;++n)result+=1000+500*((n-1)/10);
    return result;
}
bool NativeSlotsFilled(const Piece& piece){
    if(piece.native[11]!=4)return false;
    for(unsigned i=0;i<4;++i)if(Ability(piece,i)==Empty)return false;
    return true;
}
bool FifthCost(const Piece& piece,std::uint16_t word,Policy policy,unsigned& item,unsigned& quantity,const Catalog* catalog){
    bool native=false;unsigned original=0;
    if(piece.native[5]>1||!SupportedFifth(word,catalog)||!CustomizeCost(word,policy,item,original,native,catalog))return false;
    const auto* extra=FindCatalogEntry(word,catalog);
    if(extra){if(extra->kind!=unsigned(piece.native[5])+1)return false;}
    else if(!native||CustomizeRecipes[word-0x8000].kind!=unsigned(piece.native[5])+1)return false;
    quantity=(3*original+1)/2;return true;
}
Error CustomizeEligibility(const Piece& piece,unsigned slot,std::uint16_t word,const Catalog* catalog){
    if(slot>4||piece.fifthUnlocked>1||piece.native[5]>1||!SupportedRefinement(word,catalog))return Error::UnsupportedAbility;
    if(ProtectedAbility(piece,slot))return Error::Protected;
    const auto* extra=FindCatalogEntry(word,catalog);
    if(extra){if(extra->kind!=unsigned(piece.native[5])+1)return Error::UnsupportedAbility;}
    else {const auto recipe=CustomizeRecipes[word-0x8000];
        if(!recipe.quantity||recipe.kind!=unsigned(piece.native[5])+1)return Error::UnsupportedAbility;}
    // VA8C2960..8C29AC: equal/inferior groups and FF/FE restrictions.
    // Deliberately directional: native Customize allows stronger-after-weaker.
    for(unsigned i=0;i<4u+piece.fifthUnlocked;++i){
        if(i==slot)continue;
        const auto existing=Ability(piece,i);if(existing==Empty)continue;
        if(existing==word)return Error::Duplicate;
        if(!SupportedRefinement(existing,catalog))return Error::UnsupportedAbility;
        // Hook-only identities cannot index a vanilla grouping table.
        if(!extra&&!FindCatalogEntry(existing,catalog)){
            const auto candidate=CustomizeRules[word-0x8000],rule=CustomizeRules[existing-0x8000];
            if((rule.group==candidate.group&&rule.level>=candidate.level)||
               (rule.variant==255&&candidate.variant==254))return Error::IncompatibleAbility;
        }
    }
    return Error::Ok;
}
bool SupportedFifth(std::uint16_t word,const Catalog* catalog){return SupportedRefinement(word,catalog);}
bool SupportedRefinement(std::uint16_t word,const Catalog* catalog){return (word>=0x8000 && word<=0x8082)||FindCatalogEntry(word,catalog);}
bool GenericRefinement(std::uint16_t word,const Catalog* catalog){
    // Creation coverage is independent of the 26 bespoke refinement effects.
    return SupportedRefinement(word,catalog)&&!((word>=0x8062&&word<=0x8079)||word==0x8054||word==0x8055);
}
void RefineAbilityRow(std::uint16_t word,unsigned rank,std::uint8_t* row,const Catalog* catalog){
    if(!row || !rank || rank>10)return;
    if(word>=0x8062 && word<=0x8079){
        row[0x55]=static_cast<std::uint8_t>((std::min)(255u,unsigned(row[0x55])+rank));
    }else if(GenericRefinement(word,catalog) && !row[0x55] && !row[0x56] && !row[0x57]){
        // Field RVA3867CD consumes BYTE+55 as percent and WORD+56 as a bitset:
        // 0400 STR,0800 MAG;1000 is Defense. Stock fallback rows have no numeric channel.
        // Preserve original flags/statuses and never overwrite modded channels.
        row[0x55]=static_cast<std::uint8_t>(rank);row[0x57]=0x0C;
    }
}
Error Validate(const State& s){
    if(s.version!=1 || s.nextId==0 || s.nextId>std::numeric_limits<std::uint64_t>::max()-1024 || s.revision==std::numeric_limits<std::uint64_t>::max() || s.rolls==std::numeric_limits<std::uint64_t>::max())return Error::InvalidState;
    std::set<std::uint64_t> ids;
    for(const auto& p:s.pieces){
        if(!p.id){
            // Vacant native slots may retain arbitrary old record bytes.
            // Their extension must be empty; those bytes are never an identity.
            if(p.native[2] || p.mode || p.rank || p.fifthUnlocked || p.fifth!=Empty)return Error::InvalidState;
            for(unsigned i=0;i<5;++i)if(p.abilities[i] || p.ranks[i])return Error::InvalidState;
            continue;
        }
        if(p.native[11]>4 || p.mode>2 || p.rank>10 || p.fifthUnlocked>1 || (p.fifthUnlocked && p.native[11]!=4))return Error::InvalidState;
        if(bool(p.native[2])!=bool(p.id))return Error::InvalidState;
        // STL takes an aligned reference; never bind it to the packed wire field.
        const std::uint64_t pieceId=p.id;
        if(pieceId && (pieceId>=s.nextId || !ids.insert(pieceId).second))return Error::InvalidState;
        if((!p.id && (p.mode||p.rank||p.fifthUnlocked)) || (p.mode!=1 && p.rank))return Error::InvalidState;
        for(unsigned i=0;i<5;++i){const auto a=Ability(p,i);
            if(!ValidAbility(a) || p.ranks[i]>10 || (p.mode!=2 && p.ranks[i]))return Error::InvalidState;
            if(i<4 && i>=p.native[11] && a!=Empty)return Error::InvalidState;
            if(i==4 && !p.fifthUnlocked && a!=Empty)return Error::InvalidState;
            if(i==4 && !ValidFifthWord(a))return Error::InvalidState;
            if(!p.id){if(p.abilities[i]||p.ranks[i])return Error::InvalidState;continue;}
            if((a==Empty)!=(p.abilities[i]==0) || (a==Empty && p.ranks[i]))return Error::InvalidState;
            const std::uint64_t abilityId=p.abilities[i];
            if(abilityId && (abilityId>=s.nextId || !ids.insert(abilityId).second))return Error::InvalidState;
        }
    }
    for(auto n:s.items)if(n>255)return Error::InvalidState;
    return Error::Ok;
}
Error Import(const std::uint8_t* records,const std::uint16_t* items,std::uint64_t seed,State& out){
    if(!records || !items)return Error::InvalidRequest;
    State s{};s.version=1;s.nextId=1;s.rng=seed;
    for(unsigned i=0;i<GearCount;++i)Initialize(s,s.pieces[i],records+i*NativeBytes);
    std::memcpy(s.items,items,sizeof(s.items));const auto error=Validate(s);if(error==Error::Ok)out=s;return error;
}
Error Preview(const State& s,const Request& r,Plan& out,Economy economy){
    out={};out.after=s;
    if(!ValidPolicy(economy.policy)||!ValidCatalog(economy.catalog))return Error::InvalidPolicy;
    auto error=Validate(s);if(error!=Error::Ok)return error;
    if(r.slot>=GearCount)return Error::InvalidRequest;
    if(r.revision!=s.revision || r.pieceId!=s.pieces[r.slot].id)return Error::Stale;
    if(economy.customizeUnlocked>1)return Error::InvalidState;
    const bool observedEvent=r.op==Op::Create||r.op==Op::Swap||r.op==Op::Retire;
    if(!observedEvent && !economy.customizeUnlocked && !economy.policy.devIgnoreProgression)return Error::Locked;
    if(!observedEvent){const auto access=AeonAccess(s.pieces[r.slot],r.slot,economy.aeons);if(access!=Error::Ok)return access;}
    Plan planned{};planned.after=s;planned.policy=economy.policy;planned.gilBefore=economy.gil;planned.customizeUnlocked=economy.customizeUnlocked;planned.aeons=economy.aeons;planned.catalog=economy.catalog;
    error=Edit(s,r,planned);
    if(error==Error::Ok&&!observedEvent&&IsAeon(s.pieces[r.slot])){
        if(planned.gilCost>UINT32_MAX/2u)error=Error::InvalidRequest;else planned.gilCost*=2;
        for(unsigned i=0;i<5;++i)if(ProtectedAbility(s.pieces[r.slot],i)&&
            (Ability(planned.after.pieces[r.slot],i)!=0x807B||planned.after.pieces[r.slot].abilities[i]!=s.pieces[r.slot].abilities[i]))error=Error::Protected;
    }
    if(r.op!=Op::Refine||economy.policy.mode==1)std::memcpy(planned.requirements,planned.costs,sizeof(planned.costs));
    if(error==Error::Ok){
        if(!Affordable(s,planned,r.op==Op::UnlockFifth,s.pieces[r.slot].native[4]))error=Error::Materials;
        planned.gilDebit=economy.policy.devFreeGil?0:planned.gilCost;
        if(error==Error::Ok&&planned.gilDebit>economy.gil)error=Error::Gil;
    }
    if(error==Error::Ok){
        for(unsigned i=0;i<ItemCount;++i)planned.after.items[i]=static_cast<std::uint16_t>(s.items[i]-(economy.policy.devFreeMaterials?0:planned.costs[i]));
        ++planned.after.revision;error=Validate(planned.after);
    }
    if(error!=Error::Ok){planned.after=s;planned.chosenAbility=0;}
    out=planned;return error;
}
const char* Message(Error e){
    switch(e){case Error::Ok:return "Ready";case Error::InvalidState:return "Inventory or extension is inconsistent";
    case Error::Stale:return "The inventory changed; review a new preview";case Error::InvalidRequest:return "This operation is not valid for the selected piece";
    case Error::EmptyPiece:return "Select an existing piece";case Error::Equipped:return "Unequip this piece first";
    case Error::Protected:return "This piece or refinement mode is protected";case Error::UnsupportedAbility:return "An ability is outside the supported refinement or fifth-slot catalog";
    case Error::Materials:return "Not enough materials";case Error::Maximum:return "All eligible abilities are at maximum rank";
    case Error::Duplicate:return "Duplicate ability or repeated transfer slot";case Error::NoChange:return "Nothing would change";
    case Error::IncompatibleAbility:return "Native Customize rejects this ability combination";
    case Error::Locked:return "Unlock Customize in the story, or use the explicit Workshop development setting";
    case Error::Gil:return "Not enough Gil";case Error::InvalidPolicy:return "Invalid Workshop economy settings; check F8 Dev or the INI";}
    return "Unknown error";
}
}
int ws_import(const std::uint8_t* p,const std::uint16_t* i,std::uint64_t seed,workshop::State* out){return out?static_cast<int>(workshop::Import(p,i,seed,*out)):3;}
int ws_validate(const workshop::State* p){return p?static_cast<int>(workshop::Validate(*p)):3;}
// Old clients supplied a smaller Plan buffer: fail without writing to it.
int ws_plan(const workshop::State*,const workshop::Request*,workshop::Plan*){return 3;}
int ws_plan_economy_v3(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*){return 3;}
int ws_plan_economy_v4(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*){return 3;}
int ws_plan_economy_v5(const workshop::State* s,const workshop::Request* r,const workshop::Economy* e,workshop::Plan* p){return s&&r&&e&&p?static_cast<int>(workshop::Preview(*s,*r,*p,*e)):3;}
int ws_plan_economy(const workshop::State*,const workshop::Request*,const workshop::Economy*,workshop::Plan*){return 3;}
unsigned ws_plan_abi(){return 5;}
int ws_aeon_progress(const std::uint8_t* payload,std::size_t size,workshop::AeonProgress* p){return p&&workshop::ReadAeonProgress(payload,size,*p)?0:3;}
int ws_aeon_access(const workshop::Piece* p,unsigned slot,const workshop::AeonProgress* a){return p&&a?static_cast<int>(workshop::AeonAccess(*p,slot,*a)):3;}
const char* ws_message(int e){return workshop::Message(static_cast<workshop::Error>(e));}

unsigned ws_customize_unlocked(unsigned story){return workshop::NativeCustomizeUnlocked(story)?1u:0u;}
unsigned ws_fifth_cost(unsigned kind,unsigned word,const workshop::Policy* policy,unsigned* item,unsigned* quantity){
    if(kind>1||word>65535||!policy||!item||!quantity)return 0;
    workshop::Piece piece{};piece.native[5]=static_cast<std::uint8_t>(kind);
    unsigned selected=0,amount=0;
    if(!workshop::FifthCost(piece,static_cast<std::uint16_t>(word),*policy,selected,amount))return 0;
    *item=selected;*quantity=amount;return 1;
}
int ws_customize_eligibility(const workshop::Piece* piece,unsigned slot,unsigned word){
    return piece&&word<=65535?static_cast<int>(workshop::CustomizeEligibility(*piece,slot,static_cast<std::uint16_t>(word))):static_cast<int>(workshop::Error::InvalidRequest);
}

unsigned ws_fifth_cost_v5(unsigned kind,unsigned word,const workshop::Policy* policy,const workshop::Catalog* catalog,unsigned* item,unsigned* quantity){
    if(kind>1||word>65535||!policy||!catalog||!item||!quantity||!workshop::ValidCatalog(*catalog))return 0;
    workshop::Piece piece{};piece.native[5]=static_cast<std::uint8_t>(kind);unsigned selected=0,amount=0;
    if(!workshop::FifthCost(piece,static_cast<std::uint16_t>(word),*policy,selected,amount,catalog))return 0;
    *item=selected;*quantity=amount;return 1;
}
int ws_customize_eligibility_v5(const workshop::Piece* piece,unsigned slot,unsigned word,const workshop::Catalog* catalog){
    return piece&&catalog&&word<=65535&&workshop::ValidCatalog(*catalog)?static_cast<int>(workshop::CustomizeEligibility(*piece,slot,static_cast<std::uint16_t>(word),catalog)):static_cast<int>(workshop::Error::InvalidRequest);
}
