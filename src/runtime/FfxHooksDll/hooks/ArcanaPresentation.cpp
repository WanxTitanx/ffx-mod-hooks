#include "ArcanaPresentation.h"
#include <algorithm>

namespace FfxHooks::Arcana::Presentation {
namespace {
bool ValidOwned(const Owned& owned) noexcept {
    return std::all_of(owned.begin(),owned.end(),[](std::uint8_t value){return value<=1;});
}
bool Same(const View& a,const View& b) noexcept {
    return a.kind==b.kind&&a.card==b.card&&a.page==b.page&&a.legacy==b.legacy;
}
constexpr GuidePage guide[]={
    {"Your first Tarot card",
     "Tarot cards grant bonuses while equipped. Collect them as you explore Spira and complete your journey. Read the effects of the card you received."},
    {"The Tarot collection",
     "The deck has 78 traditional cards: 22 Major Arcana and 56 Minor Arcana, plus two Weapon cards and ten Dark Aeon cards. Omega Weapon awards both Weapon cards; each Dark Aeon awards its own card. These special cards cost two points in Constellation."},
    {"Finding more cards",
     "Find cards through exploration, conversations and challenges. A story milestone can unlock a card without giving it to you. Missed cards have recovery routes after free airship travel; challenge requirements still apply."},
    {"Equip a Tarot card",
     "Open Equip, choose a character, select a Tarot slot and choose an acquired card. Read its description before confirming. Tarot slots are separate from weapons and armor. Receiving a card does not equip it."},
    {"Read the effects",
     "Card descriptions list bonuses, conditions and drawbacks. Check Status and Auto-Abilities to review your equipped effects. Some bonuses apply only to certain actions or rewards; not every bonus affects the whole party."},
    {"One card, one wearer",
     "Your party shares one copy of each card. Only one character can wear it at a time. Choosing a card worn by someone else asks you to confirm a transfer. Select Unequip to free it; the card stays in your collection."},
    {"Twin Arcana",
     "Twin Arcana gives each character two Tarot slots. Equip any two acquired cards, including two Major Arcana. Each card still has only one wearer."},
    {"Constellation",
     "Constellation gives each character up to three slots and four points. A Major costs two; a Minor costs one. Equip two Majors, one Major and two Minors, or three Minors. Two Majors lock the third slot. An unused point does not create a fourth slot."},
    {"Change modes safely",
     "The mode applies to the whole party. Change it from the Tarot list. Switching to Twin Arcana reviews occupied third slots for every character. Confirming removes those cards from their slots, not from the collection. Cancel keeps the current setup."},
    {"Combine effects carefully",
     "Some bonuses add together; repeated abilities and other bonuses may use only the strongest value. Existing limits apply unless an effect changes them. Read conditions and active effects: two cards do not necessarily double a bonus."},
    {"Review your collection",
     "The Tarot list shows owned cards, their wearers and requirements for locked cards. Collect the other 77 cards to obtain The World. Choose Tarot guide to read this introduction again."},
    {"Ready to explore",
     "Cards belong to this save. Unequipping or transferring does not consume them. Save your game to keep your progress. When Equip is available, try your first card and check its effects. Reopen the Tarot guide whenever you need a reminder."}
};
static_assert(sizeof(guide)/sizeof(guide[0])==kGuidePageCount);
}

bool Validate(const State& state,const Owned& owned) noexcept {
    if(!ValidOwned(owned)||state.count>kCardCount||state.page>=kGuidePageCount||
       static_cast<unsigned>(state.intro)>static_cast<unsigned>(Intro::Complete))return false;
    std::array<bool,kCardCount> seen{};
    for(unsigned i=0;i<state.count;++i){
        const auto id=state.pending[i];
        if(id>=kCardCount||!owned[id]||seen[id])return false;
        seen[id]=true;
    }
    for(unsigned i=state.count;i<kCardCount;++i)if(state.pending[i]!=kNoCard)return false;
    const bool any=std::any_of(owned.begin(),owned.end(),[](std::uint8_t value){return value!=0;});
    if(!any)return state.intro==Intro::Waiting&&state.firstCard==kNoCard&&state.count==0&&state.page==0&&!state.legacy;
    if(state.intro==Intro::Waiting||state.firstCard>=kCardCount||!owned[state.firstCard])return false;
    if(state.intro==Intro::Pending&&state.page!=0)return false;
    if(state.intro==Intro::Complete&&state.page!=kGuidePageCount-1)return false;
    return true;
}

bool AdoptLegacy(State& state,const Owned& owned) noexcept {
    if(!ValidOwned(owned))return false;
    State next;
    for(unsigned id=0;id<kCardCount;++id)if(owned[id]){
        next.firstCard=static_cast<std::uint8_t>(id);next.intro=Intro::Pending;next.legacy=true;break;
    }
    state=next;return true;
}

bool ObserveAwards(State& state,const Owned& before,const Owned& after) noexcept {
    if(!Validate(state,before)||!ValidOwned(after))return false;
    for(unsigned id=0;id<kCardCount;++id)if(before[id]&&!after[id])return false;
    State next=state;
    const auto append=[&](unsigned id){
        if(before[id]||!after[id])return true;
        if(next.count>=kCardCount)return false;
        next.pending[next.count++]=static_cast<std::uint8_t>(id);
        if(next.intro==Intro::Waiting){
            next.intro=Intro::Pending;next.firstCard=static_cast<std::uint8_t>(id);
        }
        return true;
    };
    // World is reconciled in the same transaction as the last ordinary card.
    // Numeric order alone would announce ID 21 before the card that earned it.
    for(unsigned id=0;id<kCardCount;++id)if(id!=kWorldCard&&!append(id))return false;
    if(!append(kWorldCard)||!Validate(next,after))return false;
    state=next;return true;
}

View Current(const State& state) noexcept {
    if(state.count>kCardCount||state.page>=kGuidePageCount)return {};
    if(state.intro==Intro::Reading)return {Kind::Introduction,state.firstCard,state.page,state.legacy};
    if(state.intro!=Intro::Pending&&state.intro!=Intro::Complete)return {};
    if(state.count)return {Kind::Notice,state.pending[0],0,false};
    if(state.intro==Intro::Pending)return {Kind::Introduction,state.firstCard,0,state.legacy};
    return {};
}

bool Acknowledge(State& state,const View& expected,Navigation action) noexcept {
    const auto current=Current(state);
    if(current.kind==Kind::None||!Same(current,expected)||action==Navigation::None)return false;
    if(current.kind==Kind::Notice){
        if(action!=Navigation::Next)return false;
        for(unsigned i=1;i<state.count;++i)state.pending[i-1]=state.pending[i];
        state.pending[--state.count]=kNoCard;
        if(state.intro==Intro::Pending){state.intro=Intro::Reading;state.page=0;}
        return true;
    }
    if(action==Navigation::Skip){
        // Completion is a presentation receipt, not a card award. Keep queued
        // acquisition notices and the independently accessible manual guide.
        state.page=kGuidePageCount-1;state.intro=Intro::Complete;return true;
    }
    if(action==Navigation::Previous){
        if(!state.page)return false;
        --state.page;state.intro=Intro::Reading;return true;
    }
    if(action!=Navigation::Next)return false;
    if(state.page+1==kGuidePageCount)state.intro=Intro::Complete;
    else {++state.page;state.intro=Intro::Reading;}
    return true;
}

const GuidePage* Guide(unsigned page) noexcept {return page<kGuidePageCount?&guide[page]:nullptr;}

Navigation InputLatch::Poll(bool focused,bool available,bool confirm,bool back) noexcept {
    if(!focused||!available){armed_=false;return Navigation::None;}
    if(!confirm&&!back){armed_=true;return Navigation::None;}
    if(!armed_)return Navigation::None;
    armed_=false;
    if(confirm==back)return Navigation::None;
    return confirm?Navigation::Next:Navigation::Previous;
}
}
