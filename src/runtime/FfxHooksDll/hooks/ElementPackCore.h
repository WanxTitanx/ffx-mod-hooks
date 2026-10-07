#include "../shared/ExecutableProfile.h"
#pragma once
#include "ElementAffinity.h"
#include "ElementPackJson.h"
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace FfxHooks::ElementalDominion {

#ifdef FFXHOOKS_TARGET_STEAM_20261001
inline constexpr char SupportedExecutable[]="0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d";
#else
inline constexpr char SupportedExecutable[]="78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced";
#endif
enum Capability : unsigned { RegistryCapability=1,AffinityCapability=2,ContextCapability=4,
    SpellCapCapability=8,TacticsCapability=16,GravityCapability=32,EquipmentCapability=64,PresentationCapability=128 };
// This is the schema vocabulary, not the set of implemented runtime consumers.
// A host must pass only capabilities whose adapters have actually been admitted.
inline constexpr unsigned KnownCapabilities=255;
inline constexpr const char* CapabilityNames[]={"mod007.registry.v1","mod007.affinity.v1","mod007.context.v1",
    "mod007.spell-cap.v1","mod007.tactics.v1","mod007.gravity.v1","mod007.equipment.v1","mod007.presentation.v1"};
inline constexpr std::uint32_t InvalidRow=(std::numeric_limits<std::uint32_t>::max)();
enum class PackError : std::uint8_t { None,Json,Schema,Type,Range,Duplicate,Reference,Fingerprint,Capability,Policy,Capacity };
struct PackProblem {PackError code=PackError::None;std::string field;Json::Problem json{};};
enum class BankKind : unsigned { Item=2,Command=3,MonsterMagic1=4,MonsterMagic2=6,AutoAbility=8 };
inline constexpr unsigned RowWidth(BankKind kind) noexcept {
    return kind==BankKind::AutoAbility?108u:
        (kind==BankKind::MonsterMagic1||kind==BankKind::MonsterMagic2)?92u:96u;
}
struct BankSection {
    unsigned first=0,last=0,width=0,offset=0;
    std::uint32_t Row(unsigned index) const noexcept {
        return index>=first&&index<=last?offset+(index-first)*width:InvalidRow;
    }
};
struct PackBank {
    std::string key,locale,sha256;
    BankKind kind=BankKind::Command;
    unsigned bytes=0;
    std::vector<BankSection> sections;
    std::uint32_t Row(unsigned index) const noexcept {
        for(const auto& section:sections){const auto row=section.Row(index);if(row!=InvalidRow)return row;}
        return InvalidRow;
    }
};
struct ElementPart {unsigned element=InvalidElement,weight=1;};
enum class SpellClass : std::uint8_t { None,NativeMagic,Fury };
enum class EffectKind : std::uint8_t { Imperil,Ward,Nul,RemoveImperil,RemoveWard,RemoveNul,Cleanse };
struct ElementEffect {
    EffectKind kind=EffectKind::Imperil;
    unsigned element=InvalidElement,stacks=1,turns=3,chanceBp=10000;
};
struct CommandBinding {
    std::string key,rowSha256;
    unsigned bank=0,index=0,encoded=0;
    MixPolicy policy=MixPolicy::HighestExposure;
    SpellClass spell=SpellClass::None;
    bool augment=false,gravity=false;
    std::vector<ElementPart> parts;
    std::vector<ElementEffect> effects;
};
enum class ProfileKind : std::uint8_t { Monster,Character,Aeon };
struct ProfileAffinity {
    unsigned element=InvalidElement;
    std::int32_t baseBp=10000;
    bool locked=false,imperilImmune=false;
    unsigned imperilResistBp=10001; // Unspecified: inherit the actor profile's resistance.
};
struct GravityProfile {bool enabled=false,overrideImmunity=false,elementalAffinity=false;};
struct ActorProfile {
    std::string key,fileSha256;
    ProfileKind kind=ProfileKind::Monster;
    unsigned id=0,fileBytes=0,imperilLimit=4,imperilResistBp=0;
    GravityProfile gravity{};
    std::vector<ProfileAffinity> affinities;
};
struct EquipmentDelta {unsigned element=InvalidElement;std::int32_t deltaBp=0;};
struct EquipmentBinding {
    std::string key,rowSha256;
    unsigned bank=0,index=0,encoded=0;
    bool sos=false;
    unsigned kind=2,owners=0x3FFFF;
    std::vector<EquipmentDelta> deltas;
};
struct Pack {
    std::string packageId;
    unsigned version=0,requiredCapabilities=0;
    Registry registry;
    std::vector<PackBank> banks;
    std::vector<CommandBinding> commands;
    std::vector<ActorProfile> profiles;
    std::vector<EquipmentBinding> equipment;
    const CommandBinding* Command(unsigned encoded) const noexcept {
        for(const auto& command:commands)if(command.encoded==encoded)return &command;
        return nullptr;
    }
    const EquipmentBinding* Equipment(unsigned encoded) const noexcept {
        for(const auto& item:equipment)if(item.encoded==encoded)return &item;
        return nullptr;
    }
    std::uint32_t BoundRow(unsigned encoded) const noexcept {
        const auto* command=Command(encoded);
        return command&&command->bank<banks.size()?banks[command->bank].Row(command->index):InvalidRow;
    }
};
inline bool ValidFingerprint(std::string_view text) noexcept {
    if(text.size()!=64)return false;
    for(char ch:text)if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))return false;
    return true;
}

