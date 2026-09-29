// Jarvis-HOOK: card-only external weapon elements share the existing affinity
// owner. Kernel rows are read and revalidated; no command or item is rewritten.
void AddHitElement(HitFrame& frame,const std::array<AffinityValue,ElementLimit>& values,
                   unsigned element,unsigned weight) noexcept {
    for(unsigned i=0;i<frame.count;++i)if(frame.elements[i]==element)return;
    if(element>=pack.registry.Size()||frame.count>=ElementLimit)return;
    frame.elements[frame.count]=element;
    frame.parts[frame.count++]={values[element].value,weight};
    frame.nativeMask|=pack.registry.At(element)->nativeBit;
}
bool WeaponRow(const Bus::DamageCall& call,const Byte* row) noexcept {
    Byte formula=0;
    return Copy(&formula,static_cast<const Byte*>(call.userActor)+0x5C1,1)&&
        Arcana::Elemental::EligibleWeapon(row,96,formula);
}
bool ReadCardWeaponRow(const Bus::DamageCall& call,std::array<Byte,96>& row) noexcept {
    if(call.commandId!=0&&(call.commandId&0xFFFFF000u)!=0x3000u)return false;
    std::uint32_t bank=0;
    if(!Read(module+0xD2A92C,bank)||bank<0x10000||bank>UINT32_MAX-8u*1024u*1024u)return false;
    Byte header[8]{};if(!Copy(header,reinterpret_cast<const void*>(bank),sizeof(header)))return false;
    const auto word=[](const Byte* bytes){return unsigned(bytes[0])|(unsigned(bytes[1])<<8);};
    const unsigned sections=word(header),index=call.commandId&0xFFFu;
    if(!sections||sections>16)return false;
    bool found=false;
    for(unsigned i=0;i<sections;++i){
        Byte section[12]{};
        if(!Copy(section,reinterpret_cast<const void*>(bank+8+12*i),sizeof(section)))return false;
        const unsigned first=word(section),last=word(section+2),width=word(section+4),length=word(section+6);
        std::uint32_t offset=0;std::memcpy(&offset,section+8,4);
        if(first>last||last>4095||width!=96||length!=(last-first+1)*96u||
           offset<8+sections*12u||offset>8u*1024u*1024u-length)return false;
        if(index<first||index>last)continue;
        const auto address=std::uintptr_t(bank)+offset+(index-first)*96u;
        if(found||call.command!=reinterpret_cast<const void*>(address)||
           !Copy(row.data(),call.command,row.size()))return false;
        found=true;
    }
    return found&&WeaponRow(call,row.data());
}
void AddCardStrikes(HitFrame& frame,const Bus::DamageCall& call,const Byte* row,
                    const std::array<AffinityValue,ElementLimit>& values) noexcept {
    if(!options.core||!WeaponRow(call,row))return;
    Arcana::Elemental::Snapshot card{};
    const auto provider=Arcana::Elemental::provider.load(std::memory_order_acquire);
    if(!provider||!Arcana::Elemental::Read(call.user,card)||!card.strikes)return;
    const unsigned before=frame.count;
    for(unsigned bit:{Arcana::Elemental::Poison,Arcana::Elemental::Gravity})
        if(card.strikes&bit)AddHitElement(frame,values,CardElement(bit),1);
    if(frame.count!=before){frame.card=card;frame.cardProvider=provider;}
}
void* EnterCardWeapon(const Bus::DamageCall& call,const void*& forward) noexcept {
    if(!options.core)return nullptr;
    Arcana::Elemental::Snapshot card{};
    if(!Arcana::Elemental::Read(call.user,card)||!card.strikes)return nullptr;
    std::array<Byte,96> row{};if(!ReadCardWeaponRow(call,row))return nullptr;
    std::array<AffinityValue,ElementLimit> values{};
    Arcana::Elemental::Snapshot target{};Arcana::Elemental::Provider targetProvider=nullptr;
    if(!Affinities(call.target,call.targetActor,values,nullptr,false,&target,&targetProvider))return nullptr;
    auto& frame=frames[Bus::currentDamage->depth-1];frame={};
    frame.generation=generation.load();frame.cardRow=row;
    frame.cardTarget=target;frame.cardTargetProvider=targetProvider;
    AddCardStrikes(frame,call,row.data(),values);
    if(!frame.count)return nullptr;
    Byte weapon=0;if(!Copy(&weapon,static_cast<const Byte*>(call.userActor)+0x5D9,1)){frame={};return nullptr;}
    // Weapon-inheriting native commands use the actor mask, not the row's
    // fixed spell mask. Authored augment bindings keep their separate policy.
    const unsigned mask=weapon;
    for(unsigned i=0;i<pack.registry.Size();++i)
        if(mask&pack.registry.At(i)->nativeBit)AddHitElement(frame,values,i,1);
    std::copy(row.begin(),row.end(),frame.command.begin());
    frame.command[0x2D]=static_cast<Byte>(frame.nativeMask);
    frame.core=true;frame.cardNative=true;forward=frame.command.data();return &frame;
}
bool CurrentCardHit(const HitFrame& frame,const Bus::DamageCall& call) noexcept {
    if(frame.cardTargetProvider){
        Arcana::Elemental::Snapshot target{};
        if(frame.cardTargetProvider!=Arcana::Elemental::provider.load(std::memory_order_acquire)||
           !Arcana::Elemental::Read(call.target,target)||!(target==frame.cardTarget))return false;
    }
    if(!frame.cardProvider)return !frame.cardNative;
    Arcana::Elemental::Snapshot current{};
    if(frame.cardProvider!=Arcana::Elemental::provider.load(std::memory_order_acquire)||
       !Arcana::Elemental::Read(call.user,current)||!(current==frame.card))return false;
    if(!frame.cardNative)return true;
    std::array<Byte,96> row{};
    return ReadCardWeaponRow(call,row)&&row==frame.cardRow;
}
