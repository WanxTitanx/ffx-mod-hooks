#include "effects.h"
#include <algorithm>
#include <cstring>
namespace workshop {
namespace {
unsigned Rank(const Piece& p,unsigned i){return p.mode==1?p.rank:p.mode==2?p.ranks[i]:0;}
}
bool Effects::Active() const {return active_ && owner_==std::this_thread::get_id();}
bool Effects::Begin(bool enabled,bool profileVerified,const State& state){
    if(active_ && owner_!=std::this_thread::get_id())return false;
    End();
    if(!enabled || !profileVerified || Validate(state)!=Error::Ok)return false;
    // The admitted stock kernel has134 rows. Unknown mod IDs must never reach
    // its native row reader under a mismatched profile.
    for(const auto& piece:state.pieces)if(piece.id)
        for(unsigned i=0;i<5;++i)if(Ability(piece,i)!=Empty && Ability(piece,i)>=0x8086)return false;
    state_=state;owner_=std::this_thread::get_id();active_=true;return true;
}
void Effects::End(){if(!active_ || owner_==std::this_thread::get_id()){active_=false;state_=State{};owner_={};}}
bool Effects::Gear(unsigned slot,const std::uint8_t* current,std::array<std::uint8_t,24>& out) const {
    if(!Active() || slot>=GearCount || !current)return false;
    const auto& p=state_.pieces[slot];
    if(!p.id || std::memcmp(current,p.native,22)!=0)return false;
    std::copy(current,current+22,out.begin());const auto fifth=p.fifthUnlocked?p.fifth:Empty;
    out[22]=static_cast<std::uint8_t>(fifth);out[23]=static_cast<std::uint8_t>(fifth>>8);return true;
}
bool Effects::AbilityRow(unsigned slot,unsigned abilitySlot,const std::uint8_t* native,std::array<std::uint8_t,108>& out) const {
    if(!Active() || slot>=GearCount || abilitySlot>=5 || !native)return false;
    const auto& p=state_.pieces[slot];const auto id=Ability(p,abilitySlot);
    if(!p.id || !p.abilities[abilitySlot] || id==Empty || (abilitySlot==4 && !p.fifthUnlocked))return false;
    std::copy(native,native+108,out.begin());
    // +1 percentage point per rank, not a global kernel modification. Each
    // occurrence has its own view, including two identical native IDs.
    if(id>=0x8062 && id<=0x8079)out[0x55]=static_cast<std::uint8_t>(std::min(255u,unsigned(native[0x55])+Rank(p,abilitySlot)));
    return true;
}
int Effects::AfterStatus(int damage,bool applied,bool magic,unsigned weapon,unsigned armor,unsigned owner) const {
    if(!Active() || !applied || damage<=0 || owner>6)return damage;
    unsigned rank=0;const auto desired=magic?0x8054u:0x8055u;
    for(auto slot:{weapon,armor}){
        if(slot>=GearCount)continue;
        const auto& p=state_.pieces[slot];
        if(!p.id || p.native[4]!=owner || p.native[6]!=owner)return damage;
        for(unsigned i=0;i<4u+p.fifthUnlocked;++i)if(Ability(p,i)==desired)rank=std::max(rank,Rank(p,i));
    }
    // Highest applicable equipped rank; duplicates never stack the reduction.
    return static_cast<int>(static_cast<std::int64_t>(damage)*(100u-rank)/100u);
}
}