namespace PackDetail {
using V=Json::Value;
using T=Json::Type;
class Reader {
public:
    Reader(Pack& pack,PackProblem& problem,unsigned available):pack_(pack),problem_(problem),available_(available){}
    bool Read(const V& root){
        if(!Fields(root,{"schema","package_id","version","exe_sha256","requires","fallback","elements","banks","commands","profiles","equipment"}))return false;
        std::string schema,exe,fallback;
        if(!Text(root,"schema",schema)||schema!="ffx.mod007.elements.v1")return Fail(PackError::Schema,"schema");
        if(!Key(root,"package_id",pack_.packageId)||!Unsigned(root,"version",pack_.version,1,0x7FFFFFFF))return false;
        if(!Text(root,"exe_sha256",exe)||exe!=SupportedExecutable)return Fail(PackError::Fingerprint,"exe_sha256");
        if(!Text(root,"fallback",fallback)||fallback!="native-unmodified")return Fail(PackError::Policy,"fallback");
        const auto* requirements=Array(root,"requires",8);
        if(!requirements)return false;
        for(const auto& value:requirements->children){
            if(value.type!=T::String)return Fail(PackError::Type,"requires");
            unsigned bit=0;
            for(unsigned i=0;i<8;++i)if(value.text==CapabilityNames[i])bit=1u<<i;
            if(!bit||(available_&bit)==0)return Fail(PackError::Capability,value.text);
            if(pack_.requiredCapabilities&bit)return Fail(PackError::Duplicate,"requires");
            pack_.requiredCapabilities|=bit;
        }
        if(!Capability(RegistryCapability,"registry"))return false;
        const auto* elements=Array(root,"elements",ElementLimit);
        if(!elements)return false;
        for(const auto& value:elements->children){
            Element element{};
            if(!Fields(value,{"key","label_key","label","rgb","native_bit"})||
               !Key(value,"key",element.key)||!Text(value,"label_key",element.labelKey)||
               !Text(value,"label",element.label)||!Unsigned(value,"rgb",element.rgb,0,0xFFFFFF))return false;
            const auto* native=value.Find("native_bit");
            if(native&&native->type!=T::Null&&!Unsigned(value,"native_bit",element.nativeBit,0,128))return false;
            if(pack_.registry.Add(element)!=Error::Ok)return Fail(PackError::Duplicate,"elements");
        }
        // Every native command/weapon bit must have exactly one stable key.
        for(unsigned bit=1;bit<=128;bit<<=1)if(!pack_.registry.Native(bit))return Fail(PackError::Reference,"native_bit");
        const auto* banks=Array(root,"banks",5);
        if(!banks)return false;
        std::uint64_t totalBytes=0;
        for(const auto& value:banks->children){
            PackBank bank{};
            if(!ReadBank(value,bank))return false;
            for(const auto& prior:pack_.banks)if(prior.key==bank.key||prior.kind==bank.kind)return Fail(PackError::Duplicate,"banks");
            totalBytes+=bank.bytes;if(totalBytes>32u*1024u*1024u)return Fail(PackError::Capacity,"banks.bytes");
            pack_.banks.push_back(std::move(bank));
        }
        const auto* commands=Array(root,"commands",2048);
        if(!commands)return false;
        for(const auto& value:commands->children){
            CommandBinding command{};
            if(!ReadCommand(value,command))return false;
            for(const auto& prior:pack_.commands)if(prior.key==command.key||prior.encoded==command.encoded)return Fail(PackError::Duplicate,"commands");
            pack_.commands.push_back(std::move(command));
        }
        const auto* profiles=Array(root,"profiles",1024);
        if(!profiles)return false;
        for(const auto& value:profiles->children){
            ActorProfile profile{};
            if(!ReadProfile(value,profile))return false;
            for(const auto& prior:pack_.profiles)if(prior.key==profile.key||
               (prior.kind==profile.kind&&prior.id==profile.id&&prior.fileSha256==profile.fileSha256))return Fail(PackError::Duplicate,"profiles");
            pack_.profiles.push_back(std::move(profile));
        }
        const auto* equipment=Array(root,"equipment",1024);
        if(!equipment)return false;
        for(const auto& value:equipment->children){
            EquipmentBinding item{};
            if(!ReadEquipment(value,item))return false;
            for(const auto& prior:pack_.equipment)if(prior.key==item.key||prior.encoded==item.encoded)return Fail(PackError::Duplicate,"equipment");
            pack_.equipment.push_back(std::move(item));
        }
        for(const auto& command:pack_.commands){
            if(command.policy==MixPolicy::NativeExact){
                if(!pack_.profiles.empty()||!pack_.equipment.empty()||!command.effects.empty())return Fail(PackError::Policy,"native_exact");
                for(const auto& part:command.parts)if(!pack_.registry.At(part.element)->nativeBit)return Fail(PackError::Policy,"native_exact");
            }
        }
        return true;
    }
private:
    bool Fail(PackError code,std::string_view field){
        if(problem_.code==PackError::None){problem_.code=code;problem_.field=field;}
        return false;
    }
    bool Capability(unsigned bit,std::string_view field){
        return (pack_.requiredCapabilities&available_&bit)==bit||Fail(PackError::Capability,field);
    }
    bool Fields(const V& object,std::initializer_list<std::string_view> allowed){
        if(object.type!=T::Object)return Fail(PackError::Type,"object");
        for(const auto& item:object.children)
            if(std::find(allowed.begin(),allowed.end(),item.key)==allowed.end())return Fail(PackError::Schema,item.key);
        return true;
    }
    const V* Array(const V& object,const char* key,unsigned maximum,bool optional=false){
        const auto* value=object.Find(key);
        if(!value&&optional)return nullptr;
        if(!value||value->type!=T::Array){Fail(PackError::Type,key);return nullptr;}
        if(value->children.size()>maximum){Fail(PackError::Capacity,key);return nullptr;}
        return value;
    }
    bool Text(const V& object,const char* key,std::string& output,bool optional=false){
        const auto* value=object.Find(key);
        if(!value&&optional)return true;
        if(!value||value->type!=T::String)return Fail(PackError::Type,key);
        output=value->text;return true;
    }
    bool Key(const V& object,const char* key,std::string& output){
        return Text(object,key,output)&&(ValidKey(output)||Fail(PackError::Range,key));
    }
    bool Fingerprint(const V& object,const char* key,std::string& output){
        return Text(object,key,output)&&(ValidFingerprint(output)||Fail(PackError::Fingerprint,key));
    }
    bool Unsigned(const V& object,const char* key,unsigned& output,unsigned minimum,unsigned maximum,bool optional=false){
        const auto* value=object.Find(key);if(!value&&optional)return true;
        if(!value||value->type!=T::Integer)return Fail(PackError::Type,key);
        if(value->integer<minimum||value->integer>maximum)return Fail(PackError::Range,key);
        output=static_cast<unsigned>(value->integer);return true;
    }
    bool Boolean(const V& object,const char* key,bool& output,bool optional=true){
        const auto* value=object.Find(key);if(!value&&optional)return true;
        if(!value||value->type!=T::Boolean)return Fail(PackError::Type,key);
        output=value->boolean;return true;
    }
    bool Tier(const V& object,const char* key,std::int32_t& output,bool delta=false){
        const auto* value=object.Find(key);
        if(!value||value->type!=T::Integer)return Fail(PackError::Type,key);
        const auto lower=delta?-35000:-10000,upper=delta?35000:25000;
        if(value->integer<lower||value->integer>upper||value->integer%2500)return Fail(PackError::Range,key);
        output=static_cast<std::int32_t>(value->integer);return true;
    }
    bool ElementReference(const V& object,const char* key,unsigned& output){
        std::string name;if(!Text(object,key,name))return false;
        output=pack_.registry.Index(name);
        return output!=InvalidElement||Fail(PackError::Reference,name);
    }
    bool Binding(const V& value,std::string& key,unsigned& bank,unsigned& index,unsigned& encoded,std::string& hash,bool equipment){
        std::string bankKey;
        if(!Key(value,"key",key)||!Text(value,"bank",bankKey)||!Unsigned(value,"index",index,0,4095)||!Fingerprint(value,"row_sha256",hash))return false;
        bank=static_cast<unsigned>(pack_.banks.size());
        for(unsigned i=0;i<pack_.banks.size();++i)if(pack_.banks[i].key==bankKey)bank=i;
        if(bank==pack_.banks.size())return Fail(PackError::Reference,"bank");
        const auto& binding=pack_.banks[bank];
        if((binding.kind==BankKind::AutoAbility)!=equipment||binding.Row(index)==InvalidRow)return Fail(PackError::Reference,"index");
        encoded=(static_cast<unsigned>(binding.kind)<<12)|index;return true;
    }
    bool ReadBank(const V& value,PackBank& bank){
        if(!Fields(value,{"key","kind","locale","bytes","sha256","sections"})||!Key(value,"key",bank.key)||
           !Text(value,"locale",bank.locale)||!Fingerprint(value,"sha256",bank.sha256)||!Unsigned(value,"bytes",bank.bytes,20,8*1024*1024))return false;
        if(bank.locale.empty()||bank.locale.size()>16)return Fail(PackError::Range,"locale");
        for(char ch:bank.locale)if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'))return Fail(PackError::Range,"locale");
        std::string kind;if(!Text(value,"kind",kind))return false;
        if(kind=="command")bank.kind=BankKind::Command;else if(kind=="item")bank.kind=BankKind::Item;
        else if(kind=="monmagic1")bank.kind=BankKind::MonsterMagic1;else if(kind=="monmagic2")bank.kind=BankKind::MonsterMagic2;
        else if(kind=="autoability")bank.kind=BankKind::AutoAbility;else return Fail(PackError::Range,"kind");
        const auto* sections=Array(value,"sections",16);if(!sections)return false;
        if(sections->children.empty())return Fail(PackError::Range,"sections");
        const auto header=8+12*sections->children.size();
        for(const auto& row:sections->children){
            BankSection section{};
            if(!Fields(row,{"first","last","width","offset"})||!Unsigned(row,"first",section.first,0,4095)||
               !Unsigned(row,"last",section.last,section.first,4095)||!Unsigned(row,"width",section.width,RowWidth(bank.kind),RowWidth(bank.kind))||
               !Unsigned(row,"offset",section.offset,static_cast<unsigned>(header),bank.bytes))return false;
            const std::uint64_t end=std::uint64_t(section.offset)+std::uint64_t(section.last-section.first+1)*section.width;
            if(end>bank.bytes)return Fail(PackError::Range,"sections.bytes");
            for(const auto& prior:bank.sections){
                const std::uint64_t priorEnd=std::uint64_t(prior.offset)+std::uint64_t(prior.last-prior.first+1)*prior.width;
                if(!(prior.last<section.first||section.last<prior.first)||
                   !(end<=prior.offset||priorEnd<=section.offset))return Fail(PackError::Duplicate,"sections");
            }
            bank.sections.push_back(section);
        }
        return true;
    }
    bool ReadCommand(const V& value,CommandBinding& command){
        if(!Capability(ContextCapability,"commands")||!Fields(value,{"key","bank","index","row_sha256","elements","policy","spell","augment","gravity","effects"})||
           !Binding(value,command.key,command.bank,command.index,command.encoded,command.rowSha256,false)||
           !Boolean(value,"augment",command.augment)||!Boolean(value,"gravity",command.gravity))return false;
        std::string policy="highest_exposure",spell="none";
        if(!Text(value,"policy",policy,true)||!Text(value,"spell",spell,true))return false;
        if(policy=="highest_exposure")command.policy=MixPolicy::HighestExposure;
        else if(policy=="lowest_exposure")command.policy=MixPolicy::LowestExposure;
        else if(policy=="split_weighted")command.policy=MixPolicy::SplitWeighted;
        else if(policy=="native_exact")command.policy=MixPolicy::NativeExact;
        else return Fail(PackError::Policy,"policy");
        if(spell=="native_magic")command.spell=SpellClass::NativeMagic;
        else if(spell=="fury")command.spell=SpellClass::Fury;
        else if(spell!="none")return Fail(PackError::Policy,"spell");
        if(command.spell!=SpellClass::None&&!Capability(SpellCapCapability,"spell"))return false;
        if(command.spell==SpellClass::Fury&&pack_.banks[command.bank].kind!=BankKind::Command)return Fail(PackError::Policy,"fury.bank");
        if(command.gravity&&!Capability(GravityCapability,"gravity"))return false;
        const auto* parts=Array(value,"elements",ElementLimit);if(!parts)return false;
        if(!parts->children.empty()&&!Capability(AffinityCapability,"elements"))return false;
        for(const auto& part:parts->children){
            ElementPart result{};
            if(!Fields(part,{"key","weight"})||!ElementReference(part,"key",result.element)||!Unsigned(part,"weight",result.weight,1,1000))return false;
            for(const auto& prior:command.parts)if(prior.element==result.element)return Fail(PackError::Duplicate,"command.elements");
            command.parts.push_back(result);
        }
        if(value.Find("effects")){
            if(!Capability(TacticsCapability,"effects"))return false;
            const auto* effects=Array(value,"effects",16);if(!effects)return false;
            for(const auto& effect:effects->children){
                ElementEffect result{};std::string kind;
                if(!Fields(effect,{"kind","element","stacks","turns","chance_bp"})||!Text(effect,"kind",kind)||
                   !ElementReference(effect,"element",result.element)||!Unsigned(effect,"stacks",result.stacks,1,4)||
                   !Unsigned(effect,"turns",result.turns,1,255)||!Unsigned(effect,"chance_bp",result.chanceBp,0,10000))return false;
                if(kind=="imperil")result.kind=EffectKind::Imperil;else if(kind=="ward")result.kind=EffectKind::Ward;
                else if(kind=="nul")result.kind=EffectKind::Nul;else if(kind=="remove_imperil")result.kind=EffectKind::RemoveImperil;
                else if(kind=="remove_ward")result.kind=EffectKind::RemoveWard;else if(kind=="remove_nul")result.kind=EffectKind::RemoveNul;
                else if(kind=="cleanse")result.kind=EffectKind::Cleanse;else return Fail(PackError::Policy,"effect.kind");
                for(const auto& prior:command.effects)if(prior.kind==result.kind&&prior.element==result.element)return Fail(PackError::Duplicate,"effects");
                command.effects.push_back(result);
            }
        }
        return true;
    }
    bool ReadProfile(const V& value,ActorProfile& profile){
        if(!Capability(AffinityCapability,"profiles")||!Fields(value,{"key","kind","id","file_bytes","file_sha256","imperil_limit","imperil_resist_bp","affinities","gravity"})||
           !Key(value,"key",profile.key)||!Unsigned(value,"id",profile.id,0,65534)||
           !Unsigned(value,"imperil_limit",profile.imperilLimit,0,4,true)||!Unsigned(value,"imperil_resist_bp",profile.imperilResistBp,0,10000,true))return false;
        std::string kind;if(!Text(value,"kind",kind))return false;
        if(kind=="monster"){
            profile.kind=ProfileKind::Monster;
            if(!Fingerprint(value,"file_sha256",profile.fileSha256)||!Unsigned(value,"file_bytes",profile.fileBytes,0x30,8*1024*1024))return false;
        }else if(kind=="character"||kind=="aeon"){
            profile.kind=kind=="character"?ProfileKind::Character:ProfileKind::Aeon;
            if((profile.kind==ProfileKind::Character&&profile.id>7)||
               (profile.kind==ProfileKind::Aeon&&(profile.id<8||profile.id>17))||
               value.Find("file_bytes")||value.Find("file_sha256"))return Fail(PackError::Range,"profile.identity");
        }else return Fail(PackError::Range,"profile.kind");
        const auto* affinities=Array(value,"affinities",ElementLimit);if(!affinities)return false;
        for(const auto& affinity:affinities->children){
            ProfileAffinity result{};
            if(!Fields(affinity,{"key","base_bp","locked","imperil_immune","imperil_resist_bp"})||!ElementReference(affinity,"key",result.element)||
               !Tier(affinity,"base_bp",result.baseBp)||!Boolean(affinity,"locked",result.locked)||
               !Boolean(affinity,"imperil_immune",result.imperilImmune)||
               !Unsigned(affinity,"imperil_resist_bp",result.imperilResistBp,0,10000,true))return false;
            for(const auto& prior:profile.affinities)if(prior.element==result.element)return Fail(PackError::Duplicate,"profile.affinities");
            profile.affinities.push_back(result);
        }
        if(const auto* gravity=value.Find("gravity")){
            unsigned divisor=16;bool nonlethal=false;
            if(!Capability(GravityCapability,"profile.gravity")||profile.kind!=ProfileKind::Monster||
               !Fields(*gravity,{"maximum_hp_divisor","nonlethal","override_native_immunity","elemental_affinity"})||
               !Unsigned(*gravity,"maximum_hp_divisor",divisor,16,16)||!Boolean(*gravity,"nonlethal",nonlethal,false)||
               !Boolean(*gravity,"override_native_immunity",profile.gravity.overrideImmunity)||
               !Boolean(*gravity,"elemental_affinity",profile.gravity.elementalAffinity))return Fail(PackError::Policy,"profile.gravity");
            if(!nonlethal)return Fail(PackError::Policy,"profile.gravity.nonlethal");
            profile.gravity.enabled=true;
        }
        return true;
    }
    bool ReadEquipment(const V& value,EquipmentBinding& item){
        if(!Capability(EquipmentCapability,"equipment")||!Fields(value,{"key","bank","index","row_sha256","sos","deltas","kind","owners"})||
           !Binding(value,item.key,item.bank,item.index,item.encoded,item.rowSha256,true)||!Boolean(value,"sos",item.sos))return false;
        std::string kind="either";if(!Text(value,"kind",kind,true))return false;
        if(kind=="weapon")item.kind=0;else if(kind=="armor")item.kind=1;
        else if(kind!="either")return Fail(PackError::Range,"equipment.kind");
        if(value.Find("owners")){
            const auto* owners=Array(value,"owners",18);if(!owners)return false;
            if(owners->children.empty())return Fail(PackError::Range,"equipment.owners");
            item.owners=0;
            for(const auto& owner:owners->children){
                if(owner.type!=T::Integer||owner.integer<0||owner.integer>17)return Fail(PackError::Range,"equipment.owners");
                const unsigned bit=1u<<static_cast<unsigned>(owner.integer);
                if(item.owners&bit)return Fail(PackError::Duplicate,"equipment.owners");
                item.owners|=bit;
            }
        }
        const auto* deltas=Array(value,"deltas",ElementLimit);if(!deltas)return false;
        if(deltas->children.empty())return Fail(PackError::Range,"deltas");
        for(const auto& delta:deltas->children){
            EquipmentDelta result{};
            if(!Fields(delta,{"key","delta_bp"})||!ElementReference(delta,"key",result.element)||!Tier(delta,"delta_bp",result.deltaBp,true))return false;
            // Native auto-ability masks are already aggregated into the baseline.
            // V1 adds only external deltas; replacing native masks needs another capability.
            if(pack_.registry.At(result.element)->nativeBit)return Fail(PackError::Policy,"equipment.native_delta");
            for(const auto& prior:item.deltas)if(prior.element==result.element)return Fail(PackError::Duplicate,"equipment.deltas");
            item.deltas.push_back(result);
        }
        return true;
    }
    Pack& pack_;PackProblem& problem_;unsigned available_;
};
} // namespace PackDetail

inline bool LoadPack(std::string_view input,unsigned availableCapabilities,Pack& output,PackProblem& problem) noexcept {
    problem={};
    try{
        Json::Value root;
        if(!Json::Parse(input,root,problem.json)){problem.code=PackError::Json;return false;}
        Pack candidate;PackDetail::Reader reader(candidate,problem,availableCapabilities);
        if(!reader.Read(root))return false;
        output=std::move(candidate);return true;
    }catch(const std::bad_alloc&){problem.code=PackError::Capacity;return false;}
     catch(const std::length_error&){problem.code=PackError::Capacity;return false;}
}

} // namespace FfxHooks::ElementalDominion
