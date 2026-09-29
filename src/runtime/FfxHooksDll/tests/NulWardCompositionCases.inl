// The native producer, Nul entry, queue finish and actor clear prefix execute.
// Formula and graphical suffixes use the existing isolated fixture endpoints.
#include "../hooks/NulElementCommands.h"
static LONG WINAPI NulCompositionCrash(EXCEPTION_POINTERS* fault){
    std::printf("NUL_NATIVE_EXCEPTION code=%08lX rva=%08lX\n",fault->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-coreImage));
    return EXCEPTION_EXECUTE_HANDLER;
}
static std::string NulCompositionPack(std::vector<unsigned char>& bank){
    for(unsigned id:{101u,102u,103u,320u,321u}){
        auto* row=bank.data()+20+96*id;row[0x19]=255;row[0x20]=2;
        row[0x23]=id<320?1:0;row[0x28]=4;row[0x2A]=16;
        row[0x2D]=static_cast<unsigned char>(id==101?16:id==102?17:id==103?144:0);
    }
    for(const auto& command:FfxHooks::NulElements::Commands){
        auto* row=bank.data()+20+96*command.id;std::memset(row,0,96);
        W16(row+16,command.animation);row[23]=row[24]=4;row[25]=1;row[26]=5;row[37]=2;row[43]=1;
    }
    auto json=TacticsPack(bank);std::string commands;
    json.replace(json.find("\"last\":319"),10,"\"last\":373");
    for(unsigned id:{101u,102u,103u})commands+=",{\"key\":\"nul.mixed"+std::to_string(id)+
        "\",\"bank\":\"table.command\",\"index\":"+std::to_string(id)+",\"row_sha256\":\""+
        Hash(bank.data()+20+96*id,96)+"\",\"elements\":[{\"key\":\"tests.e4\",\"weight\":1},{\"key\":\"tests.e"+
        std::string(id==101?"8":id==102?"0":"7")+"\",\"weight\":1}]}";
    json.insert(json.find("],\"profiles\""),commands);
    ReplaceText(json,"tests.e8","spira.poison");ReplaceText(json,"tests.e9","spira.gravity");return json;
}
static void NulCompositionCases(std::uintptr_t base,std::vector<unsigned char>& actors,std::vector<unsigned char>& bank){
    coreImage=base;tacticsActors=actors.data();amount=100;
    SetUnhandledExceptionFilter(NulCompositionCrash);
    W::DamageProducerForTests(reinterpret_cast<void*>(&CoreEndpoint));
    for(unsigned i=0;i<31;++i){auto* actor=actors.data()+i*0xF90;actor[0xDC8]=actor[0xDC9]=1;
        W32(actor+0x594,100000);W32(actor+0x5D0,100000);actor[0xDE5]=255;}
    std::array<unsigned char,14> scene{};unsigned char variant=0;
    const auto scenePointer=reinterpret_cast<std::uintptr_t>(scene.data()),variantPointer=reinterpret_cast<std::uintptr_t>(&variant);
    std::memcpy(reinterpret_cast<void*>(base+0xD2A9C8),&scenePointer,4);
    std::memcpy(reinterpret_cast<void*>(base+0xD2A9FC),&variantPointer,4);
    W16(reinterpret_cast<unsigned char*>(base+0xD2C256),0);
    Check(TacticsPatch(base+0x38F0C0,reinterpret_cast<void*>(&TacticsResultSuffix)),"isolate only the graphical result suffix");
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    const auto result=reinterpret_cast<FfxHooks::SharedAction::ResultFunction>(base+0x38F0B0);
    const auto finish=reinterpret_cast<FfxHooks::SharedAction::FinishFunction>(base+0x3B0870);
    auto* queue=reinterpret_cast<unsigned char*>(base+0xD2AC70);auto* target=actors.data()+0xF90;
    auto* group=target+0x774;auto* info=group+24;
    const auto begin=[&](unsigned id,unsigned count=1){
        std::memset(queue,0,72);queue[3]=static_cast<unsigned char>(count);
        for(unsigned i=0;i<count;++i){W16(queue+8+16*i,0x3000+id);W16(queue+10+16*i,255);W32(queue+16+16*i,2);}
        *reinterpret_cast<unsigned char*>(base+0xD2BDE1)=1;actors[0xDE5]=0;actors[0xDE7]=1;
        std::memset(group,0,68);group[1]=1;
    };
    const auto calculate=[&](unsigned id){return static_cast<int>(producer(0,actors.data(),1,target,
        bank.data()+20+96*id,0x3000+id,info,0,0,0,0));};
    const auto consume=[&](){int out=0;return result(0,0,1,&out,nullptr);};
    const auto end=[&](bool complete=true){queue[2]=complete?queue[3]:0;return finish(0,0,0);};
    const auto grant=[&](unsigned id){begin(id);calculate(id);consume();end();};
    const auto hit=[&](unsigned id){begin(id);const auto value=calculate(id);consume();end();return value;};
    target[0x613]=37;target[0x614]=41;target[0x60E]=3;target[0x610]=4;
    grant(320);
    Check(target[0x613]==37&&target[0x614]==41&&target[0x60E]==3&&target[0x610]==4,
          "legacy native-slot option cannot overwrite timers or existing native Nul charges");
    Check(hit(84)==0&&hit(84)==150,"Radiant Ward blocks Holy exactly once through shared Nul");
    grant(321);Check(hit(87)==0&&hit(87)==150,"Umbral Ward blocks Dark exactly once");
    for(const auto& pair:{std::array<unsigned,2>{{370,85}},std::array<unsigned,2>{{371,86}},
                          std::array<unsigned,2>{{372,88}},std::array<unsigned,2>{{373,89}}}){
        const int baseline=hit(pair[1]);grant(pair[0]);
        Check(hit(pair[1])==0&&hit(pair[1])==baseline,"each new native/external Nul protects its own element exactly once");
    }
    grant(372);Check(hit(101)==150,"a Poison charge alone cannot cover a mixed Holy/Poison hit");
    grant(320);Check(hit(101)==0,"explicit Holy and Poison spell charges compose through one shared resolver");
    Check(hit(84)==150&&hit(88)==100,"the mixed hit consumes both participating spell charges exactly once");
    grant(320);Check(hit(102)==150&&hit(84)==0,"an uncovered Fire component preserves the Holy charge");
    grant(320);Check(hit(103)==150,"mixed Holy Dark requires both charges");
    grant(321);Check(hit(103)==0&&hit(84)==150&&hit(87)==150,"full mixed coverage spends each ward exactly once");
    grant(320);begin(102);info[0xE]=1;
    Check(calculate(102)==0&&info[0xE]==0&&calculate(102)==0&&info[0xE]==0,
          "duplicate mixed native and external calculation reuses both charge decisions");consume();end();
    Check(hit(84)==150,"settling a mixed native ward hit consumes the external charge once");
    grant(320);begin(84);Check(calculate(84)==0&&calculate(84)==0,"duplicate calculation reuses one reservation");end(false);
    Check(hit(84)==0&&hit(84)==150,"cancellation releases the reservation without spending its charge");
    grant(320);begin(84);Check(calculate(84)==0,"miss case initially reserves a ward");info[1]=1;consume();end();
    Check(hit(84)==0,"a later miss releases the ward without consuming it");
    grant(320);grant(98);Check(hit(101)==0,"Holy and the ninth element compose legacy and external Nul charges");
    Check(hit(84)==150&&hit(88)==100,"mixed settlement consumes each participating charge once");
    grant(320);begin(84);calculate(84);consume();consume();end();
    Check(hit(84)==150,"replaying result consumption cannot revive or double-debit a charge");
    grant(320);Check(TacticsPatch(base+0x39B528,reinterpret_cast<void*>(&TacticsActorSuffix)),"keep actual actor lookup and native clearing prefix");
    const auto initialize=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(base+0x39B500);
    Check(initialize(0,1)==0x507&&nativeActorWasCleared,"native actor reset executes once");E::TickMainThread();
    Check(hit(84)==150,"same-address actor reincarnation cannot inherit a ward");
    grant(320);B::Reset(B::ResetReason::NativeLoad);E::TickMainThread();Check(hit(84)==150,"save-load reset retires ward ownership");
    grant(320);Check(FfxHooks::RemoveNulWardHook(Log),"ward teardown unsubscribes without removing shared owners");
    Check(hit(84)==150&&FfxHooks::SharedAction::Start(base),"stopped wards leave Elemental and shared actions operational");
    E::RequestStop();
}
