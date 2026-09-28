#include "../hooks/ArcanaUiCore.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
int main(){
    State state;AwardAll(state,0);Ui::View v;
    Ui::Observe(v,state,0x1000,1,0,true);
    Check(v.page==Ui::Page::Root,"valid native Equip context opens root state");
    Check(Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::NativeWeapon,"Weapon stays in original native path");
    Ui::Input(v,state,Ui::Key::Down);
    Check(Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::NativeArmor,"Armor stays in original native path");
    Ui::Input(v,state,Ui::Key::Down);
    Check(Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::None&&v.page==Ui::Page::Picker&&v.slot==0,"Tarot opens its picker without creating native gear index 2");
    Ui::Input(v,state,Ui::Key::Down);
    Ui::Input(v,state,Ui::Key::Down);
    Check(Ui::Preview(v,state)==0,"picker preview follows the selected card");
    auto command=Ui::Input(v,state,Ui::Key::Confirm);
    Check(command.action==Ui::Action::Equip&&command.actor==0&&command.slot==0&&command.card==0&&command.generation==1,"picker submits a revision-bound Arcana command");
    const auto equipped=Equip(state,command.revision,command.actor,command.slot,command.card,command.transfer);Ui::Result(v,state,equipped);
    Check(v.page==Ui::Page::Root&&Ui::Preview(v,state)==0,"successful equipment returns to the same Tarot root row");
    Ui::Observe(v,state,0x1000,1,1,true);Ui::Input(v,state,Ui::Key::Confirm);Ui::Input(v,state,Ui::Key::Down);Ui::Input(v,state,Ui::Key::Down);
    Check(Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::None&&v.page==Ui::Page::Transfer,"another actor's card requires a transfer dialog");
    Ui::Input(v,state,Ui::Key::Cancel);
    Check(v.page==Ui::Page::Picker&&Owner(state,0)==0,"canceling transfer preserves both owners");
    Ui::Input(v,state,Ui::Key::Confirm);command=Ui::Input(v,state,Ui::Key::Confirm);
    Check(command.action==Ui::Action::Equip&&command.transfer&&command.actor==1,"transfer confirmation targets the current actor only");
    ++state.revision;
    Check(Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::None&&v.error==Error::Stale,"a stale confirmation cannot mutate newer state");
    Ui::Observe(v,state,0x2000,2,2,true);
    Check(v.page==Ui::Page::Root&&v.pending==kEmpty&&v.actor==2,"new menu generation discards old transfer intent");
    Ui::Input(v,state,Ui::Key::Confirm);Ui::Input(v,state,Ui::Key::Cancel);
    Check(v.page==Ui::Page::Root,"picker Back returns to Equip root");
    Check(Ui::Input(v,state,Ui::Key::Cancel).action==Ui::Action::NativeBack,"root Back delegates to native menu exit");
    Ui::Observe(v,state,0x2000,2,2,false);
    Check(v.page==Ui::Page::Closed,"leaving the native Equip context releases custom input");
    Ui::Observe(v,state,0x3000,3,7,true);
    Check(v.page==Ui::Page::Closed,"unsupported guest actor cannot acquire a permanent-party identity");
    state.mode=Mode::Constellation;Ui::Observe(v,state,0x3000,4,2,true);
    Ui::Input(v,state,Ui::Key::Up);
    Check(v.category==4,"Constellation root includes a third Tarot slot");
    Ui::Input(v,state,Ui::Key::Confirm);Ui::Input(v,state,Ui::Key::Down);
    Check(v.cursor==Ui::kModeRow&&Ui::Input(v,state,Ui::Key::Confirm).action==Ui::Action::None&&v.page==Ui::Page::ModeReview,"mode changes have an explicit review screen");
    command=Ui::Input(v,state,Ui::Key::Confirm);
    Check(command.action==Ui::Action::Mode&&command.mode==Mode::Twin,"mode review emits an explicit whole-roster transition");
    Check(Ui::LayoutY(0x4cf6c3,520,Mode::Twin)==640&&Ui::LayoutY(0x4cf6c3,520,Mode::Constellation)==700,"native equipment panels move below all Tarot rows");
    Check(Ui::LayoutY(0x4d02f6,599,Mode::Constellation)==779&&Ui::LayoutY(0x4d0357,79,Mode::Constellation)==52,"five native ability rows retain enough pitch after the menu expands");
    Check(Ui::LayoutY(0x111111,520,Mode::Constellation)==520&&Ui::LayoutY(0x4cf699,60,Mode::Constellation)==60,"unknown callers and header heights keep native geometry");
    Check(Ui::WorkshopLabelY(915,Mode::Constellation)==987,"Workshop fifth ability aligns with the resized native rows");
    Check(Ui::LayoutY(0x4d031b,60,Mode::Constellation)==48&&Ui::LayoutY(0x4f4fc8,10,Mode::Constellation)==6,"compact row backgrounds contain the native text inset");
    Check(Ui::LayoutY(0x4f4f54,46,Mode::Constellation)==38&&Ui::LayoutY(0x4f4f78,7,Mode::Constellation)==4,"native ability icons fit inside compact row backgrounds");
    State menu;AwardAll(menu,0);Ui::View mode;
    Ui::Observe(mode,menu,0x4000,5,0,true);mode.category=2;
    Ui::Input(mode,menu,Ui::Key::Confirm);Ui::Input(mode,menu,Ui::Key::Down);
    Check(Ui::Preview(mode,menu)==kEmpty&&Ui::Input(mode,menu,Ui::Key::Confirm).action==Ui::Action::None&&mode.page==Ui::Page::ModeReview,"mode selector is immediately after Unequip and never previews a card");
    menu.mode=Mode::Constellation;Equip(menu,menu.revision,0,0,0);Equip(menu,menu.revision,0,1,1);
    Ui::View locked;Ui::Observe(locked,menu,0x5000,6,0,true);locked.category=4;
    const auto revision=menu.revision;
    Check(Ui::Input(locked,menu,Ui::Key::Confirm).action==Ui::Action::None&&locked.page!=Ui::Page::Picker&&locked.error==Error::Capacity,"two Major Arcana block the empty third slot before the picker opens");
    Ui::Input(locked,menu,Ui::Key::Confirm);
    Check(locked.page==Ui::Page::Root&&locked.category==4&&menu.revision==revision,"acknowledging a locked slot returns to it without changing equipment");
    Equip(menu,menu.revision,0,1,kEmpty);Ui::Input(locked,menu,Ui::Key::Confirm);
    Check(locked.page==Ui::Page::Picker,"freeing Major capacity immediately unlocks the empty slot");
    Equip(menu,menu.revision,0,1,1);Ui::Observe(locked,menu,0x5000,7,0,true);locked.category=2;
    Ui::Input(locked,menu,Ui::Key::Confirm);
    Check(locked.page==Ui::Page::Picker,"occupied Major slots remain editable at full capacity");
    Check(Ui::LayoutY(0x4cf766,520,Mode::Twin)==640&&Ui::LayoutY(0x4cf7b5,532,Mode::Constellation)==712,"Abilities panel and caption use the real ScaleY return addresses");
    Check(Ui::LayoutY(0x4cf761,520,Mode::Twin)==520&&Ui::LayoutY(0x4cf7b0,532,Mode::Constellation)==532,"instruction addresses are not mistaken for return addresses");
    Check(Ui::PickerCard(0)==kEmpty&&Ui::PickerCard(1)==kEmpty&&Ui::PickerCard(2)==0&&Ui::PickerCard(79)==77&&Ui::PickerCard(80)==kEmpty,"top-level picker actions never alias card identities and the complete deck remains reachable");
    Check(Ui::SlotLocked(menu,0,2)&&!Ui::SlotLocked(menu,0,0)&&!Ui::SlotLocked(menu,7,2),"lock rendering targets only an empty slot at full Major capacity");
    Check(Ui::LayoutX(0x4f4f66,38,Mode::Constellation)==32&&Ui::LayoutX(0x4f4f66,38,Mode::Twin)==38&&Ui::LayoutX(1,38,Mode::Constellation)==38,"only the compact native ability icon changes width");
    State status;AwardAll(status,0);Equip(status,status.revision,0,0,6);Equip(status,status.revision,0,1,13);
    const auto unchanged=status;
    auto rows=Ui::BuildStatusRows(status,0);
    auto has=[&](const char* text){for(unsigned i=0;i<rows.count;++i)if(!std::strcmp(rows.rows[i].text.data(),text))return true;return false;};
    Check(has("Deathstrike 100%")&&has("Deathproof")&&has("Damage vs Death immunity +20%"),"Status exposes Death's offensive, defensive and conditional effects");
    Check(has("Share healing 25% (cap 10% HP/action)")&&has("Kill: restore 20% HP once/action")&&has("Kill: restore 10% MP once/action"),"Status explains conditional effects and combines their parameter-only kinds");
    Check(status.revision==unchanged.revision&&status.slots==unchanged.slots&&Ui::BuildStatusRows(status,1).count==0&&Ui::BuildStatusRows(status,7).count==0,"Status is read-only, actor-bound and omits empty/invalid actors");
    Equip(status,status.revision,0,0,3);Equip(status,status.revision,0,1,21);rows=Ui::BuildStatusRows(status,0);
    unsigned hpRows=0;for(unsigned i=0;i<rows.count;++i)if(rows.rows[i].kind==EffectKind::HpPercent)++hpRows;
    Check(hpRows==1&&has("Max HP +90%")&&has("Overdrive HP damage +50%"),"Status combines shared card effects using the runtime aggregation rules");
    status.mode=Mode::Constellation;
    bool covered=true,fits=true;unsigned largest=0;
    for(unsigned a=0;a<kCardCount;++a)for(unsigned b=a;b<=kCardCount;++b)for(unsigned c=b;c<=kCardCount;++c){
        if((b==a&&b!=kCardCount)||(c==b&&c!=kCardCount))continue;
        status.slots[0]={static_cast<std::int16_t>(a),b==kCardCount?kEmpty:static_cast<std::int16_t>(b),c==kCardCount?kEmpty:static_cast<std::int16_t>(c)};
        if(Validate(status)!=Error::None)continue;
        rows=Ui::BuildStatusRows(status,0);const auto effects=Aggregate(status,0);unsigned expected=0;
        for(unsigned kind=1;kind<static_cast<unsigned>(EffectKind::Count);++kind)
            if(effects.Get(static_cast<EffectKind>(kind))&&static_cast<EffectKind>(kind)!=EffectKind::LoversCapHp&&static_cast<EffectKind>(kind)!=EffectKind::SurviveHeal)++expected;
        covered&=rows.count==expected&&rows.count>0;largest=(std::max)(largest,rows.count);
        for(unsigned i=0;i<rows.count;++i)covered&=std::strlen(rows.rows[i].text.data())>0&&std::strlen(rows.rows[i].text.data())<=44;
        for(unsigned native:{8u,9u,10u}){
            const auto g=Ui::StatusLayout(native,rows.count);
            fits&=g.top>=g.header+48.f&&g.height>=24.f&&g.pitch>=g.height+4.f&&g.top+float((rows.count+1)/2)*g.pitch<=1030.01f;
        }
    }
    Check(covered&&largest>=21,"every legal card loadout has complete bounded Status labels, including the longest combinations");
    Check(fits,"every legal loadout fits below vanilla and fifth-slot equipment rows with readable row separation");
    Check(Ui::StatusLayout(22,24).pitch==0&&Ui::StatusLayout(8,25).pitch==0,"unknown native capacities and oversized private lists are rejected");
    State audio;AwardAll(audio,0);Ui::View soundView;Ui::Observe(soundView,audio,0x6000,8,0,true);
    auto soundStep=[&](Ui::Key key){const auto before=soundView;const auto action=Ui::Input(soundView,audio,key);return Ui::Feedback(before,soundView,key,action);};
    Check(soundStep(Ui::Key::None)==Ui::Sound::None&&soundStep(Ui::Key::Down)==Ui::Sound::MoveConfirm,"idle is silent and a consumed root movement has one native move cue");
    Check(soundStep(Ui::Key::Confirm)==Ui::Sound::None,"delegated Armor confirmation keeps native sound ownership");
    soundView.category=2;Check(soundStep(Ui::Key::Confirm)==Ui::Sound::MoveConfirm,"opening a Tarot picker confirms once");
    Check(soundStep(Ui::Key::PageDown)==Ui::Sound::MoveConfirm&&soundStep(Ui::Key::Cancel)==Ui::Sound::Cancel,"page movement and returning from the picker have native feedback");
    audio.mode=Mode::Constellation;Equip(audio,audio.revision,0,0,0);Equip(audio,audio.revision,0,1,1);soundView.category=4;
    Check(soundStep(Ui::Key::Confirm)==Ui::Sound::Error&&soundView.page==Ui::Page::SlotBlocked,"confirming a capacity-locked slot produces the native error cue");
    Check(soundStep(Ui::Key::Down)==Ui::Sound::None&&soundStep(Ui::Key::Confirm)==Ui::Sound::MoveConfirm,"blocked-dialog idle navigation is silent and acknowledgement confirms");
    soundView.category=2;soundStep(Ui::Key::Confirm);soundView.cursor=Ui::kFirstCardRow+2;audio.acquired[2]=0;
    Check(soundStep(Ui::Key::Confirm)==Ui::Sound::Error&&soundStep(Ui::Key::Confirm)==Ui::Sound::Error,"each rejected selection gives an error even when its previous error is unchanged");
    soundView.cursor=Ui::kModeRow;++audio.revision;
    Check(soundStep(Ui::Key::Confirm)==Ui::Sound::Error&&soundStep(Ui::Key::Confirm)==Ui::Sound::MoveConfirm,"a stale intent errors, then a valid retry clears the stale cue when opening mode review");
    const auto beforeCommit=soundView;auto modeCommand=Ui::Input(soundView,audio,Ui::Key::Confirm);
    Ui::Result(soundView,audio,Error::Capacity);
    Check(Ui::Feedback(beforeCommit,soundView,Ui::Key::Confirm,modeCommand,Error::Capacity)==Ui::Sound::Error,"a rejected transaction never plays a success cue");
    Ui::Result(soundView,audio,Error::None);
    Check(Ui::Feedback(beforeCommit,soundView,Ui::Key::Confirm,modeCommand)==Ui::Sound::MoveConfirm,"a committed transaction confirms exactly once");
    auto overviewSlots=Ui::BuildEquippedSlots(audio,0);
    Check(overviewSlots.count==3&&!std::strcmp(overviewSlots.slots[0].text.data(),"Tarot I: The Fool")&&overviewSlots.slots[2].locked&&!std::strcmp(overviewSlots.slots[2].text.data(),"Tarot III: Locked"),"Status overview names equipped cards and explains the blocked third slot in one row");
    Equip(audio,audio.revision,0,1,77);overviewSlots=Ui::BuildEquippedSlots(audio,0);
    Check(!std::strcmp(overviewSlots.slots[1].text.data(),"Tarot II: King of Pentacles")&&!overviewSlots.slots[2].locked,"court names retain their full traditional name and freeing capacity updates the overview");
    audio.mode=Mode::Twin;overviewSlots=Ui::BuildEquippedSlots(audio,0);
    Check(overviewSlots.count==2&&Ui::BuildEquippedSlots(audio,7).count==0,"overview follows deck mode and excludes invalid actors");
    std::printf("ArcanaUiRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
