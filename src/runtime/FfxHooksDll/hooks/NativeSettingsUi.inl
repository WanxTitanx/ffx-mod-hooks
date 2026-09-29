// Included by the existing native F8 shell after its row and pointer helpers.
// The same menu object owns these choice pages; no second overlay is created.
#include "EquipmentWorkshopSettings.h"
#include "ElementScanSettings.h"
#include "ElementNameInput.h"
#include "ElementHook.h"
#include "SeymourOverdriveHook.h"
#include "SeymourGearPresentationHook.h"
#include "SeymourGearSortHook.h"
#include "SeymourPersistentRosterHook.h"
#include "SeymourMenuListHook.h"
#include "SphereGridProgress8Runtime.h"
#include "F8FlagCatalog.h"
#include "VanguardCatalog.h"
#include "ModFeatureCatalog.h"
#include "MonsterRewardsRuntime.h"
#include "VanguardUiBridge.h"
#include "TextLanguageSettings.h"
#include "TextLanguageHook.h"
#include "TextLanguageCore.h"
static const FfxHooks::F8FlagSpec* F8NativeScanFlag(int row){
    const char* keys[]={"labs.scan_expanded","labs.element_scan_dark"};
    return row>=0&&row<2?FfxHooks::FindF8Flag(keys[row]):nullptr;
}
static bool F8NativeScanOwnsFlag(const FfxHooks::F8FlagSpec& flag){
    return &flag==F8NativeScanFlag(0)||&flag==F8NativeScanFlag(1);
}
enum class NativeSettingsPage { None, Keyboard, Gamepad, Controller, ControllerPort, Mapping, Destination, KeyCapture, PadCapture, Languages, LanguageChoice, WorkshopRefinement, Workshop, WorkshopExpansion, ElementScan, ElementColor, ElementBit, ElementVisibility,
    ArenaOptions, Vanguard, VanguardDamage, VanguardMagic, VanguardStatus, VanguardFormation,
    VanguardWeapons, VanguardArmor, VanguardEquipment, VanguardMapping, Arena, VanguardMappingEdit, VanguardCommandBindings, VanguardCommandEdit, TextLanguages, AdditionalMods, FieldScout,
    RewardMultipliers,MonsterRewardList,MonsterRewardDetail,RewardRate,MonsterRewardId,
    ElementNames,ElementNameEdit,ElementNameCapture, PhotoMode, Seymour };
static bool F8RewardPage(NativeSettingsPage page){return page>=NativeSettingsPage::RewardMultipliers&&page<=NativeSettingsPage::MonsterRewardId;}
static int F8RewardCount(NativeSettingsPage page);
static const char* const kF8ArenaKeys[]={"arena_plus.master","arena_plus.compose_f7","arena_plus.unlock_all",
    "arena_plus.victory_hook","arena_plus.resolver_log","arena_plus.music"};
