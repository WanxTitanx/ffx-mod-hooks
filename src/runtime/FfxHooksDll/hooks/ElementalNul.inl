// Jarvis-HOOK: external charges follow native result-slot ownership. Calculation
// reserves a charge; the actual result consumer commits it. Cancellation retires
// unconsumed reservations without spending the live status.
struct NulReservation {
    ActionToken action{};
    ActorToken target{};
    std::uint64_t generation=0;
    unsigned source=ActorCount,sub=4,command=0,elements=0;
};
constexpr unsigned ResultGroups=2,ResultsPerGroup=16,ResultsPerActor=ResultGroups*ResultsPerGroup;
std::array<NulReservation,ActorCount*ResultsPerActor> nulReservations{};
bool SameStatusAction(ActionToken first,ActionToken second) noexcept {
    return first.Valid()&&second.Valid()&&first.generation==second.generation&&
           first.sequence==second.sequence&&first.slot==second.slot;
}
void ClearNulReservations() noexcept {nulReservations={};}
void ReleaseNulReservations(ActionToken action) noexcept {
    for(auto& reservation:nulReservations)if(SameStatusAction(reservation.action,action))reservation={};
}
unsigned ReservedNul(ActorToken target,unsigned element) noexcept {
    if(!target.Valid()||element>=ElementLimit)return 0;
    unsigned result=0;
    for(unsigned i=0;i<ResultsPerActor;++i){auto& reservation=nulReservations[target.slot*ResultsPerActor+i];
        if(!reservation.elements)continue;
        if(reservation.generation!=generation.load()||!(reservation.target==target)||
           !statusState.Snapshot(reservation.action,target,element).valid){reservation={};continue;}
        if(reservation.elements&(1u<<element))++result;
    }
    return result;
}
unsigned NativeResultIndex(const Bus::DamageCall& call,unsigned& sub) noexcept {
    if(call.target>=ActorCount||!call.info||!Actor(call.target,call.targetActor))return UINT_MAX;
    const auto pointer=reinterpret_cast<std::uintptr_t>(call.info);
    const auto actor=reinterpret_cast<std::uintptr_t>(call.targetActor);
    for(unsigned group=0;group<ResultGroups;++group){
        const auto begin=actor+0x774u+728u*group;
        if(pointer<begin+24u||pointer>=begin+24u+ResultsPerGroup*44u||(pointer-begin-24u)%44u)continue;
        const auto hit=static_cast<unsigned>((pointer-begin-24u)/44u);
        Byte header[4]{};
        if(!Copy(header,reinterpret_cast<const void*>(begin),4)||header[0]>hit||header[1]>16||
           hit>=header[1]||header[2]!=call.user||header[3]>=4)return UINT_MAX;
        sub=header[3];return call.target*ResultsPerActor+group*ResultsPerGroup+hit;
    }
    return UINT_MAX;
}
bool ResolveTacticsNul(const HitFrame& frame,const Bus::DamageCall& call,
                      unsigned argument,void* info,int& output) noexcept {
    if(!options.tactics||!frame.action.Valid()||!(frame.command[0x23]&1)||
       (frame.command[0x20]&0x10)||!frame.count)return false;
    const auto target=statusState.Current(call.target);
    if(!target.Valid())return false;
    unsigned sub=4;const unsigned index=NativeResultIndex(call,sub);
    if(index>=nulReservations.size())return false;
    auto& reservation=nulReservations[index];
    if(reservation.elements&&reservation.generation==generation.load()&&reservation.target==target&&
       SameStatusAction(reservation.action,frame.action)&&reservation.source==call.user&&
       reservation.sub==sub&&reservation.command==call.commandId){output=-1;return true;}
    // An overwritten native result slot no longer owns its former reservation.
    reservation={};unsigned external=0,nativeMask=0,wardMask=0;
    const auto wards=SharedNul::AvailableWards(call);
    for(unsigned i=0;i<frame.count;++i){
        const unsigned element=frame.elements[i],bit=pack.registry.At(element)->nativeBit;
        if(bit&&(wards&bit)){wardMask|=bit;continue;}
        const unsigned offset=bit==1?0xEu:bit==2?0x10u:bit==4?0xFu:bit==8?0xDu:0u;
        Byte nativeCharge=0;
        if(offset&&Copy(&nativeCharge,static_cast<const Byte*>(info)+offset,1)&&nativeCharge){nativeMask|=bit;continue;}
        const auto snapshot=statusState.Snapshot(frame.action,target,element);
        const auto current=statusState.View(target,element);
        if(!snapshot.valid||!current.valid||
           (std::min)(snapshot.nul,current.nul)<=ReservedNul(target,element)){output=0;return true;}
        external|=1u<<element;
    }
    // Native-only coverage retains its existing calculation/writeback contract.
    if(!external)return false;
    if(nativeMask&&originalNul(argument,nativeMask,info)!=-1){output=0;return true;}
    if(!SharedNul::ReserveWards(call,wardMask)){output=0;return true;}
    reservation={frame.action,target,generation.load(),call.user,sub,call.commandId,external};
    output=-1;return true;
}
void SettleNulReservations(const SharedAction::ResultCall& call,ActionToken action,ActorToken target,
                           unsigned group,unsigned first,unsigned last) noexcept {
    if(!target.Valid()||group>=ResultGroups||first>last||last>ResultsPerGroup)return;
    for(unsigned hit=first;hit<last;++hit){auto& reservation=nulReservations[target.slot*ResultsPerActor+group*ResultsPerGroup+hit];
        if(!reservation.elements||reservation.generation!=generation.load()||!(reservation.target==target)||
           !SameStatusAction(reservation.action,action)||reservation.source!=call.source||reservation.sub!=call.sub)continue;
        const auto mask=reservation.elements;reservation={};
        Byte result=255;
        // Native ComputeHitDamage assigns result code 2 to the Nul branch at
        // RVA38E877; a later miss/override must not spend this external charge.
        if(!Read(statusActors[target.slot].address+0x774+728*group+24+44*hit+1,result)||result!=2)continue;
        for(unsigned element=0;element<pack.registry.Size();++element)
            if(mask&(1u<<element))(void)statusState.ConsumeNul(target,element);
    }
}
