#include <cstdio>

#if !__has_include("../hooks/ArcanaPresentation.h")
int main() {
    std::puts("FAIL: Arcana presentation state is not implemented");
    return 1;
}
#else
#include "../hooks/ArcanaPresentation.h"
#include <array>

namespace {
using namespace FfxHooks::Arcana::Presentation;
unsigned checks=0, failures=0;
void Check(bool value,const char* label) {
    ++checks;
    if(!value){++failures;std::printf("FAIL: %s\n",label);}
}
Owned Empty(){return {};}
void CompleteIntroduction(State& state) {
    for(unsigned page=0;page<kGuidePageCount;++page){
        const auto view=Current(state);
        Check(view.kind==Kind::Introduction&&view.page==page,"every introduction page appears in order");
        Check(Acknowledge(state,view,Navigation::Next),"fresh page acknowledgement advances");
    }
}
void FirstAndQueue() {
    State state;auto owned=Empty();
    Check(Validate(state,owned)&&Current(state).kind==Kind::None,"empty save does not introduce Tarot early");
    auto before=owned;owned[36]=1;
    Check(ObserveAwards(state,before,owned),"first non-Fool acquisition is recorded");
    auto view=Current(state);
    Check(view.kind==Kind::Notice&&view.card==36,"first notice names the actual first card");
    before=owned;owned[0]=1;
    Check(ObserveAwards(state,before,owned),"later acquisition joins the queue");
    Check(Acknowledge(state,view,Navigation::Next),"acknowledge first notice");
    Check(Current(state).kind==Kind::Introduction&&Current(state).card==36,"introduction precedes the next notice");
    Check(!Acknowledge(state,view,Navigation::Next),"stale notice acknowledgement cannot skip a guide page");
    CompleteIntroduction(state);
    view=Current(state);
    Check(view.kind==Kind::Notice&&view.card==0,"remaining notice follows the completed introduction");
    Check(Acknowledge(state,view,Navigation::Next)&&Current(state).kind==Kind::None,"queue drains without repeating tutorial");
    Check(ObserveAwards(state,owned,owned)&&Current(state).kind==Kind::None,"already-owned refresh produces no notice");
    Check(Validate(state,owned),"completed introduction and collection remain valid");
}
void FullDeckAndWorld() {
    State state;auto before=Empty(),owned=Empty();for(unsigned id=0;id<FfxHooks::Arcana::kStandardCardCount;++id)owned[id]=1;
    Check(ObserveAwards(state,before,owned)&&state.count==78,"full-deck grant queues all 78 identities");
    Check(Acknowledge(state,Current(state),Navigation::Next),"first full-deck notice acknowledged");
    CompleteIntroduction(state);
    std::array<unsigned,78> seen{};seen[0]=1;
    unsigned last=255;
    while(Current(state).kind==Kind::Notice){
        const auto view=Current(state);++seen[view.card];last=view.card;
        Check(Acknowledge(state,view,Navigation::Next),"each full-deck notice can be acknowledged");
    }
    for(auto count:seen)Check(count==1,"each acquired identity is announced exactly once");
    Check(last==21,"World notice follows all cards that completed its collection");
    Check(Validate(state,owned),"full-deck presentation validates after draining");
    State legacy;before=owned;before[21]=0;before[77]=0;
    Check(AdoptLegacy(legacy,before),"legacy partial collection is introduced");
    CompleteIntroduction(legacy);
    Check(ObserveAwards(legacy,before,owned),"final ordinary card and World are recorded together");
    Check(Current(legacy).card==77,"final ordinary card appears before World regardless of numeric ID");
    Acknowledge(legacy,Current(legacy),Navigation::Next);
    Check(Current(legacy).card==21,"World follows the final ordinary card");
}
void LegacyResumeAndValidation() {
    State state;auto owned=Empty();owned[7]=1;
    Check(AdoptLegacy(state,owned),"old collection migrates without inventing an acquisition");
    auto view=Current(state);
    Check(view.kind==Kind::Introduction&&view.legacy&&view.card==7,"old save receives the honest introduction variant");
    Acknowledge(state,view,Navigation::Next);
    State restored=state;
    Check(Current(restored).page==1,"persisted page resumes after interruption");
    Check(Acknowledge(restored,Current(restored),Navigation::Previous)&&Current(restored).page==0,"back returns to the previous page");
    Check(!Acknowledge(restored,Current(restored),Navigation::Previous),"back on first page never skips the introduction");
    State other;
    Check(Current(other).kind==Kind::None,"another save does not inherit an introduction");
    auto bad=restored;bad.count=79;Check(!Validate(bad,owned),"oversized queue is rejected");
    bad=restored;bad.intro=static_cast<Intro>(99);Check(!Validate(bad,owned),"unknown persisted intro phase is rejected");
    bad=restored;bad.page=kGuidePageCount;Check(!Validate(bad,owned),"out-of-range guide page is rejected");
    bad=restored;bad.count=2;bad.pending[0]=7;bad.pending[1]=7;Check(!Validate(bad,owned),"duplicate pending receipts are rejected");
    bad=restored;bad.count=1;bad.pending[0]=8;Check(!Validate(bad,owned),"unowned card cannot have a success notice");
    auto after=owned;after[7]=0;
    Check(!ObserveAwards(restored,owned,after),"ownership rollback must use load restoration, not an award event");
    Check(restored.page==0&&restored.count==0,"rejected award observation leaves presentation unchanged");
}
void FreshInput() {
    InputLatch input;
    Check(input.Poll(true,true,true,false)==Navigation::None,"held confirm on opening does not acknowledge");
    Check(input.Poll(true,true,false,false)==Navigation::None,"release only arms the next action");
    Check(input.Poll(true,true,true,false)==Navigation::Next,"fresh confirm advances once");
    Check(input.Poll(true,true,true,false)==Navigation::None,"held confirm does not advance twice");
    Check(input.Poll(false,true,false,false)==Navigation::None,"focus loss suppresses input");
    Check(input.Poll(true,true,true,false)==Navigation::None,"focus return cannot reuse a held input");
    input.Poll(true,true,false,false);
    Check(input.Poll(true,true,false,true)==Navigation::Previous,"fresh back navigates backward");
    input.Poll(true,false,false,false);
    Check(input.Poll(true,true,true,false)==Navigation::None,"foreign UI prevents arming across its ownership");
}
void SkipIntroduction() {
    State state;auto owned=Empty();owned[36]=1;
    Check(AdoptLegacy(state,owned),"existing cards can start the optional introduction");
    const auto skip=static_cast<Navigation>(3);
    const auto first=Current(state);
    Check(Acknowledge(state,first,skip),"Escape skips the entire current introduction");
    Check(Validate(state,owned)&&state.intro==Intro::Complete&&Current(state).kind==Kind::None,"skipping is a valid persisted completion without changing cards");
    Check(!Acknowledge(state,first,skip),"a stale Escape cannot acknowledge twice");
    State withQueue;auto before=Empty();Check(ObserveAwards(withQueue,before,owned),"new card receipt is queued");
    Check(!Acknowledge(withQueue,Current(withQueue),skip)&&withQueue.count==1,"Escape skip cannot consume a card receipt");
    Acknowledge(withQueue,Current(withQueue),Navigation::Next);
    before=owned;owned[0]=1;ObserveAwards(withQueue,before,owned);
    Check(Acknowledge(withQueue,Current(withQueue),skip)&&Current(withQueue).kind==Kind::Notice&&Current(withQueue).card==0,
          "skipping the guide preserves later queued card notices");
    Check(Guide(0)&&Guide(kGuidePageCount-1),"the guide remains available for manual review after skipping");
}
}
int main(){
    FirstAndQueue();FullDeckAndWorld();LegacyResumeAndValidation();FreshInput();SkipIntroduction();
    std::printf("ArcanaPresentationRt0: %u/%u checks passed\n",checks-failures,checks);
    return failures?1:0;
}
#endif
