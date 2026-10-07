#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: one immutable Energy view per admitted native queued action.
// Capture runs at native selection before the existing scalar resource debit.
// It never stages, subtracts, refunds or expands MP/Overdrive resources.
bool ActionRow(unsigned,Byte[72],unsigned*) noexcept;
bool EnergyRequested() noexcept {
    return On(Feature::EnergyBoost)||On(Feature::EnergyBurst)||
           On(Feature::EnergyWall)||On(Feature::EnergyBarrier);
}
struct EnergyKey {
    std::uintptr_t actor=0;
    std::uint32_t epoch=0;
    std::uint16_t identity=0,commands[8]{};
    Byte count=0,kind=0;
};
struct EnergyActorView {
    std::uintptr_t actor=0;
    std::uint16_t identity=0;
    Byte charge=0,maximum=0;
    bool boost=false,burst=false,wall=false,barrier=false;
};
struct EnergyActionView {
    EnergyKey key{};
    std::uint64_t serial=0,proof=0;
    EnergyActorView actors[31]{};
    bool invalid=false;
};
std::atomic<std::uint32_t> energyEpoch{0};
std::uint64_t energySerial=0;
EnergyActionView energyActions[31]{};
void InvalidateEnergyActions() noexcept {
    if(energyEpoch.fetch_add(1)==UINT32_MAX)running=false;
}
bool EnergyActionKey(unsigned owner,EnergyKey& key,unsigned* index=nullptr,Byte* cursor=nullptr) noexcept {
    key={};Byte row[72]{};
    if(owner>=31||!energyEpoch.load()||!ActionRow(owner,row,index))return false;
    auto* actor=Actor(owner);
    if(!actor||!Read<Byte>(actor+0xDC8))return false;
    key.actor=reinterpret_cast<std::uintptr_t>(actor);
    key.epoch=energyEpoch.load();key.identity=Read<std::uint16_t>(actor+0xE);
    key.count=row[3];key.kind=row[1];if(cursor)*cursor=row[2];
    for(unsigned i=0;i<key.count;++i){
        key.commands[2*i]=static_cast<std::uint16_t>(Word(row+8+16*i));
        key.commands[2*i+1]=static_cast<std::uint16_t>(Word(row+10+16*i));
    }
    return true;
}
bool SameEnergyKey(const EnergyKey& a,const EnergyKey& b) noexcept {
    return a.actor&&a.actor==b.actor&&a.epoch&&a.epoch==b.epoch&&a.identity==b.identity&&
        a.count==b.count&&a.kind==b.kind&&!std::memcmp(a.commands,b.commands,sizeof(a.commands));
}
void CaptureEnergySelection(unsigned owner,Byte* actor,const Byte* source,unsigned sub) noexcept {
    if(!Enter()||!EnergyRequested()||owner>=31||Actor(owner)!=actor||!source)return;
    try {
        EnergyKey key{};Byte cursor=0,row[72]{};
        if(!EnergyActionKey(owner,key,nullptr,&cursor)||cursor>=key.count||sub>=key.count||
           !Copy(row,source,sizeof(row))||row[0]!=owner||row[1]!=key.kind||row[3]!=key.count)return;
        for(unsigned i=0;i<key.count;++i)
            if(Word(row+8+16*i)!=key.commands[2*i]||Word(row+10+16*i)!=key.commands[2*i+1])return;
        auto& current=energyActions[owner];
        if(current.serial&&SameEnergyKey(current.key,key))return;
        current={};
        // An unobserved earlier subaction cannot manufacture a pre-debit quote.
        if(sub!=0)return;
        if(energySerial==UINT64_MAX){running=false;return;}
        EnergyActionView next{};next.key=key;next.serial=++energySerial;
        MappingState mapping{};std::vector<Byte> bytes;bool valid=false;
        {std::lock_guard<std::recursive_mutex> lock(mappingMutex);valid=MappingSnapshot(mapping,bytes);}
        next.proof=valid?mapping.stamp:0;next.invalid=!next.proof;
        // Snapshot defensive thresholds as well: native OD gained from an
        // earlier hit must not switch Wall/Barrier halfway through this action.
        // Do not retain mappingMutex while consulting Workshop's fifth slot.
        if(valid)for(unsigned id=0;id<31;++id){
            auto* unit=Actor(id);if(!unit||!Read<Byte>(unit+0xDC8))continue;
            auto& view=next.actors[id];const auto effects=Equipped(unit,mapping);
            view.actor=reinterpret_cast<std::uintptr_t>(unit);view.identity=Read<std::uint16_t>(unit+0xE);
            view.charge=Read<Byte>(unit+0x5BC);view.maximum=Read<Byte>(unit+0x5BD);
            view.boost=effects[1];view.burst=effects[2];view.wall=effects[11];view.barrier=effects[12];
        }
        if(running.load()&&energyEpoch.load()==key.epoch)current=next;
    }catch(...){running=false;if(logger)logger("[ffx-hooks] Vanguard: energy action snapshot failed; admission closed\n");}
}
struct EnergyValues {
    unsigned sourceCharge=0,sourceMaximum=0,targetCharge=0,targetMaximum=0;
    bool boost=false,burst=false,wall=false,barrier=false;
};
void ResolveEnergyAction(unsigned source,unsigned target,std::uint64_t proof,EnergyValues& values) noexcept {
    if(!EnergyRequested()||source>=31||target>=31)return;
    EnergyKey key{};Byte cursor=0;
    if(!EnergyActionKey(source,key,nullptr,&cursor)||cursor>=key.count)return;
    auto& action=energyActions[source];
    if(!action.serial||!SameEnergyKey(action.key,key))return;
    if(!proof||proof!=action.proof)action.invalid=true;
    values.boost=values.burst=values.wall=values.barrier=false;
    if(action.invalid)return;
    const auto& attacker=action.actors[source];
    if(attacker.actor==key.actor&&attacker.identity==key.identity){
        values.sourceCharge=attacker.charge;values.sourceMaximum=attacker.maximum;
        values.boost=attacker.boost;values.burst=attacker.burst;
    }
    const auto& defender=action.actors[target];auto* unit=Actor(target);
    if(unit&&defender.actor==reinterpret_cast<std::uintptr_t>(unit)&&
       defender.identity==Read<std::uint16_t>(unit+0xE)&&Read<Byte>(unit+0xDC8)){
        values.targetCharge=defender.charge;values.targetMaximum=defender.maximum;
        values.wall=defender.wall;values.barrier=defender.barrier;
    }
}
int CallEnergyFinishOriginal(unsigned owner,unsigned index,unsigned preserve){
    EnergyKey key{};unsigned active=255;std::uint64_t serial=0;int count=0;
    if(Enter()&&EnergyRequested()&&owner<31&&EnergyActionKey(owner,key,&active)&&active==index){
        const auto& action=energyActions[owner];
        if(SameEnergyKey(action.key,key)){
            serial=action.serial;count=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())));
        }
    }
    using NativeFinish=int(__cdecl*)(unsigned,unsigned,unsigned);
    const int result=reinterpret_cast<NativeFinish>(originals[FinishHook])(owner,index,preserve);
    // This brackets the actual queue removal, before Follow Up can append new
    // work. Cancellation and completion both retire their old energy view.
    if(serial&&result==1&&count>0&&energyEpoch.load()==key.epoch&&
       Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2BDE1>())))==count-1&&
       energyActions[owner].serial==serial)energyActions[owner]={};
    return result;
}
