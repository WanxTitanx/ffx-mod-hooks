// Included inside Spira's private namespace after canonical actor admission.
std::atomic<bool> aeroBound{false};
bool AeroReady() noexcept {return options.spira&&aeroBound&&Current();}
bool AeroEquipped(unsigned owner,const void* actor,unsigned kind,unsigned id) noexcept {
    EquipmentEffects::View gear{};
    if(!EquipmentEffects::Capture(module,owner,actor,Copy,gear))return false;
    for(unsigned i=0;i<gear.count;++i)if(gear.entries[i].kind==kind&&gear.entries[i].word==0x8000u+id)return true;
    return false;
}
unsigned AeroSos(unsigned owner,const void* actor) noexcept {
    if(owner>=18||!AeroReady()||Actor(owner)!=actor)return 0;
    Byte condition=0;int hp=0;const auto* bytes=static_cast<const Byte*>(actor);
    if(!Copy(&condition,bytes+0x63E,1)||!Copy(&hp,bytes+0x5D0,4))return 0;
    return AeroEquipment::Sos(AeroEquipped(owner,actor,1,180),hp>0,condition);
}
const AeroEquipmentBridge::Provider aeroProvider{AeroReady,AeroSos};
bool AeroProfile(std::uintptr_t base) noexcept {
    constexpr Byte stream[]={0x55,0x8B,0xEC,0x8B,0x45,0x0C,0x56,0x33,0xF6,0x57,0x48,0x74,0x08,0x48,0x75,0x0A};
    Byte actual[sizeof(stream)]{};
    return Copy(actual,reinterpret_cast<void*>(base+ExecutableProfile::Rva<0x38D2D0>()),sizeof(actual))&&
        !std::memcmp(actual,stream,sizeof(stream))&&RecoveryNative::Match(base,OriginalPs2Rng::Evidence::Indexed);
}
bool AeroWeaponCommand(const Bus::DamageCall& call) noexcept {
    if((call.commandId&0xFFFFF000u)!=0&&(call.commandId&0xFFFFF000u)!=0x3000)return false;
    std::uint32_t bank=0;std::array<Byte,20> header{};std::array<Byte,96> row{};
    if(!Read(module+ExecutableProfile::Rva<0xD2A92C>(),bank)||bank<0x10000||bank>UINT32_MAX-0x100000||
       !Copy(header.data(),reinterpret_cast<const void*>(bank),header.size()))return false;
    const auto id=call.commandId&0xFFF;
    if(AeroEquipment::Word(header.data())!=1||AeroEquipment::Word(header.data()+8)||
       AeroEquipment::Word(header.data()+10)>4095||id>AeroEquipment::Word(header.data()+10)||
       AeroEquipment::Word(header.data()+12)!=96||AeroEquipment::Dword(header.data()+16)!=20||
       call.command!=reinterpret_cast<const void*>(std::uintptr_t(bank)+20+96u*id)||!Copy(row.data(),call.command,row.size()))return false;
    return (row[0x1E]&4)&&(row[0x20]&3)==1&&(row[0x23]&1)&&!(row[0x20]&0x10);
}
struct AeroLease {Byte* actor=nullptr;unsigned owner=31,offset=0;std::uint32_t resource=0;Byte previous=0,applied=0;};
struct AeroFrame {std::array<AeroLease,2> leases{};};
thread_local std::array<AeroFrame,Bus::MaximumDepth> aeroFrames{};
bool AeroBorrow(AeroLease& lease,unsigned owner,Byte* actor,unsigned offset) noexcept {
    Byte before=0;std::uint32_t resource=0;
    if(!actor||Actor(owner)!=actor||!Copy(&before,actor+offset,1)||!Copy(&resource,actor+0x48,4)||(before&AeroEquipment::Wind))return false;
    const Byte next=static_cast<Byte>(before|AeroEquipment::Wind);
    if(!Copy(actor+offset,&next,1))return false;
    lease={actor,owner,offset,resource,before,next};return true;
}
void AeroRestore(AeroLease& lease) noexcept {
    if(!lease.actor)return;
    Byte actual=0;std::uint32_t resource=0;
    // Cleanup remains available after stop, but never overwrites a reused actor
    // or a byte that another native operation has subsequently changed.
    if(Actor(lease.owner)==lease.actor&&Copy(&resource,lease.actor+0x48,4)&&resource==lease.resource&&
       Copy(&actual,lease.actor+lease.offset,1)&&actual==lease.applied)Copy(lease.actor+lease.offset,&lease.previous,1);
    lease={};
}
void* AeroEnter(const Bus::DamageCall& call,const void*&) noexcept {
    if(!AeroReady()||!Bus::currentDamage||!Bus::currentDamage->depth||Bus::currentDamage->depth>aeroFrames.size()||
       Actor(call.user)!=call.userActor||Actor(call.target)!=call.targetActor)return nullptr;
    auto& frame=aeroFrames[Bus::currentDamage->depth-1];frame={};
    if(call.target==AeroEquipment::Valefor)AeroBorrow(frame.leases[0],call.target,static_cast<Byte*>(call.targetActor),0x5DA);
    if(call.user<18&&AeroWeaponCommand(call)){
        auto* source=static_cast<Byte*>(call.userActor);Byte mask=0;
        if(Copy(&mask,source+0x5D9,1)&&!(mask&AeroEquipment::Wind)){
            bool activate=call.user==AeroEquipment::Valefor;
            if(!activate&&!SpiraElemental::Bridge::imperilTouchRequested.load()&&AeroEquipped(call.user,source,0,176)){
                const auto stream=reinterpret_cast<unsigned(__cdecl*)(unsigned,unsigned)>(module+ExecutableProfile::Rva<0x38D2D0>())(call.user,0);
                const auto roll=reinterpret_cast<unsigned(__cdecl*)(unsigned)>(module+ExecutableProfile::Rva<0x398900>())(stream)%100;
                activate=AeroEquipment::Touch(true,true,mask,roll);
            }
            if(activate)AeroBorrow(frame.leases[1],call.user,source,0x5D9);
        }
    }
    return &frame;
}
void AeroLeave(void* token,const Bus::DamageCall&,unsigned,bool) noexcept {
    auto* frame=static_cast<AeroFrame*>(token);if(!frame)return;
    for(unsigned i=static_cast<unsigned>(frame->leases.size());i>0;--i)AeroRestore(frame->leases[i-1]);
}
const Bus::Observer aeroObserver{AeroEnter,AeroLeave};
