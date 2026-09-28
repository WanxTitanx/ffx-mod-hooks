// UI backend substitute. The native frame and actual runtime persistence have
// their own isolated executable; these cases execute the real menu handlers.
#include "../hooks/MonsterRewardSettings.h"
namespace FfxHooks::MonsterRewards {
static RateTable menuRates{};
unsigned Count() noexcept {return 2;}
bool SpeciesAt(unsigned row,unsigned& species) noexcept {if(row>=2)return false;species=row?342:1;return true;}
bool ReadPreview(unsigned id,Preview& out) noexcept {
    if(id>=SpeciesCount)return false;out={};out.species=id;out.source=Source::ModFile;
    out.base=id==1?BaseRewards{60000,65000,65535}:BaseRewards{456,123,246};
    std::snprintf(out.name,sizeof(out.name),"%s",id==1?"Raldo":"Test monster");
    for(unsigned type=0;type<2;++type){const auto kind=type?Kind::Gil:Kind::Ap;char key[80]{};
        std::snprintf(key,sizeof(key),"monster_rewards.m%03u.%s_multiplier",id,type?"gil":"ap");
        const unsigned multiplier=kind==Kind::Ap?menuRates[id].ap:menuRates[id].gil;
        const auto* flag=FindF8Flag(type?"cheats.gil_100x":"cheats.ap_100x");
        const unsigned global=ResolveF8Flag(*flag).value?static_cast<unsigned>(ResolveF8Scalar(*flag).value):1;
        if(kind==Kind::Ap){out.apMultiplier=multiplier;out.globalAp=global;out.ap=Calculate(out.base.ap,multiplier,global,kind);out.overkillAp=Calculate(out.base.overkillAp,multiplier,global,kind);}
        else{out.gilMultiplier=multiplier;out.globalGil=global;out.gil=Calculate(out.base.gil,multiplier,global,kind);}
    }return true;
}
bool SaveMultiplier(unsigned id,Kind kind,unsigned value){
    if(id>=SpeciesCount||!value||value>MaximumMultiplier)return false;char key[80]{};
    std::snprintf(key,sizeof(key),"monster_rewards.m%03u.%s_multiplier",id,kind==Kind::Ap?"ap":"gil");
    ++f8Writes;if(!f8AllowWrite)return false;
    auto& requested=kind==Kind::Ap?menuRates[id].ap:menuRates[id].gil;requested=static_cast<std::uint16_t>(value);return true;
}
const char* Detail() noexcept {return "Test backend";}
}
static void MonsterRewardMenuCases(){
    using namespace FfxHooks;using namespace MonsterRewards;
    menuRates={};
    Config::ResetForTests();Config::LoadTextForTests("[cheats]\nap_100x=1\nap_multiplier=25\ngil_100x=1\ngil_multiplier=7\n","C:\\private-monster-menu.ini");
    Config::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;f8Writes=0;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"reward menu allocates");return;}
    for(const auto* key:{"cheats.ap_100x","cheats.gil_100x","cheats.monster_rewards"}){
        const auto* flag=FindF8Flag(key);Check(flag&&F8NativeNestedFlag(*flag),"reward controls are grouped while their canonical catalog identities survive");}
    Check(!ResolveF8Flag(*FindF8Flag("cheats.monster_rewards")).value,"individual reward hook stays OFF until explicitly enabled");
    F8NativeSettingsPush(obj,NativeSettingsPage::RewardMultipliers);F8NativeSettingsActivate(obj,5);
    Check(F8NativeSettingsPage()==NativeSettingsPage::MonsterRewardList&&F8NativeSettingsCount(F8NativeSettingsPage())==3,"the monster browser includes every provider entry and Back");
    F8NativeSettingsActivate(obj,0);Check(F8NativeSettingsPage()==NativeSettingsPage::MonsterRewardDetail&&g_rewardSpecies==1,"selecting one monster opens only its rates");
    char label[128]{};F8NativeSettingsLabel(F8NativeSettingsPage(),3,label,sizeof(label));
    Check(std::strstr(label,"65000 > 65000 > 1625000")!=nullptr,"preview shows original, individual and global stages before edits");
    F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,7);
    Preview current{};ReadPreview(1,current);Check(current.apMultiplier==1&&f8Writes==0,"Cancel discards the individual draft without a hidden enable or write");
    F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,1);f8AllowWrite=false;F8NativeSettingsActivate(obj,6);
    ReadPreview(1,current);Check(current.apMultiplier==1&&F8NativeSettingsPage()==NativeSettingsPage::RewardRate,"failed persistence keeps the previous rate and the edit open");
    f8AllowWrite=true;F8NativeSettingsActivate(obj,6);ReadPreview(1,current);
    Check(current.apMultiplier==2&&current.gilMultiplier==1&&current.ap.individual==130000&&current.ap.applied==3250000,"one saved AP edit widens beyond 65535 and leaves Gil unchanged");
    Preview other{};ReadPreview(342,other);Check(other.apMultiplier==1&&other.gilMultiplier==1,"another monster's rates remain untouched");
    F8NativeSettingsActivate(obj,2);F8NativeSettingsActivate(obj,3);F8NativeSettingsActivate(obj,6);ReadPreview(1,current);
    Check(current.gilMultiplier==11&&current.apMultiplier==2,"Gil has its own editor and multiplier");
    NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
    Check(WorkshopRendered("Overkill AP")&&WorkshopRendered("before native"),"preview includes overkill and the native-bonus boundary");
    F8NativeSettingsActivate(obj,9);Check(F8NativeSettingsPage()==NativeSettingsPage::MonsterRewardList,"detail Back restores its browser");
    F8NativeSettingsActivate(obj,2);Check(F8NativeSettingsPage()==NativeSettingsPage::RewardMultipliers,"browser Back restores AP/Gil controls");
    PublishF8RuntimeScalarStatus("cheats.ap_100x",F8RuntimeAvailability::Available,true,true,true,25);
    F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,1);F8NativeSettingsActivate(obj,6);
    ReadPreview(1,current);
    Check(Config::ReadIntExact("cheats.ap_multiplier",1,100).value==26&&ResolveF8Flag(*FindF8Flag("cheats.ap_100x")).value&&current.apMultiplier==2&&current.gilMultiplier==11,
        "the relocated global rate editor preserves its enable state and both per-monster rates");
    F8NativeSettingsActivate(obj,6);g_rewardIdDraft=4095;F8NativeSettingsActivate(obj,4);
    Check(g_rewardSpecies==4095&&F8NativeSettingsPage()==NativeSettingsPage::MonsterRewardDetail,"custom monsters remain individually addressable beyond the name catalog");
    F8NativeSettingsReset();NativeMenu::Reset(obj);Config::ResetForTests();
}
