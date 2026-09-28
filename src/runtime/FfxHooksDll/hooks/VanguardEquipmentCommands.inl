// Jarvis-HOOK: low16 is a packed native command; high16 is OD override+1.
// High16 zero retains the native fee. Zero disables the binding. No saved learning.
extern std::atomic<unsigned> actionEpoch;
struct BindingGrantMemory {std::uintptr_t actor=0;std::array<std::uint32_t,10> words{};};
BindingGrantMemory bindingGrants[18]{};unsigned bindingEpoch=UINT_MAX;
bool BindingsSnapshot(BindingState& out,MappingState* mappingOut=nullptr,int overrideEffect=-1,unsigned overridePacked=0){
    out={};MappingState mapping{};std::vector<Byte> abilityBytes,commandBytes;CommandTableView table{};
    std::lock_guard<std::recursive_mutex> lock(mappingMutex);
    const bool mapReady=MappingSnapshot(mapping,abilityBytes);bool commandsReady=ReadCommandTable(table);
    if(commandsReady){commandBytes.resize(20+(table.last+1)*96);commandsReady=Copy(commandBytes.data(),reinterpret_cast<void*>(table.rows-20),commandBytes.size());}
    out.stamp=mapReady?mapping.stamp:1469598103934665603ull;
    if(commandsReady)for(Byte b:commandBytes){out.stamp^=b;out.stamp*=1099511628211ull;}
    for(unsigned i=0;i<AbilityCount;++i){char key[96]{};std::snprintf(key,sizeof(key),"vanguard_commands.%s",Abilities[i].key);
        const auto value=Config::ReadIntExact(key,0,0x100FFFF);auto& entry=out.entries[i];bool invalid=value.state==Config::IntReadState::Invalid;
        entry.packed=value.state==Config::IntReadState::Valid?static_cast<unsigned>(value.value):0;
        if(overrideEffect==static_cast<int>(i)){entry.packed=overridePacked;invalid=false;}
        if(invalid){entry.code=BindingCode::InvalidConfiguration;continue;}if(!entry.packed)continue;
        entry.command=entry.packed&0xFFFF;const unsigned high=entry.packed>>16;entry.cost=high?high-1:256;
        if(entry.command<0x3000||entry.command>0x313F||high>256){entry.code=BindingCode::InvalidConfiguration;continue;}
        if(!commandsReady||!mapReady){entry.code=BindingCode::NoKernel;continue;}
        if(mapping.codes[i]!=MappingCode::Valid){entry.code=BindingCode::UnverifiedAbility;continue;}
        const unsigned id=entry.command&0xFFF;if(id>table.last){entry.code=BindingCode::NotExecutable;continue;}
        const auto* row=commandBytes.data()+20+96*id;const unsigned menu=row[0x16]&0xF8;
        if(menu==8||menu==16||!(Read<std::uint32_t>(row+0x1C)&2)||(row[0x19]>=18&&row[0x19]!=255)){entry.code=BindingCode::NotExecutable;continue;}
        entry.code=BindingCode::Valid;
    }
    for(unsigned i=0;i<AbilityCount;++i)for(unsigned j=0;j<i;++j)
        if(out.entries[i].command>=0x3000&&out.entries[i].command==out.entries[j].command)out.entries[i].code=out.entries[j].code=BindingCode::DuplicateCommand;
    for(const auto& entry:out.entries){out.stamp^=entry.packed;out.stamp*=1099511628211ull;out.stamp^=static_cast<unsigned>(entry.code);out.stamp*=1099511628211ull;}
    if(!out.stamp)out.stamp=1;if(mappingOut)*mappingOut=mapping;return true;
}
struct BindingContext {BindingState state{};Effects equipped{};Byte* actor=nullptr;unsigned owner=18;bool ready=false;};
struct BindingAccess {bool configured=false,granted=false,previouslyGranted=false;unsigned cost=256;};
void LoadBindingContext(unsigned owner,BindingContext& context) noexcept {
    if(!On(Feature::EquipmentCommands)||!NativeCostActor(owner,context.actor))return;
    try{
        const auto epoch=actionEpoch.load();if(epoch!=bindingEpoch){for(auto& record:bindingGrants)record={};bindingEpoch=epoch;}
        auto& memory=bindingGrants[owner];if(memory.actor!=reinterpret_cast<std::uintptr_t>(context.actor))memory={reinterpret_cast<std::uintptr_t>(context.actor),{}};
        MappingState mapping{};if(!BindingsSnapshot(context.state,&mapping))return;
        // Release mapping ownership before reading Workshop's logical fifth.
        context.equipped=Equipped(context.actor,mapping,false);context.owner=owner;context.ready=true;
    }catch(...){context.ready=false;running=false;}
}
BindingAccess FindBinding(const BindingContext& context,unsigned id) noexcept {
    BindingAccess result{};if(!context.ready||id>=320||context.owner>=18)return result;
    auto& memory=bindingGrants[context.owner];const auto bit=1u<<(id%32);result.previouslyGranted=(memory.words[id/32]&bit)!=0;
    for(unsigned i=0;i<AbilityCount;++i){const auto& entry=context.state.entries[i];if(entry.command!=0x3000+id)continue;
        result.configured=true;
        CommandTableView table{};Byte command[96]{};
        if(entry.code==BindingCode::Valid&&context.equipped[i]&&ReadCommandTable(table)&&id<=table.last&&
           Copy(command,reinterpret_cast<void*>(table.rows+96*id),sizeof(command))&&
           (command[0x19]==255||command[0x19]==context.owner)){
            result.granted=true;result.cost=entry.cost;memory.words[id/32]|=bit;
        }
    }
    return result;
}
bool AllowedCommand(const Byte* actor,unsigned id,const BindingAccess& binding) noexcept {
    if(id>=320)return false;const auto mask=static_cast<std::uint16_t>(1u<<(id%16));
    return !(Read<std::uint16_t>(actor+0x690+(id/16)*2)&mask)&&((Read<std::uint16_t>(actor+0x664+(id/16)*2)&mask)||binding.granted);
}
using AvailabilityFn=int(__cdecl*)(unsigned,unsigned);
int __cdecl AvailabilityShim(unsigned owner,unsigned command){
    const int native=reinterpret_cast<AvailabilityFn>(originals[AvailabilityHook])(owner,command);
    const bool playerId=command<320||(command>=0x3000&&command<=0x313F);
    if(!Enter()||!On(Feature::EquipmentCommands)||(native&1)||!playerId)return native;
    const unsigned id=command&0xFFF;BindingContext context{};LoadBindingContext(owner,context);
    return FindBinding(context,id).granted?native|1:native;
}
