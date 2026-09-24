#include "workshop.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace workshop {
namespace {
std::uint16_t Read16(const std::uint8_t* p){return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));}
void Write16(std::uint8_t* p,std::uint16_t v){p[0]=static_cast<std::uint8_t>(v);p[1]=static_cast<std::uint8_t>(v>>8);}
bool ValidAbility(std::uint16_t id){return id==Empty || (id>=0x8000 && id<0x8400);}
unsigned Capacity(const Piece& p){return p.native[11]+p.fifthUnlocked;}
bool Special(const Piece& p){return (p.native[3]&0x0C)!=0 || p.native[4]>6 || p.native[5]>1;}
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
    if(item>=ItemCount || amount>255u-p.costs[item])return false;
    p.costs[item]=static_cast<std::uint16_t>(p.costs[item]+amount);return true;
}
// Initial executable effect slice: numeric stats and Auto-Protect/Auto-Shell.
// Other recipes remain catalog proposals; they are never charged as live effects.
bool RankCost(Plan& p,std::uint16_t word,unsigned rank){
    if(!SupportedRefinement(word) || rank<1 || rank>10)return false;
    const unsigned id=word-0x8000;unsigned base=0,mark=75;
    if(id>=98 && id<=121){constexpr unsigned spheres[]={87,89,88,90,85,86};base=spheres[(id-98)/4];}
    else{base=id==84?56:57;mark=base;}
    return Charge(p,base,1+(rank-1)/3) && ((rank!=4 && rank!=7 && rank!=10)||Charge(p,mark,1));
}
bool CatchUp(Plan& p,std::uint16_t ability,unsigned rank){
    for(unsigned r=1;r<=rank;++r)if(!RankCost(p,ability,r))return false;
    return true;
}
bool Evolves(std::uint16_t old,std::uint16_t replacement){
    if(old<0x8000 || replacement<0x8000)return false;
    const unsigned a=old-0x8000,b=replacement-0x8000;
    if(a>=98 && a<121 && ((a-98)%4)<3)return b==a+1;
    return (a>=47 && a<=75 && (a-47)%4==0 && b+1==a);
}
Error Edit(const State& before,const Request& r,Plan& out){
    auto& s=out.after;auto& p=s.pieces[r.slot];
    if(r.op==Op::Create){
        if(p.id || !r.gearTemplate[2] || r.gearTemplate[6]!=0xFF || r.gearTemplate[11]>4)return Error::InvalidRequest;
        Initialize(s,p,r.gearTemplate);return Error::Ok;
    }
    if(!p.id)return Error::EmptyPiece;
    // Event operations model host-observed inventory changes. Workshop mutators
    // require unequipped gear, before any material or metadata changes.
    if(r.op!=Op::Swap && Equipped(p))return Error::Equipped;
    switch(r.op){
    case Op::Swap:{
        if(r.other>=GearCount || r.other==r.slot)return Error::InvalidRequest;
        if(s.pieces[r.other].id!=r.otherId)return Error::Stale;
        std::swap(p,s.pieces[r.other]);return Error::Ok;
    }
    case Op::Retire:
        if(Special(p))return Error::Protected;
        {std::array<std::uint8_t,22> native{};std::copy(p.native,p.native+22,native.begin());native[2]=0;Initialize(s,p,native.data());}
        return Error::Ok;
    case Op::Reforge:{
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
        if(Special(donor))return Error::Protected;
        if(Equipped(donor))return Error::Equipped;
        if(donor.mode!=p.mode)return Error::InvalidRequest;
        if(r.count==2 && (r.from[0]==r.from[1] || r.to[0]==r.to[1]))return Error::Duplicate;
        for(unsigned i=0;i<r.count;++i){const unsigned src=r.from[i],dst=r.to[i];
            if(src>=Capacity(donor) || dst>=Capacity(p) || Ability(donor,src)==Empty)return Error::InvalidRequest;
            const auto ability=Ability(donor,src);
            if(p.mode==1 && !CatchUp(out,ability,p.rank))return Error::UnsupportedAbility;
            SetAbility(p,dst,ability);p.abilities[dst]=donor.abilities[src];
            p.ranks[dst]=p.mode==2?donor.ranks[src]:0;
        }
        {std::array<std::uint8_t,22> native{};std::copy(donor.native,donor.native+22,native.begin());native[2]=0;Initialize(s,donor,native.data());}
        return Charge(out,73,r.count)?Error::Ok:Error::InvalidRequest;
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
        SetAbility(p,r.value,Empty);p.abilities[r.value]=0;p.ranks[r.value]=0;
        return Charge(out,95,1)?Error::Ok:Error::InvalidRequest;
    }
    case Op::Evolve:{
        if(Special(p))return Error::Protected;
        const unsigned slot=r.to[0];if(slot>=Capacity(p) || !Evolves(Ability(p,slot),r.value))return Error::InvalidRequest;
        // Replacement is a new instance; B never inherits the old slot's rank.
        if(p.mode==1 && !CatchUp(out,r.value,p.rank))return Error::UnsupportedAbility;
        SetAbility(p,slot,r.value);p.abilities[slot]=Id(s);p.ranks[slot]=0;
        return Charge(out,75,1)?Error::Ok:Error::InvalidRequest;
    }
    case Op::Mode:
        if(Special(p))return Error::Protected;
        if(r.value<1 || r.value>2)return Error::InvalidRequest;
        if(p.mode==r.value)return Error::NoChange;
        if(p.mode!=0)return Error::Protected; // No mode-conversion rank exploit.
        p.mode=static_cast<std::uint8_t>(r.value);return Error::Ok;
    case Op::Refine:{
        if(Special(p))return Error::Protected;
        std::array<unsigned,5> eligible{};unsigned count=0,occupied=0;
        for(unsigned i=0;i<Capacity(p);++i)if(Ability(p,i)!=Empty){
            ++occupied;if(!SupportedRefinement(Ability(p,i)))return Error::UnsupportedAbility;
            if(p.ranks[i]<10)eligible[count++]=i;
        }
        if(!occupied || !p.mode)return Error::InvalidRequest;
        if(p.mode==1){
            if(p.rank==10)return Error::Maximum;
            ++p.rank;
            for(unsigned i=0;i<Capacity(p);++i)if(Ability(p,i)!=Empty && !RankCost(out,Ability(p,i),p.rank))return Error::InvalidRequest;
        }else{
            if(!count)return Error::Maximum;
            constexpr unsigned catalysts[]={72,78,87,73,93,79,76};
            if(!Charge(out,catalysts[p.native[4]],1) || !Charge(out,73,1))return Error::InvalidRequest;
            const unsigned slot=eligible[Uniform(s,count)];++p.ranks[slot];++s.rolls;out.chosenAbility=p.abilities[slot];
        }
        return Error::Ok;
    }
    case Op::UnlockFifth:
        if(Special(p))return Error::Protected;
        if(p.fifthUnlocked)return Error::NoChange;
        if(p.native[11]!=4)return Error::InvalidRequest;
        p.fifthUnlocked=1;return Charge(out,84,1)?Error::Ok:Error::InvalidRequest;
    case Op::SetFifth:
        if(Special(p))return Error::Protected;
        if(!p.fifthUnlocked || !SupportedRefinement(r.value))return Error::UnsupportedAbility;
        if(p.fifth==r.value)return Error::NoChange;
        if(p.mode==1 && !CatchUp(out,r.value,p.rank))return Error::UnsupportedAbility;
        if(!RankCost(out,r.value,1))return Error::InvalidRequest;
        p.fifth=r.value;p.abilities[4]=Id(s);p.ranks[4]=0;return Error::Ok;
    default:break;
    }
    (void)before;return Error::InvalidRequest;
}
}
std::uint16_t Ability(const Piece& p,unsigned slot){
    if(slot>4)return Empty;
    const auto word=slot==4?p.fifth:Read16(p.native+14+slot*2);
    // Both native loops skip0000 and00FF. Preserve source bytes while presenting
    // one logical empty value to the extension and material rules.
    return word==0?Empty:word;
}
bool SupportedRefinement(std::uint16_t word){return (word>=0x8062 && word<=0x8079) || word==0x8054 || word==0x8055;}
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
            if(i==4 && a!=Empty && !SupportedRefinement(a))return Error::InvalidState;
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
Error Preview(const State& s,const Request& r,Plan& out){
    auto error=Validate(s);if(error!=Error::Ok)return error;
    if(r.slot>=GearCount)return Error::InvalidRequest;
    if(r.revision!=s.revision || r.pieceId!=s.pieces[r.slot].id)return Error::Stale;
    Plan planned{};planned.after=s;error=Edit(s,r,planned);if(error!=Error::Ok)return error;
    for(unsigned i=0;i<ItemCount;++i){if(planned.costs[i]>s.items[i])return Error::Materials;planned.after.items[i]=static_cast<std::uint16_t>(s.items[i]-planned.costs[i]);}
    ++planned.after.revision;error=Validate(planned.after);if(error==Error::Ok)out=planned;return error;
}
const char* Message(Error e){
    switch(e){case Error::Ok:return "Ready";case Error::InvalidState:return "Inventory or extension is inconsistent";
    case Error::Stale:return "The inventory changed; review a new preview";case Error::InvalidRequest:return "This operation is not valid for the selected piece";
    case Error::EmptyPiece:return "Select an existing piece";case Error::Equipped:return "Unequip this piece first";
    case Error::Protected:return "This piece or refinement mode is protected";case Error::UnsupportedAbility:return "An ability has no verified refinement effect yet";
    case Error::Materials:return "Not enough materials";case Error::Maximum:return "All eligible abilities are at maximum rank";
    case Error::Duplicate:return "Choose different source and destination slots";case Error::NoChange:return "Nothing would change";}
    return "Unknown error";
}
}
int ws_import(const std::uint8_t* p,const std::uint16_t* i,std::uint64_t seed,workshop::State* out){return out?static_cast<int>(workshop::Import(p,i,seed,*out)):3;}
int ws_validate(const workshop::State* p){return p?static_cast<int>(workshop::Validate(*p)):3;}
int ws_plan(const workshop::State* s,const workshop::Request* r,workshop::Plan* p){return s&&r&&p?static_cast<int>(workshop::Preview(*s,*r,*p)):3;}
const char* ws_message(int e){return workshop::Message(static_cast<workshop::Error>(e));}
