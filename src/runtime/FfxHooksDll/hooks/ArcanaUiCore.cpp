#include "../shared/ExecutableProfile.h"
#include "ArcanaUiCore.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace FfxHooks::Arcana::Ui {
float LayoutY(std::uint32_t caller,float value,Mode mode) noexcept {
    const float extra=mode==Mode::Twin?120.f:180.f;
    switch(caller){
    case (::FfxHooks::ExecutableProfile::Rva<0x4cf6c3>()):case (::FfxHooks::ExecutableProfile::Rva<0x4cf766>()):case (::FfxHooks::ExecutableProfile::Rva<0x4cf712>()):case (::FfxHooks::ExecutableProfile::Rva<0x4cf7b5>()):
    case (::FfxHooks::ExecutableProfile::Rva<0x4d02f6>()):case (::FfxHooks::ExecutableProfile::Rva<0x4d0390>()):return value+extra;
    case (::FfxHooks::ExecutableProfile::Rva<0x4d031b>()):return mode==Mode::Twin?58.f:48.f;
    case (::FfxHooks::ExecutableProfile::Rva<0x4d0357>()):case (::FfxHooks::ExecutableProfile::Rva<0x4d03df>()):return mode==Mode::Twin?64.f:52.f;
    case (::FfxHooks::ExecutableProfile::Rva<0x4f4f54>()):return mode==Mode::Constellation?38.f:value;
    case (::FfxHooks::ExecutableProfile::Rva<0x4f4f78>()):return mode==Mode::Constellation?4.f:value;
    case (::FfxHooks::ExecutableProfile::Rva<0x4f4fc8>()):return mode==Mode::Constellation?6.f:value;
    default:return value;
    }
}
float WorkshopLabelY(float value,Mode mode) noexcept {
    if(value<599.f)return value;
    const float extra=mode==Mode::Twin?120.f:180.f,spacing=mode==Mode::Twin?64.f:52.f;
    return 599.f+extra+(value-599.f)*(spacing/79.f);
}
float LayoutX(std::uint32_t caller,float value,Mode mode) noexcept {
    return caller==(::FfxHooks::ExecutableProfile::Rva<0x4f4f66>())&&mode==Mode::Constellation?32.f:value;
}
namespace {
const char* StatusFormat(EffectKind kind,int value) noexcept {
    switch(kind){
    case EffectKind::HpPercent:return "Max HP %+d%%";
    case EffectKind::MpPercent:return "Max MP %+d%%";
    case EffectKind::StrengthPercent:return "Strength %+d%%";
    case EffectKind::MagicPercent:return "Magic %+d%%";
    case EffectKind::DefensePercent:return "Defense %+d%%";
    case EffectKind::MagicDefensePercent:return "Magic Defense %+d%%";
    case EffectKind::AccuracyFlat:return "Accuracy %+d";
    case EffectKind::EvasionFlat:return "Evasion %+d";
    case EffectKind::LuckFlat:return "Luck %+d";
    case EffectKind::Sensor:return "Sensor";
    case EffectKind::Piercing:return "Piercing";
    case EffectKind::AutoHaste:return "Auto-Haste";
    case EffectKind::AutoProtect:return "Auto-Protect";
    case EffectKind::AutoShell:return "Auto-Shell";
    case EffectKind::AutoRegen:return "Auto-Regen";
    case EffectKind::AutoReflect:return "Auto-Reflect";
    case EffectKind::SosRegen:return "SOS Regen";
    case EffectKind::FirstStrike:return "First Strike";
    case EffectKind::Counter:return "Counterattack";
    case EffectKind::MagicCounter:return "Magic Counter";
    case EffectKind::EvadeCounter:return "Evade and Counter";
    case EffectKind::MasterThief:return "Master Thief";
    case EffectKind::AutoPotion:return "Auto-Potion";
    case EffectKind::AutoPhoenix:return "Auto-Phoenix";
    case EffectKind::BreakHp:return "Break HP Limit";
    case EffectKind::BreakDamage:return "Break Damage Limit";
    case EffectKind::ProofDark:return "Darkproof";
    case EffectKind::ProofSilence:return "Silenceproof";
    case EffectKind::ProofSleep:return "Sleepproof";
    case EffectKind::ProofPoison:return "Poisonproof";
    case EffectKind::ProofConfuse:return "Confuseproof";
    case EffectKind::ProofStone:return "Stoneproof";
    case EffectKind::ProofDeath:return "Deathproof";
    case EffectKind::ProofSlow:return "Slowproof";
    case EffectKind::StrikeFire:return "Firestrike";
    case EffectKind::StrikeIce:return "Icestrike";
    case EffectKind::StrikeLightning:return "Lightningstrike";
    case EffectKind::StrikeWater:return "Waterstrike";
    case EffectKind::StrikeHoly:return "Holystrike";
    case EffectKind::StrikeShadow:return "Shadowstrike";
    case EffectKind::StrikeEarth:return "Earthstrike";
    case EffectKind::StrikeWind:return "Aerostrike";
    case EffectKind::StrikeBio:return "Biostrike (Poison; Elemental Core)";
    case EffectKind::StrikeGravity:return "Gravitystrike (Elemental Core)";
    case EffectKind::WardFire:return "Fire Ward";
    case EffectKind::WardIce:return "Ice Ward";
    case EffectKind::WardLightning:return "Lightning Ward";
    case EffectKind::WardWater:return "Water Ward";
    case EffectKind::WardHoly:return "Holy Ward";
    case EffectKind::WardShadow:return "Shadow Ward";
    case EffectKind::WardEarth:return "Earth Ward";
    case EffectKind::WardWind:return "Wind Ward";
    case EffectKind::WardBio:return "Poison Ward (element; Elemental Core)";
    case EffectKind::WardGravity:return "Gravity Ward (Elemental Core)";
    case EffectKind::TouchDark:return value>=100?"Darkstrike %d%%":"Darktouch %d%%";
    case EffectKind::TouchSilence:return value>=100?"Silencestrike %d%%":"Silencetouch %d%%";
    case EffectKind::TouchSleep:return value>=100?"Sleepstrike %d%%":"Sleeptouch %d%%";
    case EffectKind::TouchSlow:return value>=100?"Slowstrike %d%%":"Slowtouch %d%%";
    case EffectKind::TouchPoison:return value>=100?"Poisonstrike %d%% (3 turns)":"Poisontouch %d%% (3 turns)";
    case EffectKind::TouchDeath:return value>=100?"Deathstrike %d%%":"Deathtouch %d%%";
    case EffectKind::TouchArmorBreak:return "Armor Break touch %d%% (3 turns)";
    case EffectKind::TouchMentalBreak:return "Mental Break touch %d%% (3 turns)";
    case EffectKind::TouchStone:return value>=100?"Stonestrike %d%%":"Stonetouch %d%%";
    case EffectKind::TouchConfuse:return value>=100?"Confusestrike %d%%":"Confusetouch %d%%";
    case EffectKind::HalfMp:return "Half MP Cost";
    case EffectKind::HalfBlackMp:return "Half MP: Black Magic";
    case EffectKind::HalfWhiteMp:return "Half MP: White Magic";
    case EffectKind::MpReduction:return "MP cost -%d%%";
    case EffectKind::WhiteMpReduction:return "White Magic MP cost -%d%%";
    case EffectKind::WhiteHealing:return "White Magic healing %+d%%";
    case EffectKind::Healing:return "Healing %+d%%";
    case EffectKind::ItemHealing:return "Item healing %+d%%";
    case EffectKind::FirstCtbReduction:return "First CTB recovery -%d%%";
    case EffectKind::CtbIncrease:return "CTB recovery +%d%%";
    case EffectKind::CtbReduction:return "CTB recovery -%d%%";
    case EffectKind::FocusOnStart:return "Opening Focus x%d";
    case EffectKind::MpPerTurn:return "Restore %d%% MP each turn";
    case EffectKind::DefendMp:return "Defend: restore %d%% MP";
    case EffectKind::KillHp:return "Kill: restore %d%% HP once/action";
    case EffectKind::KillMp:return "Kill: restore %d%% MP once/action";
    case EffectKind::OutgoingDamage:return "HP damage dealt %+d%%";
    case EffectKind::IncomingDamage:return "HP damage taken %+d%%";
    case EffectKind::ElementDamageFireIce:return "Fire/Ice damage %+d%%";
    case EffectKind::ElementDamageLightningWater:return "Lightning/Water damage %+d%%";
    case EffectKind::ElementDamageHoly:return "Holy damage %+d%%";
    case EffectKind::OverdriveDamage:return "Overdrive HP damage %+d%%";
    case EffectKind::DeathImmuneDamage:return "Damage vs Death immunity %+d%%";
    case EffectKind::CriticalChance:return "Critical chance %+d%%";
    case EffectKind::GilBonus:return "Gil %+d%%";
    case EffectKind::ApBonus:return "AP %+d%%";
    case EffectKind::DropMultiplier:return "Item drops x%d";
    case EffectKind::EncounterReduction:return "Encounter rate -%d%%";
    case EffectKind::OpeningOverdrive:return "Opening Overdrive +%d";
    default:return nullptr;
    }
}
}
StatusRows BuildStatusRows(const State& state,unsigned actor) noexcept {
    StatusRows result;const auto effects=Aggregate(state,actor);
    for(unsigned i=1;i<static_cast<unsigned>(EffectKind::Count);++i){
        const auto kind=static_cast<EffectKind>(i);const int value=effects.Get(kind);
        if(!value||kind==EffectKind::LoversCapHp||kind==EffectKind::SurviveHeal)continue;
        // A legal loadout contains at most three cards with eight effects each.
        // These private rows never extend the native 22-DWORD ability buffer.
        if(result.count==result.rows.size())return {};
        auto& row=result.rows[result.count];row.kind=kind;
        if(kind==EffectKind::LoversHealing)
            std::snprintf(row.text.data(),row.text.size(),"Share healing %d%% (cap %d%% HP/action)",value,effects.Get(EffectKind::LoversCapHp));
        else if(kind==EffectKind::SurviveOnce)
            std::snprintf(row.text.data(),row.text.size(),"Survive once/battle: %d%% HP",effects.Get(EffectKind::SurviveHeal));
        else if(const auto* format=StatusFormat(kind,value))std::snprintf(row.text.data(),row.text.size(),format,value);
        else continue;
        ++result.count;
    }
    return result;
}
StatusGeometry StatusLayout(unsigned nativeCapacity,unsigned count) noexcept {
    if(nativeCapacity<8||nativeCapacity>10||!count||count>24)return {};
    StatusGeometry result;
    result.header=316.f+float((nativeCapacity+1)/2-1)*63.f+64.f;
    result.top=result.header+54.f;
    result.pitch=(std::min)(44.f,(1030.f-result.top)/float((count+1)/2));
    result.height=result.pitch-4.f;
    return result;
}
EquippedSlots BuildEquippedSlots(const State& state,unsigned actor) noexcept {
    EquippedSlots result;
    if(actor>=kActorCount||Validate(state)!=Error::None)return result;
    result.count=state.mode==Mode::Twin?2:3;
    constexpr const char* numerals[]={"I","II","III"};
    for(unsigned i=0;i<result.count;++i){
        auto& slot=result.slots[i];slot.card=state.slots[actor][i];slot.locked=SlotLocked(state,actor,i);
        const auto* card=slot.card==kEmpty?nullptr:FindCard(static_cast<unsigned>(slot.card));
        const char* name=slot.locked?"Locked":card?card->name:"Empty";
        auto length=std::strlen(name);
        if(card){
            // Retain the traditional name while omitting the longer Spira
            // subtitle; pip cards have a numeral prefix, court cards do not.
            const auto* first=std::strstr(name," - ");
            const auto* last=first?std::strstr(first+3," - "):nullptr;
            if(last){name=first+3;length=static_cast<std::size_t>(last-name);}
            else if(first)length=static_cast<std::size_t>(first-name);
        }
        std::snprintf(slot.text.data(),slot.text.size(),"Tarot %s: %.*s",numerals[i],static_cast<int>(length),name);
    }
    return result;
}
std::int16_t PickerCard(unsigned row) noexcept {
    return row>=kFirstCardRow&&row<kPickerRows?static_cast<std::int16_t>(row-kFirstCardRow):kEmpty;
}
bool SlotLocked(const State& state,unsigned actor,unsigned slot) noexcept {
    if(state.mode!=Mode::Constellation||actor>=kActorCount||slot>=kMaximumSlots||state.slots[actor][slot]!=kEmpty)return false;
    unsigned majors=0;
    for(auto id:state.slots[actor])if(id!=kEmpty){const auto* card=FindCard(static_cast<unsigned>(id));if(card&&card->major)++majors;}
    return majors>=2;
}
namespace {
unsigned RootRows(const State& state) noexcept {return state.mode==Mode::Twin?4:5;}
Command Make(const View& view,Action action) noexcept {
    Command command;command.action=action;command.revision=view.revision;command.generation=view.generation;
    command.actor=view.actor;command.slot=view.slot;command.card=view.pending;return command;
}
void Scroll(View& view) noexcept {
    if(view.cursor<view.top)view.top=view.cursor;
    if(view.cursor>=view.top+kVisibleRows)view.top=view.cursor-kVisibleRows+1;
}
}
void Observe(View& view,const State& state,std::uintptr_t context,std::uint64_t generation,unsigned actor,bool idle) noexcept {
    if(!idle||!context||!generation||actor>=kActorCount||Validate(state)!=Error::None){view={};return;}
    if(view.page==Page::Closed||view.context!=context||view.generation!=generation||view.actor!=actor){
        const auto category=(std::min)(view.category,RootRows(state)-1);
        view={};view.page=Page::Root;view.context=context;view.generation=generation;view.actor=actor;
        view.category=category;view.revision=state.revision;
    }
    view.category=(std::min)(view.category,RootRows(state)-1);
    if(view.slot>=RootRows(state)-2&&view.page!=Page::Root){view.page=Page::Root;view.pending=kEmpty;}
    if(view.page==Page::SlotBlocked&&!SlotLocked(state,view.actor,view.slot)){view.page=Page::Root;view.error=Error::None;}
}
Command Input(View& view,const State& state,Key key) noexcept {
    if(view.page==Page::Closed||view.actor>=kActorCount||Validate(state)!=Error::None)return {};
    if(key==Key::None)return {};
    if(key==Key::Cancel){
        if(view.page==Page::Root)return Make(view,Action::NativeBack);
        view.page=view.page==Page::Transfer?Page::Picker:Page::Root;
        view.pending=kEmpty;view.error=Error::None;view.revision=state.revision;return {};
    }
    if(view.page==Page::SlotBlocked){
        if(key==Key::Confirm){view.page=Page::Root;view.error=Error::None;view.revision=state.revision;}
        return {};
    }
    if(view.page==Page::Root){
        if(key==Key::Up)view.category=(view.category+RootRows(state)-1)%RootRows(state);
        if(key==Key::Down)view.category=(view.category+1)%RootRows(state);
        if(key!=Key::Confirm)return {};
        if(view.category<2)return Make(view,view.category==0?Action::NativeWeapon:Action::NativeArmor);
        view.slot=view.category-2;view.revision=state.revision;view.error=Error::None;
        if(SlotLocked(state,view.actor,view.slot)){view.page=Page::SlotBlocked;view.error=Error::Capacity;return {};}
        view.page=Page::Picker;
        const auto card=state.slots[view.actor][view.slot];
        view.cursor=card==kEmpty?kUnequipRow:static_cast<unsigned>(card)+kFirstCardRow;view.top=0;Scroll(view);return {};
    }
    if(key==Key::Confirm&&view.revision!=state.revision){
        view.error=Error::Stale;view.page=Page::Picker;view.pending=kEmpty;view.revision=state.revision;return {};
    }
    if(key==Key::Confirm)view.error=Error::None;
    if(view.page==Page::Transfer){
        if(key==Key::Confirm){auto command=Make(view,Action::Equip);command.transfer=true;return command;}
        return {};
    }
    if(view.page==Page::ModeReview){
        if(key==Key::Confirm){auto command=Make(view,Action::Mode);command.mode=view.proposed;return command;}
        return {};
    }
    if(view.page!=Page::Picker)return {};
    if(key==Key::Up)view.cursor=(view.cursor+kPickerRows-1)%kPickerRows;
    if(key==Key::Down)view.cursor=(view.cursor+1)%kPickerRows;
    if(key==Key::PageUp)view.cursor=(view.cursor+kPickerRows-kVisibleRows)%kPickerRows;
    if(key==Key::PageDown)view.cursor=(view.cursor+kVisibleRows)%kPickerRows;
    Scroll(view);
    if(key!=Key::Confirm){view.revision=state.revision;view.error=Error::None;return {};}
    if(view.cursor==kModeRow){
        view.page=Page::ModeReview;view.proposed=state.mode==Mode::Twin?Mode::Constellation:Mode::Twin;return {};
    }
    view.pending=PickerCard(view.cursor);
    if(view.pending!=kEmpty&&!state.acquired[static_cast<unsigned>(view.pending)]){view.error=Error::NotAcquired;return {};}
    if(view.pending!=kEmpty&&Owner(state,static_cast<unsigned>(view.pending))>=0&&state.slots[view.actor][view.slot]!=view.pending){
        view.page=Page::Transfer;return {};
    }
    return Make(view,Action::Equip);
}
void Result(View& view,const State& state,Error error) noexcept {
    view.error=error;view.pending=kEmpty;view.revision=state.revision;
    if(error==Error::None){view.page=Page::Root;view.category=(std::min)(view.category,RootRows(state)-1);}
    else view.page=Page::Picker;
}
Sound Feedback(const View& before,const View& after,Key key,const Command& command,Error result) noexcept {
    if(key==Key::None||before.page==Page::Closed||command.action==Action::NativeWeapon||
       command.action==Action::NativeArmor||command.action==Action::NativeBack)return Sound::None;
    if(key==Key::Confirm){
        if(result!=Error::None||after.error!=Error::None)return Sound::Error;
        if(before.page!=after.page||command.action==Action::Equip||command.action==Action::Mode)return Sound::MoveConfirm;
        return Sound::None;
    }
    if(key==Key::Cancel)return before.page!=after.page?Sound::Cancel:Sound::None;
    return before.category!=after.category||before.cursor!=after.cursor||before.top!=after.top?Sound::MoveConfirm:Sound::None;
}
std::int16_t Preview(const View& view,const State& state) noexcept {
    if(view.actor>=kActorCount)return kEmpty;
    if(view.page==Page::Transfer)return view.pending;
    if(view.page==Page::Picker)return PickerCard(view.cursor);
    if(view.page==Page::Root&&view.category>=2&&view.category<RootRows(state))return state.slots[view.actor][view.category-2];
    return kEmpty;
}
}
