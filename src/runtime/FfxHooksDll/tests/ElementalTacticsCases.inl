// Jarvis-HOOK: the real result entry, native HP writer and native queue removal
// drive status application/expiration. Only the graphical result suffix is replaced.
#include "../hooks/SharedActionRuntime.h"
#include "../hooks/SharedBattleRuntime.h"
static unsigned char* tacticsActors=nullptr;
static std::string TacticsPack(const std::vector<unsigned char>& bank){
    std::string json=CorePack(bank);
    const std::string capability="\"mod007.context.v1\"";
    json.replace(json.find(capability),capability.size(),capability+",\"mod007.tactics.v1\"");
    const char* kinds[]={"imperil","ward","cleanse","nul","remove_ward","remove_nul"};
    std::string commands;
    for(unsigned i=95;i<=100;++i){
        commands+=",{\"key\":\"tactics.test"+std::to_string(i)+"\",\"bank\":\"table.command\",\"index\":"+
            std::to_string(i)+",\"row_sha256\":\""+Hash(bank.data()+20+96*i,96)+
            "\",\"elements\":[{\"key\":\"tests.e8\",\"weight\":1}],\"effects\":[{\"kind\":\""+
            kinds[i-95]+"\",\"element\":\"tests.e8\",\"stacks\":1,\"turns\":3,\"chance_bp\":10000}]}";
    }
    const auto at=json.find("],\"profiles\"");json.insert(at,commands);
    const std::string before="{\"key\":\"tests.e8\",\"base_bp\":15000}";
    json.replace(json.find(before),before.size(),"{\"key\":\"tests.e8\",\"base_bp\":10000}");
    return json;
}
static bool TacticsPatch(std::uintptr_t at,void* to){
    unsigned char jump[5]={0xE9};const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to)-at-5);
    std::memcpy(jump+1,&delta,4);DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(at),jump,5);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),5);
    return VirtualProtect(reinterpret_cast<void*>(at),5,old,&ignored)!=FALSE;
}
static int __cdecl TacticsResult(unsigned source,unsigned sub,unsigned target,int*,void*){
    auto* actor=tacticsActors+target*0xF90;auto* group=actor+0x774;
    if(group[0]>=group[1]||group[2]!=source||group[3]!=sub)return 0;
    auto* info=group+24+44*group[0];
    if(!info[1]){
        int damage=0;std::memcpy(&damage,info+32,4);
        using Hp=int(__cdecl*)(unsigned,unsigned char*,int,int,int,int,int);
        reinterpret_cast<Hp>(coreImage+0x38E2F0)(target,actor,damage,0,0,0,0);
    }
    ++group[0];return 2;
}
static __declspec(naked) void TacticsResultSuffix(){
    __asm {add esp,4}
    __asm {pop edi}
    __asm {pop esi}
    __asm {pop ebx}
    __asm {mov esp,ebp}
    __asm {pop ebp}
    __asm {jmp TacticsResult}
}
static unsigned nativeActorInitializations=0;
static bool nativeActorWasCleared=false;
static int __cdecl TacticsActorRebuilt(unsigned mode,unsigned owner){
    if(mode||owner>=31)return 0;
    ++nativeActorInitializations;auto* actor=tacticsActors+owner*0xF90;
    int hp=-1;std::memcpy(&hp,actor+0x5D0,4);nativeActorWasCleared=hp==0;
    W32(actor+0x594,100000);W32(actor+0x5D0,100000);
    actor[0xDC8]=actor[0xDC9]=1;
    return 0x507;
}
static __declspec(naked) void TacticsActorSuffix(){
    __asm {add esp,12}
    __asm {pop edi}
    __asm {pop esi}
    __asm {pop ebx}
    __asm {mov esp,ebp}
    __asm {pop ebp}
    __asm {jmp TacticsActorRebuilt}
}
static void TacticsCases(std::uintptr_t base,std::vector<unsigned char>& actors,std::vector<unsigned char>& bank){
    coreImage=base;tacticsActors=actors.data();amount=100;
    W::DamageProducerForTests(reinterpret_cast<void*>(&CoreEndpoint));
    for(unsigned i=0;i<31;++i){auto* actor=actors.data()+i*0xF90;actor[0xDC8]=actor[0xDC9]=1;
        W32(actor+0x594,100000);W32(actor+0x5D0,100000);actor[0xDE5]=255;}
    std::array<unsigned char,14> scene{};unsigned char variant=0;
    const auto scenePointer=reinterpret_cast<std::uintptr_t>(scene.data()),variantPointer=reinterpret_cast<std::uintptr_t>(&variant);
    std::memcpy(reinterpret_cast<void*>(base+0xD2A9C8),&scenePointer,4);
    std::memcpy(reinterpret_cast<void*>(base+0xD2A9FC),&variantPointer,4);
    W16(reinterpret_cast<unsigned char*>(base+0xD2C256),0);
    Check(TacticsPatch(base+0x38F0C0,reinterpret_cast<void*>(&TacticsResultSuffix)),"the graphical result suffix is isolated after native profile admission");
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    const auto result=reinterpret_cast<FfxHooks::SharedAction::ResultFunction>(base+0x38F0B0);
    const auto finish=reinterpret_cast<FfxHooks::SharedAction::FinishFunction>(base+0x3B0870);
    auto* queue=reinterpret_cast<unsigned char*>(base+0xD2AC70);
    const auto begin=[&](unsigned owner,unsigned id,unsigned count,bool counter=false){
        std::memset(queue,0,72);queue[0]=static_cast<unsigned char>(owner);queue[1]=counter?1:0;queue[3]=static_cast<unsigned char>(count);
        for(unsigned i=0;i<count;++i){W16(queue+8+16*i,0x3000+id);W16(queue+10+16*i,255);W32(queue+16+16*i,2);}
        *reinterpret_cast<unsigned char*>(base+0xD2BDE1)=1;
        auto* actor=actors.data()+owner*0xF90;actor[0xDE5]=0;actor[0xDE7]=1;
    };
    const auto hit=[&](unsigned owner,unsigned id,unsigned sub,unsigned target,bool missed=false){
        queue[2]=static_cast<unsigned char>(sub);auto* actor=actors.data()+target*0xF90;auto* group=actor+0x774;
        std::memset(group,0,68);group[1]=1;group[2]=static_cast<unsigned char>(owner);group[3]=static_cast<unsigned char>(sub);
        auto* info=group+24;
        const int damage=static_cast<int>(producer(owner,actors.data()+owner*0xF90,target,actor,
            bank.data()+20+96*id,0x3000+id,info,0,0,0,0));
        W32(info+32,static_cast<unsigned>(damage));if(missed)info[1]=1;
        int out=0;result(owner,sub,target,&out,nullptr);return damage;
    };
    const auto complete=[&](unsigned owner,unsigned cursor){queue[2]=static_cast<unsigned char>(cursor);return finish(owner,0,0);};
    const auto view=[&](unsigned owner){E::ElementalView output{};Check(E::ReadElement(owner,8,output),"live numerical status view uses an admitted actor");return output;};
    begin(0,95,2);const int first=hit(0,95,0,1);
    Check(first==100&&view(1).imperil==0,"first hit does not apply the queued Imperil early");
    Check(hit(0,95,1,1)==100&&view(1).imperil==0,"every hit uses the same old action snapshot");
    Check(complete(0,2)==1&&view(1).imperil==1&&view(1).imperilTurns==3&&view(1).effectiveBp==12500,
          "one actual completed action applies one stack with a full three-action duration");
    for(unsigned i=0;i<5;++i){begin(0,95,1);hit(0,95,0,1);complete(0,1);}
    Check(view(1).imperil==4&&view(1).effectiveBp==20000,"reapplication caps at four stacks and refreshes duration");
    begin(1,0,1,true);complete(1,1);
    Check(view(1).imperilTurns==3,"a counter does not spend a normal action duration");
    begin(1,0,1);complete(1,1);Check(view(1).imperilTurns==2,"one completed target action ages the status once");
    actors[0xF90+0xDC9]=0;E::TickMainThread();E::TickMainThread();
    Check(view(1).imperilTurns==2,"a reserve without actions does not age its status by frames");actors[0xF90+0xDC9]=1;
    begin(1,0,1);complete(1,1);begin(1,0,1);complete(1,1);
    Check(!view(1).imperil&&view(1).effectiveBp==10000,"third completed target action removes the old Imperil");
    begin(0,95,1);hit(0,95,0,1,true);complete(0,1);
    Check(!view(1).imperil,"a consumed miss cannot create an external status");
    begin(0,95,1);hit(0,95,0,1);complete(0,0);
    Check(!view(1).imperil,"cancelled unfinished action discards its pending effects");
    begin(1,96,1);hit(1,96,0,1);complete(1,1);
    Check(view(1).ward==1&&view(1).wardTurns==3&&view(1).effectiveBp==7500,"self Ward starts after its own action is counted");
    begin(0,95,1);hit(0,95,0,1);complete(0,1);
    Check(view(1).imperil==1&&view(1).ward==1&&view(1).effectiveBp==10000,"independent Imperil and Ward combine in one resolver");
    begin(0,97,1);hit(0,97,0,1);complete(0,1);
    Check(!view(1).imperil&&view(1).ward==1,"Cleanse removes external Imperil without stripping Ward");
    begin(0,99,1);hit(0,99,0,1);complete(0,1);
    Check(!view(1).ward,"explicit Dispel Ward removes only its own external effect");
    begin(0,98,1);hit(0,98,0,1);complete(0,1);
    Check(view(1).nul==1,"a completed external Nul command grants one charge");
    begin(0,88,2);
    Check(hit(0,88,0,1)==0&&view(1).nul==0,"a consumed ninth-element result spends one external Nul charge");
    Check(hit(0,88,1,1)==100,"one charge cannot nullify the next hit of the same action");complete(0,2);
    begin(0,98,1);hit(0,98,0,1);complete(0,1);
    begin(0,88,1);
    auto* group=actors.data()+0xF90+0x774;std::memset(group,0,68);group[1]=1;group[2]=0;
    const auto reserved=producer(0,actors.data(),1,actors.data()+0xF90,bank.data()+20+96*88,0x3058,group+24,0,0,0,0);
    Check(reserved==0&&view(1).nul==1,"calculation reserves external Nul without committing an unconsumed result");
    complete(0,0);
    Check(view(1).nul==1,"cancelling a calculated but unconsumed hit releases its Nul reservation");
    begin(0,88,1);Check(hit(0,88,0,1)==0&&view(1).nul==0,"a new action can spend the released charge exactly once");complete(0,1);
    begin(0,98,1);hit(0,98,0,1);complete(0,1);
    begin(0,94,1);Check(hit(0,94,0,1)==150&&view(1).nul==1,
        "an uncovered native element prevents a mixed attack from spending external Nul");complete(0,1);
    begin(0,100,1);hit(0,100,0,1);complete(0,1);
    Check(!view(1).nul,"Dispel Nul can remove a charge without blocking its own support command");
    begin(0,98,1);hit(0,98,0,1);complete(0,1);
    begin(0,88,1);std::memset(group,0,24+2*44);group[1]=2;group[2]=0;
    const auto calculate=[&](unsigned n){auto* info=group+24+44*n;
        const auto damage=producer(0,actors.data(),1,actors.data()+0xF90,bank.data()+20+96*88,0x3058,info,0,0,0,0);
        W32(info+32,damage);return damage;};
    Check(calculate(0)==0&&calculate(0)==0&&view(1).nul==1,
          "repeated calculation of one pending native result does not reserve or spend twice");
    Check(calculate(1)==100&&view(1).nul==1,
          "two precomputed hits cannot both claim the same live external charge");
    int consumed=0;result(0,0,1,&consumed,nullptr);
    Check(view(1).nul==0,"the first consumed blocked result commits its reserved charge");
    result(0,0,1,&consumed,nullptr);complete(0,1);
    Check(!view(1).nul,"consuming the unblocked second result cannot debit again");
    begin(0,95,1);hit(0,95,0,8);complete(0,1);
    Check(view(8).imperil==1&&view(8).effectiveBp==12500,"an allied Aeon uses the same action/status rules");
    W32(actors.data()+8*0xF90+0x5D0,0);E::TickMainThread();W32(actors.data()+8*0xF90+0x5D0,100000);E::TickMainThread();
    Check(!view(8).imperil,"KO and revival cannot inherit a removed transient effect");
    begin(0,95,1);hit(0,95,0,1);complete(0,1);
    actors[0xF90+0xDCE]=1;E::TickMainThread();actors[0xF90+0xDCE]=0;E::TickMainThread();
    Check(!view(1).imperil,"petrification/removal retires transient status ownership");
    begin(0,95,1);hit(0,95,0,1);complete(0,1);
    Check(view(1).imperil==1,"the native incarnation fixture starts with a live external effect");
    Check(TacticsPatch(base+0x39B528,reinterpret_cast<void*>(&TacticsActorSuffix)),
          "actor initialization retains its native lookup and complete status-memory clearing prefix");
    const auto initialize=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(base+0x39B500);
    Check(initialize(0,1)==0x507&&nativeActorInitializations==1&&nativeActorWasCleared,
          "the actual native actor initialization prefix executes exactly once");
    E::TickMainThread();
    Check(!view(1).imperil,"native reinitialization retires old effects even with the same address, owner and file");
    begin(0,95,1);hit(0,95,0,1);
    B::Reset(B::ResetReason::NativeLoad);E::TickMainThread();complete(0,1);
    Check(!view(1).imperil,"a native load discards pending actions and effects");
    E::RequestStop();E::ElementalView stopped{};
    Check(!E::ReadElement(1,8,stopped),"the numerical view cannot advertise stopped status data");
}
