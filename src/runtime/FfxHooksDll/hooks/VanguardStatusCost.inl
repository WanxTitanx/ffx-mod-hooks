// Included inside Vanguard's private runtime namespace. Native RNG, status
// conflicts and result counters stay owned by the original status producer.
using MpCostFn=int(__cdecl*)(unsigned,const Byte*);
int NativeModifiedMpCost(unsigned id,const Byte* command) {
    const int native=reinterpret_cast<MpCostFn>(originals[CostHook])(id,command);
    if(!Enter()||!On(Feature::Efficiency)||native<=0||!command)return native;
    auto* actor=Actor(id);if(!actor)return native;
    Effects effects{},unused{};ReadEffects(actor,nullptr,effects,unused);
    if(!running.load()||!effects[3])return native;
    const auto abilities=Read<std::uint16_t>(actor+0x6BC);
    // Preserve the native One MP / Magic Booster precedence, rather than
    // accidentally making a positive command free by reducing its final BYTE.
    if(abilities&0x8000)return native;
    const unsigned base=Read<Byte>(command+0x25);
    const auto category=Read<Byte>(command+0x17);
    const unsigned booster=(abilities&0x40)&&(category==1||category==2)?2u:1u;
    const unsigned percent=(abilities&0x4000)?25u:75u;
    return static_cast<int>((std::uint64_t(base)*booster*percent+99)/100);
}
int __cdecl MpCostShim(unsigned id,const Byte* command){return AdjustCastMpCost(id,command,NativeModifiedMpCost(id,command));}
using QuarterFn=int(__cdecl*)(const Byte*,unsigned*,int);
int __cdecl QuarterShim(const Byte* target,unsigned* output,int amount) {
    const auto original=reinterpret_cast<QuarterFn>(originals[QuarterHook]);
    if(!Enter()||!On(Feature::UnhinderedHealing)||amount>=0||ActorId(target)>=31)
        return original(target,output,amount);
    Byte snapshot[0xF90]{};if(!Copy(snapshot,target,sizeof(snapshot)))return original(target,output,amount);
    // Ignore Shield mitigation for restoration. Keep native Boost and all other
    // polarity rules; the actual actor does not consume a shield-on-hit marker.
    snapshot[0x616]&=static_cast<Byte>(~0x40u);
    return original(snapshot,output,amount);
}
using StatusFn=int(__cdecl*)(unsigned,Byte*,unsigned,Byte*,const Byte*,unsigned*,unsigned*,Byte*,int,int*,int*);
int RunStatusEntry(unsigned sourceId,Byte* source,unsigned targetId,Byte* target,
                   const Byte* command,unsigned* hits,unsigned* effects,Byte* info,
                   int result,int* amounts,int* counter,int refreshIndex) {
    Byte saved=0;Byte* duration=refreshIndex>=0?target+0x608+refreshIndex:nullptr;
    if(duration){saved=*duration;*duration=0;}
    int next=result;
    __try {next=reinterpret_cast<StatusFn>(originals[StatusHook])(
        sourceId,source,targetId,target,command,hits,effects,info,result,amounts,counter);}
    __finally {if(duration)*duration=saved;}
    return next;
}
int __cdecl StatusShim(unsigned sourceId,Byte* source,unsigned targetId,Byte* target,
                      const Byte* command,unsigned* hits,unsigned* effects,Byte* info,
                      int result,int* amounts,int* counter) {
    const auto original=reinterpret_cast<StatusFn>(originals[StatusHook]);
    if(!Enter()||!(On(Feature::StatusRefresh)||On(Feature::DurationResistance))||
       Actor(sourceId)!=source||Actor(targetId)!=target||!command||!hits||!effects||!info||!amounts||!counter)
        return original(sourceId,source,targetId,target,command,hits,effects,info,result,amounts,counter);
    Byte row[96]{};if(!Copy(row,command,sizeof(row)))return original(sourceId,source,targetId,target,command,hits,effects,info,result,amounts,counter);
    const auto misc=Read<std::uint32_t>(row+0x1C);const bool weapon=(misc&0x40000)!=0;
    const bool cleanse=(row[0x20]&0x20)!=0;
    Byte chances[25]{},durations[13]{};
    for(unsigned i=0;i<25;++i)chances[i]=weapon?(std::max)(row[0x2E+i],source[0x5DE+i]):row[0x2E+i];
    for(unsigned i=0;i<13;++i)durations[i]=weapon?(std::max)(row[0x47+i],source[0x5F7+i]):row[0x47+i];
    const auto withoutWeapon=misc&~0x40000u;std::memcpy(row+0x1C,&withoutWeapon,4);
    std::memset(row+0x2E,0,25);
    unsigned resistance=25;
    const auto configured=Config::ReadIntExact("vanguard_status.enemy_duration_resistance",0,100);
    if(configured.state==Config::IntReadState::Valid)resistance=static_cast<unsigned>(configured.value);
    else if(configured.state==Config::IntReadState::Invalid)resistance=0;
    // One native invocation per selected status preserves its original ordered
    // RNG draws while exposing whether THAT status actually applied. No extra
    // success roll, chance override, or global command-table mutation is used.
    for(unsigned index=0;index<25;++index){
        if(!chances[index])continue;
        row[0x2E+index]=chances[index];
        const int timed=static_cast<int>(index)-12;
        int refresh=-1;
        if(timed>=0){row[0x47+timed]=durations[timed];
            const unsigned prior=target[0x608+timed];
            if(On(Feature::StatusRefresh)&&!cleanse&&prior>0&&prior<254&&
               !(Read<std::uint16_t>(target+0x62C)&(1u<<timed)))refresh=timed;}
        const unsigned before=hits[4];
        result=RunStatusEntry(sourceId,source,targetId,target,row,hits,effects,info,result,amounts,counter,refresh);
        if(timed>=0&&!cleanse&&hits[4]!=before&&On(Feature::DurationResistance)&&sourceId<18&&targetId>=18&&
           (timed<=2||timed==12)&&info[7+timed]>0&&info[7+timed]<254){
            info[7+timed]=static_cast<Byte>((std::max)(1u,unsigned(info[7+timed])*(100-resistance)/100));
        }
        row[0x2E+index]=0;
    }
    return result;
}
