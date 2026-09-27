#pragma once
#include "../shared/Config.h"
#include "../../../../research/equipment_workshop/include/workshop.h"

namespace FfxHooks::EquipmentWorkshop::Settings {
inline constexpr const char* ModeKey="equipment_workshop.refinement_mode";
inline constexpr const char* ExpansionKey="equipment_workshop.expansion_recipe";
inline bool ReadExpansion(unsigned& out){
    const auto read=Config::ReadIntExact(ExpansionKey,1,2);
    if(read.state==Config::IntReadState::Invalid)return false;
    out=read.state==Config::IntReadState::Valid?static_cast<unsigned>(read.value):1u;return true;
}
inline bool SaveExpansion(unsigned recipe){
    if(recipe<1||recipe>2||!Config::SetInt(ExpansionKey,static_cast<int>(recipe)))return false;
    unsigned observed=0;return ReadExpansion(observed)&&observed==recipe;
}
inline bool AdmitsExpansion(const workshop::Request& request){
    if(request.op!=workshop::Op::Expand)return true;
    unsigned recipe=0;return ReadExpansion(recipe)&&request.policy==recipe-1;
}
inline constexpr const char* DevelopmentKeys[]={"equipment_workshop.dev_free_materials","equipment_workshop.dev_free_gil","equipment_workshop.dev_ignore_progression"};
inline bool Read(workshop::Policy& out){
    workshop::Policy candidate{};
    struct Field {const char* key;std::uint32_t* value;int minimum,maximum;};
    const Field fields[]={
        {ModeKey,&candidate.mode,1,2},
        {"equipment_workshop.base_sphere_item",&candidate.baseItem,70,73},
        {"equipment_workshop.base_sphere_amount",&candidate.baseAmount,1,99},
        {"equipment_workshop.refinement_divisor",&candidate.refinementDivisor,1,100},
        {"equipment_workshop.fusion_divisor",&candidate.fusionDivisor,1,100},
        {"equipment_workshop.fusion_gil_per_ability",&candidate.fusionGilPerAbility,1,100000000},
        {"equipment_workshop.mod_recipe_quantity",&candidate.modRecipeQuantity,1,255},
        {DevelopmentKeys[0],&candidate.devFreeMaterials,0,1},
        {DevelopmentKeys[1],&candidate.devFreeGil,0,1},
        {DevelopmentKeys[2],&candidate.devIgnoreProgression,0,1}
    };
    for(const auto& field:fields){const auto read=Config::ReadIntExact(field.key,field.minimum,field.maximum);
        if(read.state==Config::IntReadState::Invalid)return false;
        if(read.state==Config::IntReadState::Valid)*field.value=static_cast<std::uint32_t>(read.value);
    }
    if(!workshop::ValidPolicy(candidate))return false;
    out=candidate;return true;
}
inline bool SaveMode(unsigned mode){
    if(mode!=1&&mode!=2)return false;
    if(!Config::SetInt(ModeKey,static_cast<int>(mode)))return false;
    const auto read=Config::ReadIntExact(ModeKey,1,2);
    return read.state==Config::IntReadState::Valid&&read.value==static_cast<int>(mode);
}
inline bool SaveDevelopment(unsigned index,bool enabled){
    if(index>=3||!Config::SetInt(DevelopmentKeys[index],enabled?1:0))return false;
    const auto read=Config::ReadIntExact(DevelopmentKeys[index],0,1);
    return read.state==Config::IntReadState::Valid&&read.value==(enabled?1:0);
}
inline const char* ModeName(unsigned mode){return mode==1?"A - whole equipment":mode==2?"B - random ability":"Invalid mode";}
}
