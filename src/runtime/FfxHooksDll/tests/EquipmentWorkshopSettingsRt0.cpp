#include "../hooks/EquipmentWorkshopSettings.h"
#include <cstdio>
#include <cstring>
#include <string>
namespace C=FfxHooks::Config;
namespace S=FfxHooks::EquipmentWorkshop::Settings;
static unsigned checks=0,failures=0,writes=0;
static bool allowWrite=true;
static std::string disk;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static bool Persist(void*,const char*,const char* text){++writes;if(!allowWrite)return false;disk=text;return true;}
static void Load(const char* text){
    C::ResetForTests();Check(C::LoadTextForTests(text,"C:\\private-workshop-settings.ini"),"private config loads");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
}
int main(){
    Load("[core]\nlog_level=1\n");workshop::Policy p{};
    Check(S::Read(p)&&p.mode==2&&p.baseItem==70&&p.baseAmount==1&&p.refinementDivisor==10&&p.fusionDivisor==3&&p.fusionGilPerAbility==10000,"absent config defaults to official random economy");
    Check(S::SaveMode(1)&&S::Read(p)&&p.mode==1,"F8 choice persists explicit A with readback");
    const auto saved=disk;Load(saved.c_str());
    Check(S::Read(p)&&p.mode==1,"A survives config reload");
    allowWrite=false;const auto before=p;
    Check(!S::SaveMode(2)&&S::Read(p)&&std::memcmp(&before,&p,sizeof(p))==0,"failed F8 persistence preserves active policy");
    allowWrite=true;Check(S::SaveMode(2)&&S::Read(p)&&p.mode==2,"B is selectable again without mutating equipment");
    const auto previousWrites=writes;
    Check(!S::SaveMode(0)&&!S::SaveMode(3)&&writes==previousWrites,"invalid choices never persist");
    Load("[equipment_workshop]\nrefinement_mode=wrong\n");p=before;
    Check(!S::Read(p)&&std::memcmp(&p,&before,sizeof(p))==0,"invalid stored mode is not silently accepted or rewritten");
    Check(S::SaveMode(2)&&S::Read(p)&&p.mode==2,"F8 can repair an invalid mode without changing other config");
    Load("[equipment_workshop]\nbase_sphere_item=73\nbase_sphere_amount=4\nrefinement_divisor=5\nfusion_divisor=6\nfusion_gil_per_ability=25000\nmod_recipe_quantity=60\n");
    Check(S::Read(p)&&p.mode==2&&p.baseItem==73&&p.baseAmount==4&&p.refinementDivisor==5&&p.fusionDivisor==6&&p.fusionGilPerAbility==25000&&p.modRecipeQuantity==60,"distribution tuning reads all documented keys");
    Load("[equipment_workshop]\nfusion_gil_per_ability=0\n");
    Check(!S::Read(p),"zero-cost fusion cannot be enabled through an invalid policy");
    Load("[equipment_workshop]\ndev_free_materials=1\ndev_free_gil=1\ndev_ignore_progression=1\n");
    Check(S::Read(p)&&p.devFreeMaterials==1&&p.devFreeGil==1&&p.devIgnoreProgression==1,"explicit Workshop development flags are read independently");
    Load("[equipment_workshop]\ndev_free_gil=2\n");
    Check(!S::Read(p),"out-of-range development flag fails closed");
    Load("[core]\nlog_level=1\n");
    Check(S::Read(p)&&!p.devFreeMaterials&&!p.devFreeGil&&!p.devIgnoreProgression,"all development shortcuts default OFF");
    unsigned recipe=0;Check(S::ReadExpansion(recipe)&&recipe==1,"absent expansion preference selects A");
    Check(S::SaveExpansion(2)&&S::ReadExpansion(recipe)&&recipe==2,"expansion B persists and reads back");
    const auto expansion=disk;Load(expansion.c_str());Check(S::ReadExpansion(recipe)&&recipe==2,"expansion preference survives reload");
    allowWrite=false;Check(!S::SaveExpansion(1)&&S::ReadExpansion(recipe)&&recipe==2,"failed expansion write preserves the previous recipe");allowWrite=true;
    Check(!S::SaveExpansion(0)&&!S::SaveExpansion(3),"out-of-range expansion choices are rejected");
    Load("[equipment_workshop]\nexpansion_recipe=3\n");recipe=99;
    Check(!S::ReadExpansion(recipe)&&recipe==99,"invalid saved expansion is not silently repaired to a cheaper recipe");
    C::ResetForTests();std::printf("WORKSHOP_SETTINGS %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
