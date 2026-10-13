#include <cstdio>
#if !__has_include("../hooks/ArcanaWorldCore.h")
int main(){std::puts("FAIL: interactive Tarot collection is missing");return 1;}
#else
#include "../hooks/ArcanaWorldCore.h"
using namespace FfxHooks::Arcana;
namespace {unsigned checks=0,failures=0;void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}}
int main(){
    using namespace Acquisition;
    State state;Progress progress{};Facts facts;
    Check(Catalog().size()==78,"all 78 card identities have one authored collection site");
    for(unsigned i=0;i<78;++i){const auto& site=Catalog()[i];Check(site.card==i&&site.location.room&&site.stage<25&&site.hint&&*site.hint,"catalog preserves stable IDs and complete player-facing requirements");}
    const auto& fool=Catalog()[0];
    Check(Eligible(state,progress,facts,0),"first Tarot is available at its physical Besaid site");
    Check(!Eligible(state,progress,facts,25),"Valefor gate is enforced before the first temple");
    facts.aeons|=1u<<8;Check(Eligible(state,progress,facts,25),"obtaining Valefor unlocks its collection site");
    Check(state.acquired[25]==0,"unlocking a milestone never grants a card");
    Target target;target.card=0;target.location=fool.location;
    auto result=Interact(state,progress,facts,target,fool.location.room,fool.location.x+300,fool.location.z,false);
    Check(result.kind==ResultKind::None&&!state.acquired[0],"interaction outside the symbol radius cannot award");
    result=Interact(state,progress,facts,target,fool.location.room,fool.location.x,fool.location.z,false);
    Check(result.kind==ResultKind::Awarded&&state.acquired[0],"a physical confirmed symbol interaction awards exactly one card");
    Check(Interact(state,progress,facts,target,fool.location.room,fool.location.x,fool.location.z,false).kind==ResultKind::None,"a collected symbol cannot award twice");
    facts.story=2999;facts.mirror=true;facts.omega=true;facts.aeons=0x3ffffu;
    Check(!Eligible(state,progress,facts,2)&&!Eligible(state,progress,facts,7),"first airship never substitutes for free destination travel");
    facts.freeTravel=true;facts.story=3205;
    Check(!Eligible(state,progress,facts,7),"Magus and Omega alone do not complete the new three-post route");
    progress[ChocoboPosts]=15;
    Check(!Eligible(state,progress,facts,7)&&!Eligible(state,progress,facts,61),"all three posts still require returning to the trainer");
    progress[ChocoboPosts]=31;
    Check(Eligible(state,progress,facts,7)&&Eligible(state,progress,facts,61),"the completed route and return unlock both rider cards");
    facts.omega=false;Check(!Eligible(state,progress,facts,7),"route receipts do not bypass Omega");
    facts.omega=true;facts.darkAeons=0x7f;Check(!Eligible(state,progress,facts,20),"seven completed Dark encounters cannot unlock Judgement");
    facts.darkAeons=0xff;Check(Eligible(state,progress,facts,20),"eight completed Dark encounters unlock their collection site");
    Check(!Eligible(state,progress,facts,21),"World waits for all other 77 cards");
    for(unsigned i=0;i<78;++i)if(i!=21)Award(state,i);
    Check(Eligible(state,progress,facts,21)&&!state.acquired[21],"World still requires a physical collection after 77 cards");
    auto bad=progress;bad[ChocoboPosts]=16;Check(!ValidProgress(bad),"return without start and posts is rejected");
    State pilgrimage;Progress complete{3,3,3,4,7,7,31,3,3,3,3,3};facts.wobbly=true;
    for(unsigned card=0;card<78;++card){
        if(card==21)continue;
        const auto& site=Catalog()[card];const auto options=Discover(pilgrimage,complete,facts,site.location.room);
        bool found=false;Target actual;
        for(unsigned i=0;i<options.count;++i)if(options.values[i].kind==TargetKind::Card&&options.values[i].card==card){actual=options.values[i];found=true;}
        const auto acquired=found?Interact(pilgrimage,complete,facts,actual,site.location.room,site.location.x,site.location.z,false):Result{};
        Check(found&&acquired.kind==ResultKind::Awarded&&pilgrimage.acquired[card],"every ordinary catalog symbol has a reachable single-card collection transaction");
    }
    const auto& world=Catalog()[21];Target finalCard;finalCard.card=21;finalCard.location=world.location;
    Check(Interact(pilgrimage,complete,facts,finalCard,world.location.room,world.location.x,world.location.z,false).kind==ResultKind::Awarded&&pilgrimage.acquired[21],"complete 78-card journey ends with the explicit World collection");
    State route;Progress routeProgress{};
    const auto& trainer=Catalog()[7];auto options=Discover(route,routeProgress,facts,trainer.location.room);
    Target start;bool started=false;
    for(unsigned i=0;i<options.count;++i)if(options.values[i].kind==TargetKind::RouteStart){start=options.values[i];started=true;}
    Check(started&&Interact(route,routeProgress,facts,start,start.location.room,start.location.x,start.location.z,false).kind==ResultKind::Progressed&&routeProgress[ChocoboPosts]==8,"new route requires an explicit start at the trainer");
    options=Discover(route,routeProgress,facts,223);
    for(unsigned i=0;i<options.count;++i)if(options.values[i].kind==TargetKind::RoutePost){
        const auto post=options.values[i];const auto before=routeProgress;
        Check(Interact(route,routeProgress,facts,post,223,post.location.x,post.location.z,false).kind==ResultKind::None&&routeProgress==before,"walking to a mounted route post does not record it");
        Check(Interact(route,routeProgress,facts,post,223,post.location.x,post.location.z,true).kind==ResultKind::Progressed,"mounted interaction records each distinct post");
        Check(Interact(route,routeProgress,facts,post,223,post.location.x,post.location.z,true).kind==ResultKind::None,"a recorded route post cannot advance the route twice");
    }
    Check(routeProgress[ChocoboPosts]==15&&!route.acquired[7]&&!route.acquired[61],"the route completes before its two separate collection receipts");
    options=Discover(route,routeProgress,facts,trainer.location.room);bool returned=false;
    for(unsigned i=0;i<options.count;++i)if(options.values[i].kind==TargetKind::RouteFinish){
        const auto finish=options.values[i];returned=Interact(route,routeProgress,facts,finish,finish.location.room,finish.location.x,finish.location.z,false).kind==ResultKind::Progressed;
    }
    Check(returned&&routeProgress[ChocoboPosts]==31,"only an interaction back at the trainer completes reconnaissance");
    State reader;Progress reading{};const auto& record=Catalog()[5];Target book;book.card=5;book.location=record.location;
    for(unsigned page=0;page<3;++page)Check(Interact(reader,reading,facts,book,record.location.room,record.location.x,record.location.z,false).kind==ResultKind::Progressed&&!reader.acquired[5],"every temple record is presented before its card is claimed");
    Check(Interact(reader,reading,facts,book,record.location.room,record.location.x,record.location.z,false).kind==ResultKind::Awarded,"completed local records lead to one card receipt");
    const auto deckRoom=Catalog()[21].location.room;
    for(unsigned missing:{18u,35u,49u,64u}){
        State stranded;Progress recoveryProgress{};
        for(unsigned card=0;card<78;++card)if(card!=21&&card!=missing)Award(stranded,card);
        const unsigned steps=missing==49?2u:3u;
        for(unsigned step=0;step<steps;++step){
            auto choices=Discover(stranded,recoveryProgress,facts,deckRoom);bool offered=false;Result recovered;
            for(unsigned i=0;i<choices.count;++i)if(choices.values[i].card==missing&&choices.values[i].location.room==deckRoom){
                const auto entry=choices.values[i];offered=true;recovered=Interact(stranded,recoveryProgress,facts,entry,deckRoom,entry.location.x,entry.location.z,false);break;
            }
            Check(offered&&recovered.kind==ResultKind::Progressed&&!stranded.acquired[missing],"deck recovery offers equivalent unfinished regional objectives without a new Dark Aeon requirement");
        }
        const auto choices=Discover(stranded,recoveryProgress,facts,deckRoom);bool claimed=false;
        for(unsigned i=0;i<choices.count;++i)if(choices.values[i].card==missing){const auto entry=choices.values[i];claimed=Interact(stranded,recoveryProgress,facts,entry,deckRoom,entry.location.x,entry.location.z,false).kind==ResultKind::Awarded;break;}
        Check(claimed&&stranded.acquired[missing],"completed recovery objective gives the same single card receipt");
    }
    for(unsigned retained:{7u,61u}){
        State migrated;Progress unfinished{};Award(migrated,retained);
        const unsigned missing=retained==7?61u:7u;
        auto choices=Discover(migrated,unfinished,facts,trainer.location.room);Target begin;bool offered=false;
        for(unsigned i=0;i<choices.count;++i)if(choices.values[i].kind==TargetKind::RouteStart){begin=choices.values[i];offered=true;}
        Check(offered&&begin.card==missing&&Interact(migrated,unfinished,facts,begin,begin.location.room,begin.location.x,begin.location.z,false).kind==ResultKind::Progressed,"legacy ownership of either rider card cannot disable the shared route for its missing partner");
        choices=Discover(migrated,unfinished,facts,223);
        for(unsigned i=0;i<choices.count;++i)if(choices.values[i].kind==TargetKind::RoutePost){const auto post=choices.values[i];Interact(migrated,unfinished,facts,post,223,post.location.x,post.location.z,true);}
        choices=Discover(migrated,unfinished,facts,deckRoom);bool finished=false;
        for(unsigned i=0;i<choices.count;++i)if(choices.values[i].kind==TargetKind::RouteFinish){const auto finish=choices.values[i];finished=Interact(migrated,unfinished,facts,finish,deckRoom,finish.location.x,finish.location.z,false).kind==ResultKind::Progressed;break;}
        // Earlier unclaimed cards may occupy the recovery display. The local
        // trainer must independently offer the same completed route.
        if(!finished){choices=Discover(migrated,unfinished,facts,trainer.location.room);for(unsigned i=0;i<choices.count;++i)if(choices.values[i].kind==TargetKind::RouteFinish){const auto finish=choices.values[i];finished=Interact(migrated,unfinished,facts,finish,finish.location.room,finish.location.x,finish.location.z,false).kind==ResultKind::Progressed;break;}}
        const auto& destination=Catalog()[missing];Target claim;claim.card=static_cast<std::uint8_t>(missing);claim.location=destination.location;
        Check(finished&&Interact(migrated,unfinished,facts,claim,destination.location.room,destination.location.x,destination.location.z,false).kind==ResultKind::Awarded&&migrated.acquired[retained]&&migrated.acquired[missing],"migrated partial rider pair completes with only the missing card newly awarded");
    }
    std::printf("ArcanaWorldRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#endif