static const char* const kF8FieldScoutKeys[]={"field_scout.master","field_scout.heavy","field_scout.max","field_scout.ultra"};
static bool F8NativeNestedFlag(const FfxHooks::F8FlagSpec& flag){
    for(const auto* key:{"cheats.ap_100x","cheats.gil_100x","cheats.monster_rewards"})if(std::strcmp(key,flag.gate.canonicalKey)==0)return true;
    if(std::strncmp(flag.gate.canonicalKey,"vanguard.",9)==0)return true;
    for(const auto& entry:FfxHooks::ModFeatures::Entries)if(std::strcmp(entry.gate.canonicalKey,flag.gate.canonicalKey)==0)return true;
    for(const auto* key:kF8FieldScoutKeys)if(std::strcmp(key,flag.gate.canonicalKey)==0)return true;
    for(const auto* key:kF8ArenaKeys)if(std::strcmp(key,flag.gate.canonicalKey)==0)return true;
    return false;
}
static bool F8NativeVanguardGroup(NativeSettingsPage page){
    return page>=NativeSettingsPage::VanguardDamage&&page<=NativeSettingsPage::VanguardEquipment;
}
static const FfxHooks::F8FlagSpec* F8NativeNestedSpec(NativeSettingsPage page,int row){
    if(row<0)return nullptr;
    if(page==NativeSettingsPage::ArenaOptions)return row<6?FfxHooks::FindF8Flag(kF8ArenaKeys[row]):nullptr;
    if(page==NativeSettingsPage::AdditionalMods)return row<static_cast<int>(FfxHooks::ModFeatures::Entries.size())?FfxHooks::FindF8Flag(FfxHooks::ModFeatures::Entries[row].gate.canonicalKey):nullptr;
    if(page==NativeSettingsPage::FieldScout)return row<4?FfxHooks::FindF8Flag(kF8FieldScoutKeys[row]):nullptr;
    if(!F8NativeVanguardGroup(page))return nullptr;
    const auto group=static_cast<FfxHooks::Vanguard::Group>(static_cast<int>(page)-static_cast<int>(NativeSettingsPage::VanguardDamage));
    for(const auto& spec:FfxHooks::Vanguard::Features)if(spec.group==group&&row--==0){
        char key[96]{};_snprintf_s(key,sizeof(key),_TRUNCATE,"vanguard.%s",spec.key);
        return FfxHooks::FindF8Flag(key);
    }
    return nullptr;
}
struct NativeSettingsFrame {NativeSettingsPage page=NativeSettingsPage::None;int selected=0,top=0;};
static NativeSettingsFrame g_nativeSettingsFrames[4]{};
static int g_nativeSettingsDepth=0,g_nativeSettingsParentRow=0,g_nativeSettingsParentTop=0;
static int g_nativeSettingsAction=0,g_nativeSettingsMapFrom=0,g_nativeSettingsLastEdge=0;
static int g_nativeSettingsLanguage=0;
static const char* const g_nativeLanguageKeys[]={"language.voice","language.sfx","language.video"};
static char g_nativeSettingsNotice[128]{};
static unsigned g_vanguardMappingEffect=0,g_vanguardMappingId=135;
static std::uint64_t g_vanguardMappingStamp=0;
static unsigned g_vanguardBindingEffect=0,g_vanguardBindingCommand=0x3000,g_vanguardBindingCost=256;
static std::uint64_t g_vanguardBindingStamp=0;
static unsigned g_nativeElementIndex=0;
static char g_nativeHookElementKey[65]{},g_nativeHookElementTitle[80]{};
static unsigned g_nativeElementNameBit=0,g_nativeElementNameCharacter=1;
static char g_nativeElementNameKey[65]{},g_nativeElementNameDraft[65]{};
static constexpr char g_nativeElementNameAlphabet[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'()./";
static void F8NativeScanElementName(unsigned index,char* out,size_t capacity){
    using namespace FfxHooks;
    const auto bit=Config::ReadIntExact(ElementScan::BitKey,32,64);
    if(index>=2&&(bit.state==Config::IntReadState::Invalid||(bit.state==Config::IntReadState::Valid&&bit.value!=32&&bit.value!=64))){strncpy_s(out,capacity,"Custom (invalid order)",_TRUNCATE);return;}
    const unsigned primary=bit.state==Config::IntReadState::Valid?static_cast<unsigned>(bit.value):32u;
    const unsigned nativeBit=index==0?0x10u:index==1?0x80u:index==2?primary:0x60u^primary;
    const auto catalog=ElementMenu::Read();strncpy_s(out,capacity,catalog[ElementMenu::NativeIndex(nativeBit)].label,_TRUNCATE);
}
static FfxHooks::ElementScan::Hsv g_nativeElementHsv{};
static std::uint32_t g_nativeElementRgb=0;
static bool g_nativeElementDirty=false;
static int g_nativeElementRepeat=0;
static void F8NativeElementAdjust(int row,int delta){
    if(row<0||row>2)return;
    if(row==0)g_nativeElementHsv.h=static_cast<unsigned>((static_cast<int>(g_nativeElementHsv.h)+360+delta)%360);
    else {auto& value=row==1?g_nativeElementHsv.s:g_nativeElementHsv.v;value=static_cast<unsigned>((std::max)(0,(std::min)(100,static_cast<int>(value)+delta)));}
    g_nativeElementDirty=true;
}
static bool F8NativeSettingsActive(){return g_nativeSettingsDepth>0;}
static NativeSettingsPage F8NativeSettingsPage(){return g_nativeSettingsDepth?g_nativeSettingsFrames[g_nativeSettingsDepth-1].page:NativeSettingsPage::None;}
static int F8NativeSettingsCount(NativeSettingsPage page){
    if(F8RewardPage(page))return F8RewardCount(page);
    if(F8NativeVanguardGroup(page)){int rows=page==NativeSettingsPage::VanguardEquipment?2:1;
        const auto group=static_cast<FfxHooks::Vanguard::Group>(static_cast<int>(page)-static_cast<int>(NativeSettingsPage::VanguardDamage));
        for(const auto& spec:FfxHooks::Vanguard::Features){if(spec.group==group)++rows;}
        return rows;
    }
    switch(page){
    case NativeSettingsPage::Arena:return 2;
    case NativeSettingsPage::ArenaOptions:return 7;
    case NativeSettingsPage::AdditionalMods:return static_cast<int>(FfxHooks::ModFeatures::Entries.size())+1;
    case NativeSettingsPage::FieldScout:return 5;
    case NativeSettingsPage::Vanguard:return 9;
    case NativeSettingsPage::VanguardMapping:return static_cast<int>(FfxHooks::Vanguard::AbilityCount)+1;
    case NativeSettingsPage::VanguardMappingEdit:return 4;
    case NativeSettingsPage::VanguardCommandBindings:return 14;
    case NativeSettingsPage::VanguardCommandEdit:return 8;
    case NativeSettingsPage::Keyboard:case NativeSettingsPage::Gamepad:return static_cast<int>(FfxHooks::NativeBindings::Action::Count)+1;
    case NativeSettingsPage::Controller:return 4;
    case NativeSettingsPage::ControllerPort:return 6;
    case NativeSettingsPage::Mapping:return 12;
    case NativeSettingsPage::Destination:return 11;
    case NativeSettingsPage::KeyCapture:case NativeSettingsPage::PadCapture:return 2;
    case NativeSettingsPage::Languages:case NativeSettingsPage::LanguageChoice:return 4;
    case NativeSettingsPage::WorkshopRefinement:return 3;
    case NativeSettingsPage::Workshop:return 6;
    case NativeSettingsPage::WorkshopExpansion:return 3;
    case NativeSettingsPage::ElementScan:return 12;
    case NativeSettingsPage::ElementNames:return 7;
    case NativeSettingsPage::ElementNameEdit:return 8;
    case NativeSettingsPage::ElementNameCapture:return 3;
    case NativeSettingsPage::ElementColor:return 5;
    case NativeSettingsPage::ElementBit:return 3;
    case NativeSettingsPage::ElementVisibility:return 7;
    case NativeSettingsPage::TextLanguages:return 4;
    case NativeSettingsPage::PhotoMode:return PhotoMode::MenuCount()+1;
    case NativeSettingsPage::Seymour:return FfxHooks::SeymourCompatibility::MenuCount()+13;
    default:return 0;
    }
}
static float F8NativeSettingsHeight(){return 0.285f+0.052f*static_cast<float>((std::min)(9,F8NativeSettingsCount(F8NativeSettingsPage())));}
static float F8NativeSettingsTop(){return (1.0f-F8NativeSettingsHeight())*0.5f;}
static void F8NativeSettingsSetGeometry(int obj){
    using namespace NativeMenu;
    auto& frame=g_nativeSettingsFrames[g_nativeSettingsDepth-1];
    WrW(obj,O_COUNT,static_cast<int16_t>(F8NativeSettingsCount(frame.page)));
    WrW(obj,O_PAGE,9);WrW(obj,O_SELECTED,static_cast<int16_t>(frame.selected));WrW(obj,O_TOP,static_cast<int16_t>(frame.top));
    F7SeedPointerForDestination();InterlockedExchange(&g_f7MouseWheelDelta,0);
    g_nativeSettingsLastEdge=PadEdge();g_f7ConfirmTimer=12;
}
static void F8NativeSettingsPush(int obj,NativeSettingsPage page){
    if(g_nativeSettingsDepth>=4)return;
    if(g_nativeSettingsDepth){auto& frame=g_nativeSettingsFrames[g_nativeSettingsDepth-1];frame.selected=NativeMenu::RdW(obj,NativeMenu::O_SELECTED);frame.top=NativeMenu::RdW(obj,NativeMenu::O_TOP);}
    else {g_nativeSettingsParentRow=NativeMenu::RdW(obj,NativeMenu::O_SELECTED);g_nativeSettingsParentTop=NativeMenu::RdW(obj,NativeMenu::O_TOP);}
    g_nativeSettingsFrames[g_nativeSettingsDepth++]={page,0,0};
    if(page==NativeSettingsPage::TextLanguages){
        const auto selection=FfxHooks::TextLanguage::Settings::Selection();
        g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=selection==1?1:0;
        if(selection<0)strncpy_s(g_nativeSettingsNotice,"Invalid text locale. Select an available language.",_TRUNCATE);
    }
    if(page==NativeSettingsPage::WorkshopRefinement){workshop::Policy policy{};
        if(FfxHooks::EquipmentWorkshop::Settings::Read(policy))g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=policy.mode==1?1:0;
        else strncpy_s(g_nativeSettingsNotice,"Invalid Workshop setting. Select a valid mode; check other INI costs.",_TRUNCATE);
    }
    if(page==NativeSettingsPage::WorkshopExpansion){unsigned recipe=0;
        if(FfxHooks::EquipmentWorkshop::Settings::ReadExpansion(recipe))g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=static_cast<int>(recipe-1);
    }
    if(page==NativeSettingsPage::ElementColor){
        const FfxHooks::ElementScan::Settings defaults{};
        if(g_nativeElementIndex<4){
            char name[65]{};F8NativeScanElementName(g_nativeElementIndex,name,sizeof(name));
            _snprintf_s(g_nativeHookElementTitle,sizeof(g_nativeHookElementTitle),_TRUNCATE,"%.64s color",name);
            const auto read=FfxHooks::Config::ReadIntExact(FfxHooks::ElementScan::ColorKeys[g_nativeElementIndex],0,0xFFFFFF);
            g_nativeElementRgb=read.state==FfxHooks::Config::IntReadState::Valid?static_cast<std::uint32_t>(read.value):defaults.rgb[g_nativeElementIndex];
        }else{const auto catalog=FfxHooks::ElementMenu::Read();const auto& item=catalog[8+g_nativeElementIndex-4];bool visible=false;
            g_nativeElementRgb=item.rgb;(void)FfxHooks::ElementScan::ReadHookPresentation(item,g_nativeElementRgb,visible);}
        g_nativeElementHsv=FfxHooks::ElementScan::ToHsv(g_nativeElementRgb);g_nativeElementDirty=false;g_nativeElementRepeat=0;
    }
    if(page==NativeSettingsPage::ElementBit){const auto read=FfxHooks::Config::ReadIntExact(FfxHooks::ElementScan::BitKey,32,64);
        g_nativeSettingsFrames[g_nativeSettingsDepth-1].selected=read.state==FfxHooks::Config::IntReadState::Valid&&read.value==64?1:0;}
    F8NativeSettingsSetGeometry(obj);
}
static void F8NativeSettingsReset(){g_nativeSettingsDepth=0;FfxHooks::NativePorts::CancelBindingCapture();FfxHooks::ElementNameInput::Abort();g_nativeSettingsNotice[0]=0;}
static void F8NativeSettingsPop(int obj){
    if(!g_nativeSettingsDepth)return;
    FfxHooks::NativePorts::CancelBindingCapture();FfxHooks::ElementNameInput::Abort();--g_nativeSettingsDepth;
    if(g_nativeSettingsDepth)F8NativeSettingsSetGeometry(obj);
    else {
        NativeMenu::WrW(obj,NativeMenu::O_COUNT,static_cast<int16_t>(g_f7RowCount));
        NativeMenu::WrW(obj,NativeMenu::O_PAGE,FfxHooks::F8Ui::Layout::VisibleRows);
        NativeMenu::WrW(obj,NativeMenu::O_SELECTED,static_cast<int16_t>(g_nativeSettingsParentRow));
        NativeMenu::WrW(obj,NativeMenu::O_TOP,static_cast<int16_t>(g_nativeSettingsParentTop));
        F7SeedPointerForDestination();g_f7ConfirmTimer=12;g_f7LastEdge=NativeMenu::PadEdge();
    }
}
#include "NativeRewardSettings.inl"
static const char* F8NativeSettingsTitle(NativeSettingsPage page){
    if(F8RewardPage(page))return F8RewardTitle(page);
    if(F8NativeVanguardGroup(page))return FfxHooks::Vanguard::GroupNames[static_cast<unsigned>(page)-static_cast<unsigned>(NativeSettingsPage::VanguardDamage)];
    switch(page){
    case NativeSettingsPage::Arena:return "Arena+";
    case NativeSettingsPage::ArenaOptions:return "Arena+ settings";
    case NativeSettingsPage::AdditionalMods:return "Additional mods";
    case NativeSettingsPage::FieldScout:return "FieldScout";
    case NativeSettingsPage::Vanguard:return "Vanguard Combat Engine";
    case NativeSettingsPage::VanguardMapping:return "Ability ID mapping";
    case NativeSettingsPage::VanguardMappingEdit:return "Edit ability ID";
    case NativeSettingsPage::VanguardCommandBindings:return "Equipment command bindings";
    case NativeSettingsPage::VanguardCommandEdit:return "Edit equipment command";
    case NativeSettingsPage::Keyboard:return "Keyboard shortcuts";
    case NativeSettingsPage::Gamepad:return "Gamepad shortcuts";
    case NativeSettingsPage::Controller:return "Gamepad settings";
    case NativeSettingsPage::ControllerPort:return "Choose a controller";
    case NativeSettingsPage::Mapping:return "Remap gamepad buttons";
    case NativeSettingsPage::Destination:return "Choose the game action button";
    case NativeSettingsPage::KeyCapture:return "Press a keyboard shortcut";
    case NativeSettingsPage::PadCapture:return "Press a gamepad combination";
    case NativeSettingsPage::Languages:return "Audio languages";
    case NativeSettingsPage::TextLanguages:return "Text languages";
    case NativeSettingsPage::LanguageChoice:return g_nativeSettingsLanguage==0?"Voice language":g_nativeSettingsLanguage==1?"Battle sound language":"Movie audio language";
    case NativeSettingsPage::WorkshopRefinement:return "Workshop refinement";
    case NativeSettingsPage::Workshop:return "Equipment Workshop";
    case NativeSettingsPage::WorkshopExpansion:return "Expansion recipe";
    case NativeSettingsPage::ElementScan:return "Scan settings";
    case NativeSettingsPage::ElementNames:return "Element names";
    case NativeSettingsPage::ElementNameEdit:return "Rename element";
    case NativeSettingsPage::ElementNameCapture:return "Type element name";
    case NativeSettingsPage::ElementColor:return g_nativeHookElementTitle;
    case NativeSettingsPage::ElementBit:return "Custom element order";
    case NativeSettingsPage::ElementVisibility:return "Enabled Scan elements";
    case NativeSettingsPage::PhotoMode:return "Battle Photo Mode";
    case NativeSettingsPage::Seymour:return "Seymour compatibility";
    default:return "Settings";
    }
}
static void F8NativeSettingsLabel(NativeSettingsPage page,int row,char* out,size_t capacity){
    using namespace FfxHooks;
    const int count=F8NativeSettingsCount(page);
    if(row<0||row>=count){strncpy_s(out,capacity,"Invalid selection",_TRUNCATE);return;}
    if(row==count-1){strncpy_s(out,capacity,page==NativeSettingsPage::RewardRate||page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture||page==NativeSettingsPage::ElementColor||page==NativeSettingsPage::VanguardMappingEdit||page==NativeSettingsPage::VanguardCommandEdit?"Cancel":"Back",_TRUNCATE);return;}
    if(F8RewardPage(page)){F8RewardLabel(page,row,out,capacity);return;}
    if(page==NativeSettingsPage::VanguardEquipment&&row==2){strncpy_s(out,capacity,"Command bindings",_TRUNCATE);return;}
    if(page==NativeSettingsPage::VanguardCommandBindings){
        Vanguard::BindingState bindings{};
        if(!Vanguard::ReadUiBindings(bindings)){strncpy_s(out,capacity,"Loaded command data unavailable",_TRUNCATE);return;}
        const auto& binding=bindings.entries[row];
        if(!binding.packed)_snprintf_s(out,capacity,_TRUNCATE,"%s: Disabled",Vanguard::Abilities[row].label);
        else if(binding.cost==256)_snprintf_s(out,capacity,_TRUNCATE,"%s: %04X | Native OD | %s",Vanguard::Abilities[row].label,binding.command,binding.code==Vanguard::BindingCode::Valid?"Valid":"Invalid");
        else _snprintf_s(out,capacity,_TRUNCATE,"%s: %04X | OD %u | %s",Vanguard::Abilities[row].label,binding.command,binding.cost,binding.code==Vanguard::BindingCode::Valid?"Valid":"Invalid");
        return;
    }
    if(page==NativeSettingsPage::VanguardCommandEdit){
        if(row<2)_snprintf_s(out,capacity,_TRUNCATE,"Command %04X: %s",g_vanguardBindingCommand,row==0?"Increase (+1)":"Decrease (-1)");
        else if(row<4){
            if(g_vanguardBindingCost==256)_snprintf_s(out,capacity,_TRUNCATE,"OD cost: Native | %s",row==2?"Increase":"Decrease");
            else _snprintf_s(out,capacity,_TRUNCATE,"OD cost %u: %s",g_vanguardBindingCost,row==2?"Increase (+1)":"Decrease (-1)");
        }else strncpy_s(out,capacity,row==4?"Use native Overdrive cost":row==5?"Validate and save binding":"Disable this binding",_TRUNCATE);
        return;
    }
    if(page==NativeSettingsPage::Arena){strncpy_s(out,capacity,"Options",_TRUNCATE);return;}
    if(page==NativeSettingsPage::Vanguard){strncpy_s(out,capacity,Vanguard::GroupNames[row],_TRUNCATE);return;}
    if(page==NativeSettingsPage::VanguardMapping){
        Vanguard::MappingState mapping{};Vanguard::ReadUiMapping(mapping);
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %u | %s",Vanguard::Abilities[row].label,
            mapping.ids[row],Vanguard::UiMappingDetail(mapping.codes[row]));return;
    }
    if(page==NativeSettingsPage::VanguardMappingEdit){
        if(row<2)_snprintf_s(out,capacity,_TRUNCATE,"ID %u: %s",g_vanguardMappingId,row==0?"Increase (+1)":"Decrease (-1)");
        else strncpy_s(out,capacity,"Validate and save ID",_TRUNCATE);
        return;
    }
    if(page==NativeSettingsPage::ArenaOptions||page==NativeSettingsPage::AdditionalMods||page==NativeSettingsPage::FieldScout||F8NativeVanguardGroup(page)){
        const auto* flag=F8NativeNestedSpec(page,row);
        if(!flag){strncpy_s(out,capacity,"Control unavailable",_TRUNCATE);return;}
        const auto runtime=GetF8RuntimeStatus(*flag);
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s | %s",flag->label,ResolveF8Flag(*flag).value?"ON":"OFF",
            flag->activation==F8Activation::NotWired?"NOT WIRED":
            runtime.hasAppliedValue?(runtime.appliedValue?"Running ON":"Running OFF"):
            flag->activation==F8Activation::RestartRequired?"Restart required":F8AvailabilityName(runtime.availability));return;
    }
    if(page==NativeSettingsPage::PhotoMode){PhotoMode::MenuLabel(row,out,capacity);return;}
    if(page==NativeSettingsPage::Seymour){
        if(row<3)FfxHooks::SeymourCompatibility::MenuLabel(row,out,capacity);
        else if(row==3)FfxHooks::SeymourOverdrive::MenuLabel(out,capacity);
        else if(row==4)FfxHooks::SeymourOverdrive::Detail(out,capacity);
        else if(row==5)FfxHooks::SeymourGearPresentation::MenuLabel(out,capacity);
        else if(row==6)FfxHooks::SeymourGearPresentation::Detail(out,capacity);
        else if(row==7)FfxHooks::SeymourGearSort::MenuLabel(out,capacity);
        else if(row==8)FfxHooks::SeymourGearSort::Detail(out,capacity);
        else if(row==9)FfxHooks::SeymourPersistentRoster::MenuLabel(out,capacity);
        else if(row==10)FfxHooks::SeymourPersistentRoster::Detail(out,capacity);
        else if(row==11)FfxHooks::SeymourMenuList::MenuLabel(out,capacity);
        else if(row==12)FfxHooks::SeymourMenuList::Detail(out,capacity);
        else if(row==13)FfxHooks::SphereGridProgress8Runtime::MenuLabel(out,capacity);
        else FfxHooks::SphereGridProgress8Runtime::Detail(out,capacity);
        return;
    }
    if(page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture){strncpy_s(out,capacity,"Clear this shortcut",_TRUNCATE);return;}
    if(page==NativeSettingsPage::ElementNames){
        const auto catalog=ElementMenu::Read();_snprintf_s(out,capacity,_TRUNCATE,"%.64s",catalog[4+row].label);return;
    }
    if(page==NativeSettingsPage::ElementNameEdit){
        if(row==0)_snprintf_s(out,capacity,_TRUNCATE,"Name: %.40s",g_nativeElementNameDraft);
        else if(row==1){const char ch=g_nativeElementNameAlphabet[g_nativeElementNameCharacter];
            if(ch==' ')strncpy_s(out,capacity,"Character: (space)",_TRUNCATE);
            else _snprintf_s(out,capacity,_TRUNCATE,"Character: %c",ch);}
        else {const char* labels[]={"Add character","Delete last character","Clear draft","Save name","Restore default name"};strncpy_s(out,capacity,labels[row-2],_TRUNCATE);}
        return;
    }
    if(page==NativeSettingsPage::ElementNameCapture){
        if(row==0){char value[65]{};ElementNameInput::Copy(value);_snprintf_s(out,capacity,_TRUNCATE,"Name: %.40s_",value);}
        else strncpy_s(out,capacity,"Use this name",_TRUNCATE);return;
    }
    if(page==NativeSettingsPage::ElementScan){
        if(row==10){strncpy_s(out,capacity,"Element names",_TRUNCATE);return;}
        if(row==8||row==9){
            const auto catalog=ElementMenu::Read();const auto& item=catalog[row];std::uint32_t rgb=0;bool visible=false;
            if(!item.available)_snprintf_s(out,capacity,_TRUNCATE,"%.32s: enable Core and restart",item.label);
            else if(!ElementScan::ReadHookPresentation(item,rgb,visible))_snprintf_s(out,capacity,_TRUNCATE,"%.48s color: INVALID",item.label);
            else _snprintf_s(out,capacity,_TRUNCATE,"%.48s color: #%06X",item.label,rgb);return;
        }
        if(row<2){
            const auto* flag=F8NativeScanFlag(row);
            if(!flag){strncpy_s(out,capacity,"Scan control unavailable",_TRUNCATE);return;}
            const bool running=row==0?IsScanExpandedInstalled():IsElementHookInstalled();
            _snprintf_s(out,capacity,_TRUNCATE,"%s: %s | Running: %s",flag->label,
                ResolveF8Flag(*flag).value?"ON":"OFF",running?"ON":"OFF");return;
        }
        row-=2;
        if(row==4){ElementScan::Settings settings{};
            if(ElementScan::ReadSettings(settings)){unsigned visibleCount=ElementScan::VisibleCount(settings);const auto catalog=ElementMenu::Read();
                for(unsigned i=8;i<10;++i){std::uint32_t rgb=0;bool visible=false;if(ElementScan::ReadHookPresentation(catalog[i],rgb,visible)&&visible)++visibleCount;}
                _snprintf_s(out,capacity,_TRUNCATE,"Enabled extra elements: %u / 6",visibleCount);}
            else strncpy_s(out,capacity,"Enabled extra elements: INVALID settings",_TRUNCATE);return;}
        // Keep the existing color, bit and visibility row positions for saved
        // navigation; append the complementary Custom color before Back.
        if(row==5)row=3;
        else if(row==3){const auto bit=Config::ReadIntExact(ElementScan::BitKey,32,64);const bool valid=bit.state==Config::IntReadState::Missing||(bit.state==Config::IntReadState::Valid&&(bit.value==32||bit.value==64));
            if(!valid)strncpy_s(out,capacity,"Custom order: INVALID",_TRUNCATE);
            else {char first[65]{},second[65]{};F8NativeScanElementName(2,first,sizeof(first));F8NativeScanElementName(3,second,sizeof(second));
                _snprintf_s(out,capacity,_TRUNCATE,"Custom order: %.24s / %.24s",first,second);}return;}
        if(row>=0&&row<4){char name[65]{};F8NativeScanElementName(static_cast<unsigned>(row),name,sizeof(name));const ElementScan::Settings defaults{};
            const auto read=Config::ReadIntExact(ElementScan::ColorKeys[row],0,0xFFFFFF);
            if(read.state==Config::IntReadState::Invalid)_snprintf_s(out,capacity,_TRUNCATE,"%.48s color: INVALID",name);
            else _snprintf_s(out,capacity,_TRUNCATE,"%.48s color: #%06X",name,read.state==Config::IntReadState::Valid?static_cast<unsigned>(read.value):defaults.rgb[row]);
        }return;
    }
    if(page==NativeSettingsPage::ElementColor){
        if(row==0)_snprintf_s(out,capacity,_TRUNCATE,"Hue: %u / 359",g_nativeElementHsv.h);
        else if(row==1)_snprintf_s(out,capacity,_TRUNCATE,"Saturation: %u%%",g_nativeElementHsv.s);
        else if(row==2)_snprintf_s(out,capacity,_TRUNCATE,"Brightness: %u%%",g_nativeElementHsv.v);
        else strncpy_s(out,capacity,"Save color",_TRUNCATE);return;
    }
    if(page==NativeSettingsPage::ElementBit){const auto catalog=ElementMenu::Read();const auto& item=catalog[6+row];
        _snprintf_s(out,capacity,_TRUNCATE,"%.40s - bit 0x%02X",item.label,item.nativeBit);return;}
    if(page==NativeSettingsPage::ElementVisibility){
        if(row>=4){const auto catalog=ElementMenu::Read();const auto& item=catalog[8+row-4];std::uint32_t rgb=0;bool visible=false;
            _snprintf_s(out,capacity,_TRUNCATE,"%.48s: %s",item.label,!item.available?"Core unavailable":
                !ElementScan::ReadHookPresentation(item,rgb,visible)?"INVALID":visible?"ON":"OFF");return;}
        if(row<0||row>=4)return;
        char name[65]{};F8NativeScanElementName(static_cast<unsigned>(row),name,sizeof(name));const ElementScan::Settings defaults{};
        const auto read=Config::ReadIntExact(ElementScan::EnabledKeys[row],0,1);
        const bool enabled=read.state==Config::IntReadState::Missing?defaults.enabled[row]!=0:read.value!=0;
        const char* value=read.state==Config::IntReadState::Invalid?"INVALID":enabled?"ON":"OFF";
        _snprintf_s(out,capacity,_TRUNCATE,"%.48s: %s",name,value);return;
    }
    if(page==NativeSettingsPage::Workshop){
        if(row==0){strncpy_s(out,capacity,"Refinement method: A / B",_TRUNCATE);return;}
        if(row==4){unsigned recipe=0;const bool valid=EquipmentWorkshop::Settings::ReadExpansion(recipe);
            _snprintf_s(out,capacity,_TRUNCATE,"Expansion recipe: %s",valid?(recipe==1?"A - one per slot":"B - 1/2/3/4"):"INVALID");return;}
        const char* labels[]={"Free materials","Free Gil","Ignore Customize progression"};
        if(row<1||row>3){strncpy_s(out,capacity,"Invalid option",_TRUNCATE);return;}
        const auto value=Config::ReadIntExact(EquipmentWorkshop::Settings::DevelopmentKeys[row-1],0,1);
        const char* state=value.state==Config::IntReadState::Invalid?"INVALID":value.state==Config::IntReadState::Valid&&value.value?"ON":"OFF";
        _snprintf_s(out,capacity,_TRUNCATE,"DEV - %s: %s",labels[row-1],state);return;
    }
    if(page==NativeSettingsPage::WorkshopExpansion){
        unsigned recipe=0;const bool valid=EquipmentWorkshop::Settings::ReadExpansion(recipe);
        _snprintf_s(out,capacity,_TRUNCATE,"%s%s",row==0?"A - one Key Sphere per added slot":"B - 1/2/3/4 Key Spheres by slot",valid&&recipe==static_cast<unsigned>(row+1)?" [Selected]":"");return;
    }
    if(page==NativeSettingsPage::WorkshopRefinement){
        workshop::Policy policy{};const bool valid=EquipmentWorkshop::Settings::Read(policy);const unsigned mode=row==0?2u:1u;
        _snprintf_s(out,capacity,_TRUNCATE,"%s%s%s",EquipmentWorkshop::Settings::ModeName(mode),mode==2?" (default)":"",valid&&policy.mode==mode?" [Selected]":"");return;
    }
    if(page==NativeSettingsPage::TextLanguages){
        if(row<2){const auto selected=TextLanguage::Settings::Selection();
            _snprintf_s(out,capacity,_TRUNCATE,"%s%s",row==0?"Original game language":TextLanguage::Settings::BrazilianName,
                selected==row?" [Selected]":"");
        }else{const auto runtime=TextLanguage::Native::Inspect();
            _snprintf_s(out,capacity,_TRUNCATE,"Runtime: %s | Text reads: %u",runtime.fontReady?"ON":"OFF",runtime.textOpens);}
        return;
    }
    if(page==NativeSettingsPage::Languages){
        const char* names[]={"Voices","Battle sounds","Movie audio"};
        const auto choice=static_cast<NativeLanguage::Choice>(Config::GetInt(g_nativeLanguageKeys[row],0));
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s",names[row],NativeLanguage::Valid(choice)?NativeLanguage::Name(choice):"Invalid setting");return;
    }
    if(page==NativeSettingsPage::LanguageChoice){strncpy_s(out,capacity,NativeLanguage::Name(static_cast<NativeLanguage::Choice>(row)),_TRUNCATE);return;}
    if(page==NativeSettingsPage::Keyboard || page==NativeSettingsPage::Gamepad){
        const auto action=static_cast<NativeBindings::Action>(row);
        const char* value=page==NativeSettingsPage::Keyboard?NativePorts::BindingText(action):NativePorts::GamepadBindingText(action);
        _snprintf_s(out,capacity,_TRUNCATE,"%s: %s",NativeBindings::Name(action),strlen(value)>28?"Configured combination":value);return;
    }
    if(page==NativeSettingsPage::Controller){
        if(row==0){const int port=Config::GetInt("gamepad.controller",-1);if(port<0)strncpy_s(out,capacity,"Controller: Automatic",_TRUNCATE);else _snprintf_s(out,capacity,_TRUNCATE,"Controller: Pad %d",port+1);}
        else strncpy_s(out,capacity,row==1?"Button mapping":"Restore default mapping",_TRUNCATE);
        return;
    }
    if(page==NativeSettingsPage::ControllerPort){if(row==0)strncpy_s(out,capacity,"Automatic - first connected controller",_TRUNCATE);else _snprintf_s(out,capacity,_TRUNCATE,"Pad %d",row);return;}
    if(page==NativeSettingsPage::Mapping){
        if(row==10){strncpy_s(out,capacity,"Restore default mapping",_TRUNCATE);return;}
        const auto map=NativeGamepad::Mapping();
        _snprintf_s(out,capacity,_TRUNCATE,"%s sends %s",NativeGamepad::kButtonNames[row],NativeGamepad::kButtonNames[map[row]]);return;
    }
    if(page==NativeSettingsPage::Destination)_snprintf_s(out,capacity,_TRUNCATE,"%s",NativeGamepad::kButtonNames[row]);
}
static void F8NativeSettingsActivate(int obj,int row){
    using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();
    if(row<0||row>=F8NativeSettingsCount(page))return;
    if(row==F8NativeSettingsCount(page)-1){F8NativeSettingsPop(obj);return;}
    if(page==NativeSettingsPage::ElementNames){
        const auto catalog=ElementMenu::Read();const auto& item=catalog[4+row];
        g_nativeElementNameBit=item.nativeBit;strncpy_s(g_nativeElementNameKey,item.key,_TRUNCATE);
        strncpy_s(g_nativeElementNameDraft,item.label,_TRUNCATE);g_nativeElementNameCharacter=1;
        g_nativeSettingsNotice[0]=0;F8NativeSettingsPush(obj,NativeSettingsPage::ElementNameEdit);return;
    }
    if(page==NativeSettingsPage::ElementNameEdit){
        if(row==0){if(g_nativeSettingsDepth>=4)return;ElementNameInput::Begin(g_nativeElementNameDraft);F8NativeSettingsPush(obj,NativeSettingsPage::ElementNameCapture);return;}
        if(row==1){g_nativeElementNameCharacter=(g_nativeElementNameCharacter+1)%(sizeof(g_nativeElementNameAlphabet)-1);return;}
        if(row==2){const auto length=std::strlen(g_nativeElementNameDraft);
            if(length<ElementNames::MaximumLength){g_nativeElementNameDraft[length]=g_nativeElementNameAlphabet[g_nativeElementNameCharacter];g_nativeElementNameDraft[length+1]=0;}
            else strncpy_s(g_nativeSettingsNotice,ElementNames::Detail(ElementNames::Result::TooLong),_TRUNCATE);return;}
        if(row==3){const auto length=std::strlen(g_nativeElementNameDraft);if(length)g_nativeElementNameDraft[length-1]=0;return;}
        if(row==4){g_nativeElementNameDraft[0]=0;return;}
        const auto result=ElementMenu::SaveName(g_nativeElementNameBit,g_nativeElementNameKey,g_nativeElementNameDraft,row==6);
        strncpy_s(g_nativeSettingsNotice,ElementNames::Detail(result),_TRUNCATE);
        if(result==ElementNames::Result::Saved)F8NativeSettingsPop(obj);return;
    }
    if(page==NativeSettingsPage::ElementNameCapture){
        if(row==1){
            if(!ElementNameInput::Acceptable()){strncpy_s(g_nativeSettingsNotice,"Input rejected. Backspace or Delete to correct the name.",_TRUNCATE);return;}
            ElementNameInput::Copy(g_nativeElementNameDraft);F8NativeSettingsPop(obj);
            strncpy_s(g_nativeSettingsNotice,"Name staged. Choose Save name to apply.",_TRUNCATE);}
        return;
    }
    if(F8RewardPage(page)){F8RewardActivate(obj,page,row);return;}
    if(page==NativeSettingsPage::VanguardEquipment&&row==2){F8NativeSettingsPush(obj,NativeSettingsPage::VanguardCommandBindings);return;}
    if(page==NativeSettingsPage::VanguardCommandBindings){
        Vanguard::BindingState bindings{};
        if(!Vanguard::ReadUiBindings(bindings)||!bindings.stamp){strncpy_s(g_nativeSettingsNotice,"Loaded command validator unavailable. Nothing was changed.",_TRUNCATE);return;}
        const auto& binding=bindings.entries[row];g_vanguardBindingEffect=static_cast<unsigned>(row);
        g_vanguardBindingCommand=binding.command>=0x3000&&binding.command<=0x313F?binding.command:0x3000;
        g_vanguardBindingCost=binding.cost<=256?binding.cost:256;g_vanguardBindingStamp=bindings.stamp;
        F8NativeSettingsPush(obj,NativeSettingsPage::VanguardCommandEdit);return;
    }
    if(page==NativeSettingsPage::VanguardCommandEdit){
        if(row==0){if(g_vanguardBindingCommand<0x313F)++g_vanguardBindingCommand;return;}
        if(row==1){if(g_vanguardBindingCommand>0x3000)--g_vanguardBindingCommand;return;}
        if(row==2){g_vanguardBindingCost=g_vanguardBindingCost==256?0:(std::min)(255u,g_vanguardBindingCost+1);return;}
        if(row==3){g_vanguardBindingCost=g_vanguardBindingCost==256?255:g_vanguardBindingCost?g_vanguardBindingCost-1:0;return;}
        if(row==4){g_vanguardBindingCost=256;return;}
        const unsigned packed=row==6?0:g_vanguardBindingCommand|(g_vanguardBindingCost==256?0:((g_vanguardBindingCost+1)<<16));
        Vanguard::BindingState current{};
        if(!Vanguard::ReadUiBindings(current)||current.stamp!=g_vanguardBindingStamp){
            strncpy_s(g_nativeSettingsNotice,"Command data or settings changed. Cancel and review a fresh binding.",_TRUNCATE);return;}
        if(!Vanguard::SaveUiBinding(g_vanguardBindingEffect,packed,g_vanguardBindingStamp)){
            strncpy_s(g_nativeSettingsNotice,"Binding rejected or save failed. Use a unique executable command outside battle.",_TRUNCATE);return;}
        strncpy_s(g_nativeSettingsNotice,packed?"Binding saved. It grants nothing unless the matching ability is equipped.":"Binding disabled. Native learned commands are unchanged.",_TRUNCATE);
        F8NativeSettingsPop(obj);return;
    }
    if(page==NativeSettingsPage::Arena){F8NativeSettingsPush(obj,NativeSettingsPage::ArenaOptions);return;}
    if(page==NativeSettingsPage::TextLanguages){
        if(row==2){strncpy_s(g_nativeSettingsNotice,TextLanguage::Native::Detail(),_TRUNCATE);return;}
        const bool saved=TextLanguage::Settings::Save(row);
        strncpy_s(g_nativeSettingsNotice,saved?"Saved. Restart FFX to apply text language. Audio is unchanged.":"Unable to save text language. Previous choice preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
        return;
    }
    if(page==NativeSettingsPage::PhotoMode){
        (void)PhotoMode::MenuAction(row);
        PhotoMode::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));return;
    }
    if(page==NativeSettingsPage::Seymour){
        if(row>=13){
            if(row==14||SphereGridProgress8Runtime::MenuAction())SphereGridProgress8Runtime::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist Grid8 save setting.",_TRUNCATE);
            return;
        }
        if(row>=11){
            if(row==12||SeymourMenuList::MenuAction())SeymourMenuList::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist eight-character menu setting.",_TRUNCATE);
            return;
        }
        if(row>=9){
            if(row==10||SeymourPersistentRoster::MenuAction())SeymourPersistentRoster::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist permanent roster setting.",_TRUNCATE);
            return;
        }
        if(row>=7){
            if(row==8||SeymourGearSort::MenuAction())SeymourGearSort::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist equipment sorting setting.",_TRUNCATE);
            return;
        }
        if(row>=5){
            if(row==6||SeymourGearPresentation::MenuAction())SeymourGearPresentation::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist equipment presentation setting.",_TRUNCATE);
            return;
        }
        if(row>=3){
            if(row==4||SeymourOverdrive::MenuAction())SeymourOverdrive::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
            else strncpy_s(g_nativeSettingsNotice,"Unable to persist Overdrive setting.",_TRUNCATE);
            return;
        }
        const bool saved=SeymourCompatibility::MenuAction(row);
        if(row==2||saved)SeymourCompatibility::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
        else strncpy_s(g_nativeSettingsNotice,"Unable to persist Seymour setting.",_TRUNCATE);
        return;
    }
    const auto action=static_cast<NativeBindings::Action>(g_nativeSettingsAction);
    if(page==NativeSettingsPage::Vanguard){
        F8NativeSettingsPush(obj,row==7?NativeSettingsPage::VanguardMapping:
            static_cast<NativeSettingsPage>(static_cast<int>(NativeSettingsPage::VanguardDamage)+row));return;
    }
    if(page==NativeSettingsPage::VanguardMapping){
        Vanguard::MappingState mapping{};
        if(!Vanguard::ReadUiMapping(mapping)){strncpy_s(g_nativeSettingsNotice,"Loaded kernel unavailable. No mapping was changed.",_TRUNCATE);return;}
        g_vanguardMappingEffect=static_cast<unsigned>(row);
        g_vanguardMappingId=(std::max)(135u,(std::min)(4095u,mapping.ids[row]));
        g_vanguardMappingStamp=mapping.stamp;
        F8NativeSettingsPush(obj,NativeSettingsPage::VanguardMappingEdit);return;
    }
    if(page==NativeSettingsPage::VanguardMappingEdit){
        if(row==0){if(g_vanguardMappingId<4095)++g_vanguardMappingId;return;}
        if(row==1){if(g_vanguardMappingId>135)--g_vanguardMappingId;return;}
        Vanguard::MappingState current{};
        if(!Vanguard::ReadUiMapping(current)||current.stamp!=g_vanguardMappingStamp){
            strncpy_s(g_nativeSettingsNotice,"Kernel or mapping changed. Cancel and review a fresh selection.",_TRUNCATE);return;
        }
        if(!Vanguard::SaveUiMapping(g_vanguardMappingEffect,g_vanguardMappingId)){
            strncpy_s(g_nativeSettingsNotice,"ID rejected or save failed. Use a matching neutral row outside battle.",_TRUNCATE);return;
        }
        strncpy_s(g_nativeSettingsNotice,"Validated mapping saved. Binary names and rows were not modified.",_TRUNCATE);
        F8NativeSettingsPop(obj);return;
    }
    if(page==NativeSettingsPage::ArenaOptions||page==NativeSettingsPage::AdditionalMods||page==NativeSettingsPage::FieldScout||F8NativeVanguardGroup(page)){
        const auto* flag=F8NativeNestedSpec(page,row);if(!flag)return;
        const bool requested=!ResolveF8Flag(*flag).value;const auto result=SetF8FlagValue(*flag,requested);
        if(result.code==F8EditCode::RejectedNotWired||result.code==F8EditCode::RejectedUnavailable)
            strncpy_s(g_nativeSettingsNotice,"Native implementation unavailable. No setting was enabled.",_TRUNCATE);
        else if(result.code!=F8EditCode::Saved)strncpy_s(g_nativeSettingsNotice,"Unable to save. Previous value preserved.",_TRUNCATE);
        else if(result.effective.value!=requested)strncpy_s(g_nativeSettingsNotice,"Preference saved. An external override still controls this feature.",_TRUNCATE);
        else strncpy_s(g_nativeSettingsNotice,flag->activation==F8Activation::RestartRequired?
            "Saved. Restart FFX to apply this feature.":"Saved. The existing runtime will acknowledge the change.",_TRUNCATE);
        return;
    }
    if(page==NativeSettingsPage::ElementScan){
        if(row==10){F8NativeSettingsPush(obj,NativeSettingsPage::ElementNames);return;}
        if(row==8||row==9){const auto catalog=ElementMenu::Read();const auto& item=catalog[row];
            if(!item.available){strncpy_s(g_nativeSettingsNotice,"Enable Elemental Core and restart. Built-in custom elements need no pack.",_TRUNCATE);return;}
            g_nativeElementIndex=4+static_cast<unsigned>(row-8);strncpy_s(g_nativeHookElementKey,item.key,_TRUNCATE);
            _snprintf_s(g_nativeHookElementTitle,sizeof(g_nativeHookElementTitle),_TRUNCATE,"%.64s color",item.label);
            F8NativeSettingsPush(obj,NativeSettingsPage::ElementColor);return;}
        if(row<2){
            const auto* flag=F8NativeScanFlag(row);
            if(!flag){strncpy_s(g_nativeSettingsNotice,"Scan control unavailable.",_TRUNCATE);return;}
            const bool requested=!ResolveF8Flag(*flag).value;
            const auto result=SetF8FlagValue(*flag,requested);
            if(result.code!=F8EditCode::Saved)
                strncpy_s(g_nativeSettingsNotice,"Unable to save Scan setting. Previous value preserved.",_TRUNCATE);
            else if(result.effective.value!=requested)
                _snprintf_s(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice),_TRUNCATE,
                    "%s saved %s. External override keeps it %s.",flag->label,
                    requested?"ON":"OFF",result.effective.value?"ON":"OFF");
            else _snprintf_s(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice),_TRUNCATE,
                "%s saved %s. Restart FFX to apply.",flag->label,requested?"ON":"OFF");
            return;
        }
        row-=2;
        if(row<3||row==5){g_nativeElementIndex=static_cast<unsigned>(row==5?3:row);F8NativeSettingsPush(obj,NativeSettingsPage::ElementColor);}
        else F8NativeSettingsPush(obj,row==3?NativeSettingsPage::ElementBit:NativeSettingsPage::ElementVisibility);return;
    }else if(page==NativeSettingsPage::ElementVisibility){
        if(row>=4){const auto catalog=ElementMenu::Read();const auto& item=catalog[8+row-4];char key[128]{};
            if(!item.available||!ElementScan::HookSettingKey(item.key,"enabled",key)){strncpy_s(g_nativeSettingsNotice,"Hook element unavailable. Nothing changed.",_TRUNCATE);return;}
            const auto value=Config::ReadIntExact(key,0,1);
            const unsigned next=value.state==Config::IntReadState::Valid&&value.value==0?1u:0u;
            const bool saved=ElementScan::SaveHookPresentation(item.key,"enabled",next);
            strncpy_s(g_nativeSettingsNotice,saved?"Saved. Scan visibility changes next frame.":"Unable to save. Previous visibility preserved.",_TRUNCATE);return;}
        const auto current=Config::ReadIntExact(ElementScan::EnabledKeys[row],0,1);
        const ElementScan::Settings defaults{};
        // Missing uses the per-column migration default. Invalid is repaired to
        // OFF; repairing malformed preferences must not enable a new column.
        const bool next=current.state==Config::IntReadState::Missing?defaults.enabled[row]==0:
            current.state==Config::IntReadState::Valid&&current.value==0;
        const bool saved=ElementScan::SaveEnabled(static_cast<unsigned>(row),next);
        strncpy_s(g_nativeSettingsNotice,saved?"Element choice saved. Applies next Scan frame when the master is enabled.":"Unable to save element choice. Previous value preserved.",_TRUNCATE);
        return;
    }else if(page==NativeSettingsPage::ElementColor){
        if(row<3){F8NativeElementAdjust(row,row==0?15:10);return;}
        const auto rgb=g_nativeElementDirty?ElementScan::FromHsv(g_nativeElementHsv):g_nativeElementRgb;
        const bool saved=g_nativeElementIndex<4?ElementScan::SaveColor(g_nativeElementIndex,rgb):
            ElementScan::SaveHookPresentation(g_nativeHookElementKey,"rgb",rgb);
        strncpy_s(g_nativeSettingsNotice,saved?"Color saved. Visible on the next Scan frame when enabled.":"Unable to save color. Previous value preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);return;
    }else if(page==NativeSettingsPage::ElementBit){
        const bool saved=ElementScan::SaveBit(row==0?32u:64u);
        strncpy_s(g_nativeSettingsNotice,saved?"Third element selected. Gameplay masks are unchanged.":"Unable to save the third element. Previous value preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);return;
    }else if(page==NativeSettingsPage::Workshop){
        if(row==0){F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopRefinement);return;}
        if(row==4){F8NativeSettingsPush(obj,NativeSettingsPage::WorkshopExpansion);return;}
        if(row<1||row>3)return;
        const auto current=Config::ReadIntExact(EquipmentWorkshop::Settings::DevelopmentKeys[row-1],0,1);
        // An invalid value is repaired to OFF, never silently enabled.
        const bool enabled=current.state==Config::IntReadState::Missing || (current.state==Config::IntReadState::Valid&&current.value==0);
        const bool saved=EquipmentWorkshop::Settings::SaveDevelopment(static_cast<unsigned>(row-1),enabled);
        strncpy_s(g_nativeSettingsNotice,saved?"Development setting saved. Prices stay visible; fusion still destroys its donor.":"Unable to save the development setting. Previous value preserved.",_TRUNCATE);
    }else if(page==NativeSettingsPage::WorkshopExpansion){
        const bool saved=EquipmentWorkshop::Settings::SaveExpansion(static_cast<unsigned>(row+1));
        strncpy_s(g_nativeSettingsNotice,saved?"Expansion recipe saved. Existing slots are unchanged.":"Unable to save expansion recipe. Previous choice preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    }else if(page==NativeSettingsPage::WorkshopRefinement){
        const bool saved=EquipmentWorkshop::Settings::SaveMode(row==0?2u:1u);
        strncpy_s(g_nativeSettingsNotice,saved?"Saved for future refinements. Existing ability ranks are unchanged.":"Unable to save the refinement mode. Previous setting preserved.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    }else if(page==NativeSettingsPage::Languages){g_nativeSettingsLanguage=row;F8NativeSettingsPush(obj,NativeSettingsPage::LanguageChoice);}
    else if(page==NativeSettingsPage::LanguageChoice){
        const bool saved=Config::SetInt(g_nativeLanguageKeys[g_nativeSettingsLanguage],row);
        strncpy_s(g_nativeSettingsNotice,saved?"Saved. Restart FFX to apply the audio language.":"Unable to save the audio language.",_TRUNCATE);
        if(saved)F8NativeSettingsPop(obj);
    } else if(page==NativeSettingsPage::Keyboard||page==NativeSettingsPage::Gamepad){
        g_nativeSettingsAction=row;const auto selected=static_cast<NativeBindings::Action>(row);
        const bool opened=page==NativeSettingsPage::Keyboard?NativePorts::BeginBindingCapture(selected):NativePorts::BeginGamepadCapture(selected);
        if(opened){g_nativeSettingsNotice[0]=0;F8NativeSettingsPush(obj,page==NativeSettingsPage::Keyboard?NativeSettingsPage::KeyCapture:NativeSettingsPage::PadCapture);}
        else strncpy_s(g_nativeSettingsNotice,"No XInput controller detected. Connect it or enable Steam Input.",_TRUNCATE);
    } else if(page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture){
        const bool saved=page==NativeSettingsPage::KeyCapture?NativePorts::SaveBinding(action,{})==NativeBindings::BindResult::Ok:NativePorts::SaveGamepadBinding(action,0);
        strncpy_s(g_nativeSettingsNotice,saved?"Shortcut cleared.":"Unable to save the shortcut.",_TRUNCATE);F8NativeSettingsPop(obj);
    } else if(page==NativeSettingsPage::Controller){
        if(row<2)F8NativeSettingsPush(obj,row==0?NativeSettingsPage::ControllerPort:NativeSettingsPage::Mapping);
        else strncpy_s(g_nativeSettingsNotice,NativeGamepad::SaveMapping(NativeGamepad::IdentityMap())?"Default mapping restored.":"Unable to save mapping.",_TRUNCATE);
    } else if(page==NativeSettingsPage::ControllerPort){
        const bool saved=Config::SetInt("gamepad.controller",row-1);strncpy_s(g_nativeSettingsNotice,saved?"Controller selection saved.":"Unable to save controller selection.",_TRUNCATE);if(saved){NativeGamepad::RefreshMapping();F8NativeSettingsPop(obj);}
    } else if(page==NativeSettingsPage::Mapping){
        if(row==10)strncpy_s(g_nativeSettingsNotice,NativeGamepad::SaveMapping(NativeGamepad::IdentityMap())?"Default mapping restored.":"Unable to save mapping.",_TRUNCATE);
        else {g_nativeSettingsMapFrom=row;F8NativeSettingsPush(obj,NativeSettingsPage::Destination);}
    } else if(page==NativeSettingsPage::Destination){
        auto map=NativeGamepad::Mapping();NativeGamepad::SwapDestination(map,static_cast<unsigned>(g_nativeSettingsMapFrom),static_cast<unsigned>(row));
        const bool saved=NativeGamepad::SaveMapping(map);strncpy_s(g_nativeSettingsNotice,saved?"Mapping saved. Shortcuts still use physical buttons.":"Unable to save mapping.",_TRUNCATE);if(saved)F8NativeSettingsPop(obj);
    }
}
static void F8NativeSettingsInput(int obj){
    using namespace NativeMenu;using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();const bool naming=page==NativeSettingsPage::ElementNameCapture;
    const bool capturing=page==NativeSettingsPage::KeyCapture||page==NativeSettingsPage::PadCapture||naming;
    if(g_f7ConfirmTimer>0)--g_f7ConfirmTimer;
    if(naming){
        char value[65]{};bool cancelled=false;
        if(ElementNameInput::Consume(value,cancelled)){
            if(!cancelled)strncpy_s(g_nativeElementNameDraft,value,_TRUNCATE);
            F8NativeSettingsPop(obj);
            strncpy_s(g_nativeSettingsNotice,cancelled?"Typing cancelled.":"Name staged. Choose Save name to apply.",_TRUNCATE);return;
        }
        if(!ElementNameInput::Acceptable())strncpy_s(g_nativeSettingsNotice,"Input rejected. Backspace or Delete to correct the name.",_TRUNCATE);
    }
    if(capturing&&!naming){
        bool done=false,saved=false,cancelled=false;
        if(page==NativeSettingsPage::KeyCapture){NativeBindings::BindResult result{};done=NativePorts::ConsumeBindingCapture(&result,&cancelled);saved=done&&!cancelled&&result==NativeBindings::BindResult::Ok;}
        else done=NativePorts::ConsumeGamepadCapture(&saved,&cancelled);
        if(done){strncpy_s(g_nativeSettingsNotice,cancelled?"Shortcut edit cancelled.":saved?"Shortcut saved.":"Shortcut not saved. Avoid reserved or duplicate combinations.",_TRUNCATE);F8NativeSettingsPop(obj);PlaySfx(saved?4:3);return;}
    }
    const auto mouse=F7ListMouseTick(obj,NX(0.250f),NY(F8NativeSettingsTop()+0.175f),NW(0.500f),NH(0.052f),NH(0.045f),RdW(obj,O_COUNT),9);
    const int edge=PadEdge(),dir=capturing&&!naming?0:F7Ui::ResolveDirectionalInput(PadDir(),mouse.ownsDirectionalFrame);
    int selected=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP),count=RdW(obj,O_COUNT);
    if(g_nativeElementRepeat>0)--g_nativeElementRepeat;
    if(page==NativeSettingsPage::ElementColor&&selected<3&&(dir&0xA000)&&!g_nativeElementRepeat){
        F8NativeElementAdjust(selected,(dir&0x8000)?-1:1);g_nativeElementRepeat=6;
    }
    if(page==NativeSettingsPage::ElementNameEdit&&selected==1&&(dir&0xA000)&&!g_nativeElementRepeat){
        const unsigned alphabetCount=static_cast<unsigned>(sizeof(g_nativeElementNameAlphabet)-1);
        g_nativeElementNameCharacter=(g_nativeElementNameCharacter+((dir&0x8000)?alphabetCount-1:1))%alphabetCount;g_nativeElementRepeat=6;
    }
    if(dir&0x1000){if(selected>0)--selected;}
    else if(dir&0x4000){if(selected+1<count)++selected;}
    if(selected<top)top=selected;else if(selected>=top+9)top=selected-8;
    WrW(obj,O_SELECTED,static_cast<int16_t>(selected));WrW(obj,O_TOP,static_cast<int16_t>(top));
    const bool confirm=mouse.confirm||((!capturing||naming)&&(edge&0x20)&&!(g_nativeSettingsLastEdge&0x20));
    const bool cancel=(!capturing||naming)&&(edge&0x40)&&!(g_nativeSettingsLastEdge&0x40);
    g_nativeSettingsLastEdge=edge;
    if(g_f7ConfirmTimer==0 && (confirm||cancel)){
        if(cancel)F8NativeSettingsPop(obj);else F8NativeSettingsActivate(obj,selected);
        g_f7ConfirmTimer=12;PlaySfx(1);
    }
}
static void F8NativeSettingsDraw(int obj,int frame){
    using namespace NativeMenu;using namespace FfxHooks;
    const auto page=F8NativeSettingsPage();const int selected=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP),count=RdW(obj,O_COUNT);
    if(page==NativeSettingsPage::Vanguard||F8NativeVanguardGroup(page)||page==NativeSettingsPage::VanguardMapping||page==NativeSettingsPage::VanguardMappingEdit||page==NativeSettingsPage::VanguardCommandBindings||page==NativeSettingsPage::VanguardCommandEdit)Vanguard::RefreshUi();
    const float panelTop=F8NativeSettingsTop(),panelHeight=F8NativeSettingsHeight();
    auto text=[page](const char* label,float x,float y,bool title=false){
        unsigned char value[128]{};
        if(page==NativeSettingsPage::TextLanguages&&std::strchr(label,static_cast<char>(0xC3))){
            std::vector<std::uint8_t> bytes;std::string error;
            if(TextLanguage::EncodeLiteral(TextLanguage::Font{},label,sizeof(value)-1,bytes,error))
                std::copy(bytes.begin(),bytes.end(),value);
            else EncodeLabel("Text label unavailable",value,sizeof(value));
        }else EncodeLabel(label,value,sizeof(value));
        if(title)DrawString(value,x,y);else DrawStringSub(value,x,y);
    };
    DrawMenuBackdrop();DrawMenuNeonFrame(frame);
    DrawMenuGlassPanel(NX(0.205f),NY(panelTop),NW(0.590f),NH(panelHeight),frame,1);
    text(F8NativeSettingsTitle(page),NX(0.250f),NY(panelTop+0.037f),true);
    const auto pad=NativeGamepad::Poll();
    const char* help="Select an option. Back returns to F8.";
    if(F8RewardPage(page))help=F8RewardHelp(page);
    else if(page==NativeSettingsPage::Arena)help="Open Arena+ options. Back returns to Reforge.";
    else if(page==NativeSettingsPage::ArenaOptions)help="Existing Arena+ controls. Back returns to Arena+.";
    else if(page==NativeSettingsPage::Vanguard)help="Independent opt-in rules; opening a group changes nothing.";
    else if(page==NativeSettingsPage::VanguardMapping)help="IDs select installed neutral rows; they do not rename data.";
    else if(page==NativeSettingsPage::VanguardMappingEdit)help="Stage an ID; Save checks the current kernel. Cancel discards.";
    else if(page==NativeSettingsPage::VanguardCommandBindings)help="Explicit equipment-to-command associations; defaults Disabled.";
    else if(page==NativeSettingsPage::VanguardCommandEdit)help="One atomic binding. Partial OD fees need their independent toggle.";
    else if(page==NativeSettingsPage::AdditionalMods||page==NativeSettingsPage::FieldScout||F8NativeVanguardGroup(page)){const auto* flag=F8NativeNestedSpec(page,selected);if(flag)help=flag->help;}
    else if(page==NativeSettingsPage::Keyboard)help="Choose an action, then press its keyboard shortcut.";
    else if(page==NativeSettingsPage::Gamepad)help="Use two or more buttons. Release the combo to save.";
    else if(page==NativeSettingsPage::Controller)help=pad.connected?"XInput / Steam Input controller connected.":"Connect an XInput controller or enable Steam Input.";
    else if(page==NativeSettingsPage::KeyCapture)help="Press keys. Esc cancels; Backspace/Delete clears.";
    else if(page==NativeSettingsPage::PadCapture)help="Press a combo, then release. Mouse: clear or cancel.";
    else if(page==NativeSettingsPage::Destination)help="Used targets swap places. Every button stays mapped.";
    else if(page==NativeSettingsPage::TextLanguages)help="PT-BR pack and English game text required. Restart to apply.";
    else if(page==NativeSettingsPage::Languages)help="Voices, battle sounds and movie audio. Restart required.";
    else if(page==NativeSettingsPage::LanguageChoice)help="Game default, English or Japanese. Restart required.";
    else if(page==NativeSettingsPage::Workshop)help="Development only. Free materials still require one each.";
    else if(page==NativeSettingsPage::WorkshopRefinement)help="B rolls one ability. A improves all. Existing ranks stay.";
    else if(page==NativeSettingsPage::WorkshopExpansion)help="Each added slot uses its matching Key Sphere level.";
    else if(page==NativeSettingsPage::ElementScan){
        if(selected==0)help="Eight monster stats and MP. Restart after changing.";
        else if(selected==1)help="Extra affinities in Scan and Sensor. Restart after changing.";
        else help="Colors and choices apply next frame when elements are ON.";
    }
    else if(page==NativeSettingsPage::ElementColor)help="Left/Right: fine change. Enter: larger step. Save or Cancel.";
    else if(page==NativeSettingsPage::ElementNames)help="Rename hook element labels. Item attributes keep their names.";
    else if(page==NativeSettingsPage::ElementNameEdit)help="Type a name or use the character picker. Save applies; Back discards.";
    else if(page==NativeSettingsPage::ElementNameCapture)help="Type up to 32 characters. Enter accepts; Esc cancels typing.";
    else if(page==NativeSettingsPage::ElementBit)help="Only changes the displayed bit; adds no gameplay effects.";
    else if(page==NativeSettingsPage::ElementVisibility)help="Independent choices. Scan Extra Elements must be ON.";
    if(page==NativeSettingsPage::PhotoMode){
        help="Controls below actions. All default OFF. Hold does not pause.";
        PhotoMode::Detail(g_nativeSettingsNotice,sizeof(g_nativeSettingsNotice));
    }
    if(page==NativeSettingsPage::Seymour){
        help="All default OFF. Master required. First enable: restart.";
    }
    text(help,NX(0.250f),NY(panelTop+0.106f));
    for(int row=top;row<count&&row<top+9;++row){
        const float y=NY(panelTop+0.175f+(row-top)*0.052f);
        DrawSolidRect(NX(0.250f),y,NW(0.500f),NH(0.045f),0x50314558u,0x30303A48u);
        if(row==selected){DrawSolidRect(NX(0.250f),y,NW(0.500f),NH(0.045f),0x60345263u,0x40273849u);DrawSolidRect(NX(0.250f),y+NH(0.043f),NW(0.500f),NH(0.002f),kMenuNeonGreenLine,kMenuNeonGreenLineLo);DrawCursor(NX(0.223f),y);}
        char label[100]{};F8NativeSettingsLabel(page,row,label,sizeof(label));text(label,NX(0.265f),y+NH(0.012f));
    }
    if(page==NativeSettingsPage::ElementColor){
        const auto rgb=g_nativeElementDirty?ElementScan::FromHsv(g_nativeElementHsv):g_nativeElementRgb;
        DrawSolidRect(NX(.718f),NY(panelTop+.033f),NW(.028f),NH(.040f),0x80000000u|rgb,0x80000000u|rgb);
        for(unsigned i=0;i<36;++i){const auto color=0x80000000u|ElementScan::FromHsv({i*10,100,100});
            DrawSolidRect(NX(.25f+static_cast<float>(i)*(.5f/36)),NY(panelTop+panelHeight-.111f),NW(.5f/36),NH(.012f),color,color);}
        DrawSolidRect(NX(.25f+static_cast<float>(g_nativeElementHsv.h)/360*.5f),NY(panelTop+panelHeight-.114f),NW(.002f),NH(.018f),0x80FFFFFFu,0x80FFFFFFu);
    }
    char first[57]{},second[73]{};strncpy_s(first,g_nativeSettingsNotice,56);
    if(strlen(g_nativeSettingsNotice)>56)strncpy_s(second,g_nativeSettingsNotice+56,_TRUNCATE);
    text(first,NX(0.250f),NY(panelTop+panelHeight-0.09f));text(second,NX(0.250f),NY(panelTop+panelHeight-0.06f));
    text("Mouse/Scroll  Arrows: move  Enter: select  Back: return",NX(0.250f),NY(panelTop+panelHeight-0.03f));
}
