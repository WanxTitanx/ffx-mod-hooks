#include "lifecycle.h"
#include <algorithm>
#include <array>
#include <cstring>
namespace workshop {
bool Lifecycle::Owned() const{return active_ && owner_==std::this_thread::get_id();}
bool Lifecycle::Begin(bool enabled,bool profile,const State& s){
    if(active_ && !Owned())return false;
    End();if(!enabled || !profile || Validate(s)!=Error::Ok)return false;
    owner_=std::this_thread::get_id();state_=s;active_=true;return true;
}
void Lifecycle::End(){if(!active_ || Owned()){active_=false;state_=State{};owner_={};}}
bool Lifecycle::Snapshot(State& out) const {if(!Owned())return false;out=state_;return true;}
bool Lifecycle::Observe(InventoryEvent event,unsigned first,unsigned second,unsigned owner,
                       std::uint64_t revision,const std::uint8_t* before,const std::uint8_t* after){
    if(!Owned())return false;
    auto reject=[&]{active_=false;return false;};
    if(!before || !after || revision!=state_.revision || first>=GearCount)return reject();
    for(unsigned i=0;i<GearCount;++i)if(std::memcmp(before+i*22,state_.pieces[i].native,22)!=0)return reject();
    State next=state_;
    switch(event){
    case InventoryEvent::Created:{
        Request r{};r.op=Op::Create;r.slot=static_cast<std::uint16_t>(first);r.revision=state_.revision;
        r.pieceId=state_.pieces[first].id;std::memcpy(r.gearTemplate,after+first*22,22);
        Plan plan{};if(Preview(state_,r,plan)!=Error::Ok)return reject();next=plan.after;break;
    }
    case InventoryEvent::Swapped:
        if(second>=GearCount || first==second)return reject();
        std::swap(next.pieces[first],next.pieces[second]);++next.revision;break;
    case InventoryEvent::Removed:{
        if(!next.pieces[first].id)return reject();
        std::array<std::uint8_t,22> native{};std::copy(next.pieces[first].native,next.pieces[first].native+22,native.begin());native[2]=0;
        auto& p=next.pieces[first];p=Piece{};p.fifth=Empty;std::copy(native.begin(),native.end(),p.native);++next.revision;break;
    }
    case InventoryEvent::Equipped:{
        // first is the newly equipped regular slot; second is the previous slot,
        // or GearCount when the character had none. Native already did the write.
        if(owner>=18 || !next.pieces[first].id || next.pieces[first].native[4]!=owner)return reject();
        if(second<GearCount){
            if(!next.pieces[second].id || next.pieces[second].native[6]!=owner || next.pieces[second].native[5]!=next.pieces[first].native[5])return reject();
            next.pieces[second].native[6]=255;
        }else if(second!=GearCount)return reject();
        next.pieces[first].native[6]=static_cast<std::uint8_t>(owner);++next.revision;break;
    }
    case InventoryEvent::Unequipped:
        if(owner>=18 || !next.pieces[first].id || next.pieces[first].native[6]!=owner)return reject();
        next.pieces[first].native[6]=255;++next.revision;break;
    default:return reject();
    }
    for(unsigned i=0;i<GearCount;++i)if(std::memcmp(after+i*22,next.pieces[i].native,22)!=0)return reject();
    if(Validate(next)!=Error::Ok)return reject();
    state_=next;return true;
}
}
