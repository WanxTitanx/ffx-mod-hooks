#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace FfxHooks::NativeBindings {
enum Modifier : unsigned { Control=1, Shift=2, Alt=4 };
enum class Action : unsigned { MenuF7, MenuF8, Performance, Borderless, FreeLook, TimeStop, SpeedCycle, Sensor, PartyAP, NativeTurbo, Supercharge, EncounterRate, AutoBattle, HideHud, GameMenu, RefreshWindow, Workshop, Count, None=255 };
struct Binding { unsigned key=0, modifiers=0; };
using Table=std::array<Binding,static_cast<unsigned>(Action::Count)>;
enum class BindResult { Ok, Invalid, Reserved, Protected, Duplicate };
inline Table Defaults() noexcept {
    Table table{};table[0]={0x76,0};table[1]={0x77,0};
    table[static_cast<unsigned>(Action::SpeedCycle)]={'K',Control|Shift};return table;
}
inline const char* Name(Action action) noexcept {
    switch(action) {
    case Action::MenuF7:return "Open F7 menu";
    case Action::MenuF8:return "Open F8 settings";
    case Action::Performance:return "Performance display";
    case Action::Borderless:return "Borderless window";
    case Action::FreeLook:return "Free camera";
    case Action::TimeStop:return "Freeze field scene";
    case Action::SpeedCycle:return "Cycle SpeedHack";
    case Action::Sensor:return "Permanent Sensor";
    case Action::PartyAP:return "Entire party AP";
    case Action::NativeTurbo:return "Game turbo (F1)";
    case Action::Supercharge:return "Supercharge (F2)";
    case Action::EncounterRate:return "Encounter rate (F3)";
    case Action::AutoBattle:return "Auto-battle (F4)";
    case Action::HideHud:return "Hide game HUD (F5)";
    case Action::GameMenu:return "Open game menu";
    case Action::RefreshWindow:return "Refresh game window";
    case Action::Workshop:return "Open Equipment Workshop";
    default:return "Unknown action";
    }
}
inline const char* Key(Action action) noexcept {
    switch(action) {
    case Action::MenuF7:return "bindings.menu_f7";
    case Action::MenuF8:return "bindings.menu_f8";
    case Action::Performance:return "bindings.performance";
    case Action::Borderless:return "bindings.borderless";
    case Action::FreeLook:return "bindings.free_camera";
    case Action::TimeStop:return "bindings.freeze_scene";
    case Action::SpeedCycle:return "bindings.speed_cycle";
    case Action::Sensor:return "bindings.sensor";
    case Action::PartyAP:return "bindings.party_ap";
    case Action::NativeTurbo:return "bindings.native_turbo";
    case Action::Supercharge:return "bindings.supercharge";
    case Action::EncounterRate:return "bindings.encounters";
    case Action::AutoBattle:return "bindings.auto_battle";
    case Action::HideHud:return "bindings.hide_hud";
    case Action::GameMenu:return "bindings.game_menu";
    case Action::RefreshWindow:return "bindings.refresh_window";
    case Action::Workshop:return "bindings.workshop";
    default:return nullptr;
    }
}
inline BindResult Validate(const Table& table,Action action,Binding binding) noexcept {
    const auto index=static_cast<unsigned>(action);
    if(index>=table.size() || binding.key>255 || binding.modifiers>7 || (!binding.key && binding.modifiers))
        return BindResult::Invalid;
    if(!binding.key)return BindResult::Ok;
    if(binding.key>=0x70u && binding.key<=0x74u)return BindResult::Reserved;
    if((action==Action::MenuF7 || action==Action::MenuF8 || action==Action::Workshop) &&
       (binding.key==0x0Du || binding.key==0x20u || binding.key==0x09u || (binding.key>=0x25u&&binding.key<=0x28u)))return BindResult::Reserved;
    if(action!=Action::SpeedCycle && binding.key=='K' && binding.modifiers==(Control|Shift))return BindResult::Protected;
    if(binding.key<0x08u || binding.key==0x1Bu || binding.key==0x5Bu || binding.key==0x5Cu ||
       binding.key==0x10u || binding.key==0x11u || binding.key==0x12u ||
       (binding.key>=0xA0u && binding.key<=0xA5u) ||
       ((binding.modifiers&Alt) && (binding.key==0x73u || binding.key==0x09u)) ||
       ((binding.modifiers&(Control|Alt))==(Control|Alt) && (binding.key==0x2Eu || binding.key==0x78u || binding.key==0x79u)) ||
       ((binding.key==0x2Du || binding.key==0x78u) && binding.modifiers==0))return BindResult::Reserved;
    for(unsigned i=0;i<table.size();++i)
        if(i!=index && table[i].key==binding.key && table[i].modifiers==binding.modifiers)
            return BindResult::Duplicate;
    return BindResult::Ok;
}
inline Action Resolve(const Table& table,unsigned key,unsigned modifiers) noexcept {
    if(!key)return Action::None;
    for(unsigned i=0;i<table.size();++i)
        if(table[i].key==key && table[i].modifiers==modifiers)return static_cast<Action>(i);
    return Action::None;
}
inline int Encode(Binding value) noexcept {return static_cast<int>(value.key | (value.modifiers<<8u));}
inline bool Decode(int encoded,Binding* output) noexcept {
    if(!output || encoded<0 || encoded>0x7FF)return false;
    *output={static_cast<unsigned>(encoded)&255u,static_cast<unsigned>(encoded)>>8u};
    return output->key!=0 || output->modifiers==0;
}
inline bool Format(Binding binding,char* output,std::size_t capacity) noexcept {
    if(!output || !capacity)return false;
    char key[16]{};
    if(!binding.key)std::snprintf(key,sizeof(key),"Unassigned");
    else if(binding.key>=0x70u && binding.key<=0x87u)std::snprintf(key,sizeof(key),"F%u",binding.key-0x6Fu);
    else if((binding.key>='A' && binding.key<='Z') || (binding.key>='0' && binding.key<='9'))
        std::snprintf(key,sizeof(key),"%c",static_cast<char>(binding.key));
    else if(binding.key==0x2Du)std::snprintf(key,sizeof(key),"Insert");
    else std::snprintf(key,sizeof(key),"Key %u",binding.key);
    const int n=std::snprintf(output,capacity,"%s%s%s%s",(binding.modifiers&Control)?"Ctrl+":"",
        (binding.modifiers&Shift)?"Shift+":"",(binding.modifiers&Alt)?"Alt+":"",key);
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
} // namespace FfxHooks::NativeBindings
