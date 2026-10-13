// Experimental adapter draft. Normal builds retain the proven elemental path.
// Enable only after native RNG/command signatures, hit ownership, Rite writes
// and lifecycle fixtures are complete. This is not an installed feature.
#if defined(FFXHOOKS_EXPERIMENTAL_ELEMENTAL_PROGRESS)
// Included after the shared native action ledger; all mutation is battle-thread
// private state. Damage/result/retirement and genuine CTB edges remain native.
struct ProgressiveAction {
    ActionToken native{};Progressive::ActionKey key{};
    std::array<unsigned,ActorCount> consumed{};
};
std::array<ProgressiveAction,MaximumActions> progressiveActions{};
Progressive::ActorKey ProgressiveKey(ActorToken key) noexcept {return {key.generation,key.incarnation,key.slot};}
bool ProgressiveCurrent() noexcept {
    if(!CurrentData()||!progressiveDataReady.load()||(!options.progression&&!options.omegaRite))return false;
    std::uint32_t bank=0;std::uint16_t size=0;
    return Read(module+ExecutableProfile::Rva<0xD2A944>(),bank)&&bank==progressiveBank&&
        Read(module+ExecutableProfile::Rva<0xD2A970>(),size)&&size==progressiveBytes;
}
void PrepareProgressiveData() noexcept {
    if(!options.progression&&!options.omegaRite)return;
    std::uint32_t bank=0;std::uint16_t size=0;
    if(!Read(module+ExecutableProfile::Rva<0xD2A944>(),bank)||!Read(module+ExecutableProfile::Rva<0xD2A970>(),size))return;
    if(bank==progressiveBank&&size==progressiveBytes&&progressiveDataGeneration==generation.load())return;
    progressiveDataReady=false;progressiveBank=bank;progressiveBytes=size;progressiveDataGeneration=generation.load();
    try {
        if(bank<0x10000||size<20||bank>UINT32_MAX-size)return;
        std::vector<Byte> bytes(size);
        if(!Copy(bytes.data(),reinterpret_cast<void*>(bank),size)||!Progressive::Equipment::Validate(bytes.data(),bytes.size()))return;
        for(unsigned i=0;i<7;++i)std::memcpy(progressiveRows[i].data(),bytes.data()+20+108*Progressive::Equipment::Ids[i],108);
        progressiveDataReady=true;
        if(logger)logger("[ffx-hooks] Spira elemental progression: native187-row Touch/Capture data admitted\n");
    }catch(...){progressiveDataReady=false;}
}
bool ProgressiveRowsCurrent() noexcept {
    if(!ProgressiveCurrent())return false;
    for(unsigned i=0;i<7;++i){std::array<Byte,108> row{};
        if(!Copy(row.data(),reinterpret_cast<void*>(std::uintptr_t(progressiveBank)+20+108*Progressive::Equipment::Ids[i]),row.size())||row!=progressiveRows[i])return false;}
    return true;
}
void ProgressiveActor(ActorToken key,bool spawn) noexcept {
    if(!key.Valid())return;
    if(spawn)progressiveState.Spawn(ProgressiveKey(key));else progressiveState.Remove(ProgressiveKey(key));
}
void ProgressiveReset() noexcept {progressiveActions={};progressiveState.Start(statusState.Generation());}
unsigned RegistryMask(unsigned native) noexcept {
    unsigned mask=0;for(unsigned i=0;i<pack.registry.Size();++i)if(pack.registry.At(i)->nativeBit&native)mask|=1u<<i;return mask;
}
bool ProgressiveBases(std::array<Progressive::Base,ActorCount>& out) noexcept {
    out={};
    for(unsigned slot=0;slot<ActorCount;++slot){auto& value=out[slot];value.exposure.fill(10000);
        if(!statusActors[slot].token.Valid())continue;
        std::array<AffinityValue,ElementLimit> values{};
        if(!Affinities(slot,reinterpret_cast<void*>(statusActors[slot].address),values,nullptr,true,nullptr,nullptr,false))return false;
        for(unsigned i=0;i<pack.registry.Size();++i){const auto rule=statusState.View(statusActors[slot].token,i);
            if(!rule.valid)return false;value.exposure[i]=values[i].value;value.resistance[i]=rule.imperilResistanceBp;
            if(values[i].locked||rule.locked)value.locked|=1u<<i;
            if(rule.imperilImmune)value.immune|=1u<<i;
        }
    }
    return true;
}
unsigned ProgressiveEquipment(unsigned owner,bool ward) noexcept {
    if(owner>=18||!ProgressiveRowsCurrent())return 0;
    EquipmentEffects::View view{};
    if(!EquipmentEffects::Capture(module,owner,reinterpret_cast<void*>(statusActors[owner].address),Copy,view))return 0;
    unsigned native=0;
    for(unsigned i=0;i<view.count;++i){const auto& entry=view.entries[i];
        if(entry.kind==(ward?1u:0u))native|=ward?Progressive::Equipment::Ward(entry.word):Progressive::Equipment::Touch(entry.word);}
    unsigned result=RegistryMask(native);
    if(ward){Arcana::Elemental::Snapshot card{};
        if(Arcana::Elemental::Read(owner,card))for(unsigned bit:{Arcana::Elemental::Poison,Arcana::Elemental::Gravity})if(card.wards&bit){
            const auto element=CardElement(bit);if(element<pack.registry.Size())result|=1u<<element;}}
    return result;
}
ProgressiveAction* ProgressiveFind(ActionToken token) noexcept {
    if(!token.Valid()||token.slot>=progressiveActions.size())return nullptr;
    auto& action=progressiveActions[token.slot];return SameStatusAction(action.native,token)?&action:nullptr;
}
void ProgressiveCancel(ActionToken token) noexcept {
    if(auto* action=ProgressiveFind(token)){progressiveState.Cancel(action->key);*action={};}
}
void ProgressiveBegin(ActionToken token,unsigned source,unsigned parent) noexcept {
    if(!token.Valid()||token.slot>=progressiveActions.size()||source>=ActorCount||!ProgressiveRowsCurrent())return;
    std::array<Progressive::Base,ActorCount> bases{};if(!ProgressiveBases(bases))return;
    auto& action=progressiveActions[token.slot];action={};action.native=token;
    action.key=progressiveState.Begin(ProgressiveKey(statusActors[source].token),parent,
        options.progression?ProgressiveEquipment(source,false):0,bases);
}
bool ProgressiveExposure(unsigned slot,unsigned element,const ActionToken* token,bool live,int& value) noexcept {
    if(!options.progression||!ProgressiveCurrent()||slot>=ActorCount||element>=pack.registry.Size())return false;
    const auto key=ProgressiveKey(statusActors[slot].token);
    if(token){if(const auto* action=ProgressiveFind(*token))return progressiveState.SnapshotExposure(action->key,key,element,value);return false;}
    if(live){value=progressiveState.Live(key,element,value);return true;}return false;
}
struct NativeProgressiveCommand {std::array<Byte,108> bytes{};const void* address=nullptr;unsigned width=0,id=0;};
bool ReadProgressiveCommand(unsigned id,NativeProgressiveCommand& out) noexcept {
    out={};const unsigned kind=id?id>>12:3,index=id&4095;
    const auto bank=kind==3?BankKind::Command:kind==2?BankKind::Item:kind==4?BankKind::MonsterMagic1:kind==6?BankKind::MonsterMagic2:BankKind::AutoAbility;
    if(bank==BankKind::AutoAbility)return false;
    const auto* location=Location(bank);std::uint32_t pointer=0;std::array<Byte,20> header{};
    if(!location||!Read(module+location->global,pointer)||pointer<0x10000||pointer>UINT32_MAX-0x100000||
       !Copy(header.data(),reinterpret_cast<void*>(pointer),header.size()))return false;
    const auto word=[](const Byte* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    const unsigned width=kind==4||kind==6?92u:96u,last=word(header.data()+10),length=word(header.data()+14);
    unsigned offset=0;std::memcpy(&offset,header.data()+16,4);
    if(word(header.data())!=1||word(header.data()+8)||last>4095||index>last||word(header.data()+12)!=width||
       length!=(last+1)*width||offset!=20||pointer>UINT32_MAX-offset-length)return false;
    out.address=reinterpret_cast<void*>(std::uintptr_t(pointer)+offset+index*width);out.width=width;out.id=id;
    return Copy(out.bytes.data(),out.address,width);
}
bool ProgressiveOffense(const NativeProgressiveCommand& command) noexcept {
    const auto& row=command.bytes;
    return (row[0x23]&7)==1&&!(row[0x20]&0x10)&&row[0x28]!=0;
}
unsigned ProgressiveElements(unsigned source,const NativeProgressiveCommand& command) noexcept {
    unsigned mask=command.bytes[0x2D];
    if(command.bytes[0x1E]&4){Byte weapon=0;if(!Read(statusActors[source].address+0x5D9,weapon))return 0;mask=weapon;
        if(source==8&&AeroEquipmentBridge::Valefor())mask|=0x40;}
    return RegistryMask(mask);
}
void* EnterProgressiveNative(const Bus::DamageCall& call,const void*& forward) noexcept {
    if(!ProgressiveCurrent())return EnterCardWeapon(call,forward);
    NativeProgressiveCommand command;if(!ReadProgressiveCommand(call.commandId,command)||call.command!=command.address||!ProgressiveOffense(command))return EnterCardWeapon(call,forward);
    auto* action=TrackAction(call.user);if(!action||!ProgressiveFind(action->token))return nullptr;
    auto& frame=frames[Bus::currentDamage->depth-1];frame={};frame.generation=generation.load();frame.action=action->token;
    frame.progressiveNative=true;frame.nativeWidth=command.width;std::memcpy(frame.cardRow.data(),command.bytes.data(),command.width);
    frame.command=command.bytes;
    if(options.progression){
        std::array<AffinityValue,ElementLimit> values{};
        if(!Affinities(call.target,call.targetActor,values,&action->token,false,&frame.cardTarget,&frame.cardTargetProvider))return nullptr;
        const unsigned mask=ProgressiveElements(call.user,command);
        for(unsigned i=0;i<pack.registry.Size();++i)if(mask&(1u<<i))AddHitElement(frame,values,i,1);
        if(command.width==96)AddCardStrikes(frame,call,command.bytes.data(),values);
        frame.core=true;frame.command[0x2D]=static_cast<Byte>(frame.nativeMask);forward=frame.command.data();
    }
    return &frame;
}
bool CurrentProgressiveHit(const void* raw,const Bus::DamageCall& call) noexcept {
    const auto& frame=*static_cast<const HitFrame*>(raw);NativeProgressiveCommand current;
    return ProgressiveCurrent()&&ReadProgressiveCommand(call.commandId,current)&&current.address==call.command&&current.width==frame.nativeWidth&&
        !std::memcmp(current.bytes.data(),frame.cardRow.data(),current.width)&&CurrentCardHit(frame,call);
}
bool ProgressiveRandom(void* raw,unsigned& output) noexcept {
    if(!ProgressiveCurrent())return false;
    const auto source=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(raw));
    __try {
        const auto stream=reinterpret_cast<unsigned(__cdecl*)(unsigned,unsigned)>(module+ExecutableProfile::Rva<0x38D2D0>())(source,0);
        output=reinterpret_cast<unsigned(__cdecl*)(unsigned)>(module+ExecutableProfile::Rva<0x398900>())(stream);return output<=0x7FFFFFFF;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ProgressiveResultCommand(const SharedAction::ResultCall& call,const NativeAction& action,NativeProgressiveCommand& command) noexcept {
    if(call.sub>=action.row[3])return false;
    const auto at=8+16*call.sub;unsigned id=unsigned(action.row[at])|(unsigned(action.row[at+1])<<8);
    if(call.source==4&&id>=0x3074&&id<=0x3077){
        // Native MapCommandToReelIndex reads the current parent at+F5C. Result
        // records are shared: their category cannot prove Aurochs ownership.
        std::uint16_t parent=0,resolved=0;Byte preview=1;
        if(!Read(statusActors[4].address+0xF5C,parent)||parent!=id||!Read(statusActors[4].address+0xD26,preview)||preview||
           !Read(module+0xD2BE80+4*(id-0x3074)+2,resolved)||!((resolved>=0x30EF&&resolved<=0x30FE)||resolved==0x312E))return false;
        id=resolved;
    }
    return ReadProgressiveCommand(id,command)&&ProgressiveOffense(command);
}
void ProgressiveResults(ActionToken token,const SharedAction::ResultCall& call,unsigned group,unsigned first) noexcept {
    if(!options.progression||!ProgressiveRowsCurrent()||group>=2||call.source>=ActorCount||call.target>=ActorCount)return;
    auto* action=ProgressiveFind(token);if(!action)return;
    NativeProgressiveCommand command;if(!ProgressiveResultCommand(call,nativeActions[call.source],command))return;
    const unsigned elements=ProgressiveElements(call.source,command);
    const auto key=ProgressiveKey(statusActors[call.target].token);
    const auto address=statusActors[call.target].address+0x774+728*group;Byte count=0;
    if(!Read(address,count)||count>16||first>count)return;
    for(unsigned hit=first;hit<count;++hit){const unsigned bit=1u<<(group*16+hit);
        if(action->consumed[call.target]&bit)continue;
        std::array<Byte,44> info{};if(!Copy(info.data(),reinterpret_cast<void*>(address+24+44*hit),info.size()))return;
        action->consumed[call.target]|=bit;
        std::uint16_t flags=0;std::memcpy(&flags,info.data()+0x18,2);int amount=0;std::memcpy(&amount,info.data()+0x20,4);
        if(!(flags&1))continue;
        bool absorbed=amount<0;
        if(elements){int largest=-10000;for(unsigned e=0;e<pack.registry.Size();++e)if(elements&(1u<<e))largest=(std::max)(largest,progressiveState.Exposure(action->key,key,e));absorbed=absorbed||largest<0;}
        progressiveState.Hit(action->key,key,{info[1]==0,(call.source<18)!=(call.target<18),absorbed,(command.bytes[0x20]&3)==1,elements},
            {reinterpret_cast<void*>(static_cast<std::uintptr_t>(call.source)),ProgressiveRandom});
    }
}
void ProgressiveFinish(ActionToken token,bool complete) noexcept {
    auto* action=ProgressiveFind(token);if(!action)return;
    std::array<Progressive::Base,ActorCount> current{};
    if(complete&&ProgressiveCurrent()&&ProgressiveBases(current))progressiveState.Finish(action->key,current);
    else progressiveState.Cancel(action->key);
    *action={};
}
void ProgressiveTurn(unsigned actor,void* native,std::uint32_t sequence) noexcept {
    if(!options.progression||!ProgressiveRowsCurrent()||actor>=ActorCount||!Actor(actor,native))return;
    SyncStatusActors();if(!statusActors[actor].token.Valid())return;
    std::array<Progressive::Base,ActorCount> bases{};if(!ProgressiveBases(bases))return;
    progressiveState.Turn(ProgressiveKey(statusActors[actor].token),sequence,ProgressiveEquipment(actor,true),bases[actor]);
}
const SharedTurn::Observer progressiveTurnObserver{ProgressiveTurn};
bool StartProgressiveTurns(){
    return !options.progression||(SharedTurn::Start(module)&&SharedTurn::Register(SharedTurn::Consumer::Elemental,&progressiveTurnObserver));
}
void StopProgressiveTurns() noexcept {SharedTurn::Unregister(SharedTurn::Consumer::Elemental,&progressiveTurnObserver);}

#else
bool ProgressiveCurrent() noexcept {return false;}
void PrepareProgressiveData() noexcept {}
void ProgressiveActor(ActorToken,bool) noexcept {}
void ProgressiveReset() noexcept {}
void ProgressiveCancel(ActionToken) noexcept {}
void ProgressiveBegin(ActionToken,unsigned,unsigned) noexcept {}
bool ProgressiveExposure(unsigned,unsigned,const ActionToken*,bool,int&) noexcept {return false;}
void* EnterProgressiveNative(const Bus::DamageCall& call,const void*& forward) noexcept {return EnterCardWeapon(call,forward);}
bool CurrentProgressiveHit(const void*,const Bus::DamageCall&) noexcept {return false;}
void ProgressiveResults(ActionToken,const SharedAction::ResultCall&,unsigned,unsigned) noexcept {}
void ProgressiveFinish(ActionToken,bool) noexcept {}
bool StartProgressiveTurns(){return true;}
void StopProgressiveTurns() noexcept {}
#endif
