// Jarvis-HOOK: external affinity deltas consume exact equipped native records.
static std::string EquipmentPack(const std::vector<unsigned char>& commands,const std::vector<unsigned char>& kernel){
    auto text=CorePack(commands);
    const std::string requirement="\"mod007.context.v1\"";
    text.replace(text.find(requirement),requirement.size(),requirement+",\"mod007.equipment.v1\"");
    const auto second=",{\"key\":\"table.autoability\",\"kind\":\"autoability\",\"locale\":\"us\",\"bytes\":"+
        std::to_string(kernel.size())+",\"sha256\":\""+Hash(kernel.data(),kernel.size())+
        "\",\"sections\":[{\"first\":0,\"last\":200,\"width\":108,\"offset\":20}]}";
    text.insert(text.find("],\"commands\""),second);
    const auto at=text.find("\"equipment\":[]");
    std::string equipment="\"equipment\":[";
    for(unsigned id=180;id<182;++id){if(id==181)equipment+=",";
        equipment+="{\"key\":\"gear.test"+std::to_string(id)+"\",\"bank\":\"table.autoability\",\"index\":"+
            std::to_string(id)+",\"row_sha256\":\""+Hash(kernel.data()+20+id*108,108)+
            "\",\"kind\":\"armor\",\"owners\":[1,8],\"sos\":"+(id==181?"true":"false")+
            ",\"deltas\":[{\"key\":\"tests.e8\",\"delta_bp\":-2500}]}";
    }
    equipment+="]";text.replace(at,std::strlen("\"equipment\":[]"),equipment);return text;
}
static void EquipmentCases(std::uintptr_t base,std::vector<unsigned char>& actors,
                            std::vector<unsigned char>& commands,std::vector<unsigned char>& kernel){
    coreImage=base;amount=1000;W::DamageProducerForTests(reinterpret_cast<void*>(&CoreEndpoint));
    for(unsigned owner=0;owner<18;++owner){actors[owner*0xF90+0x592]=255;actors[owner*0xF90+0x593]=255;}
    auto* gear=reinterpret_cast<unsigned char*>(base+0xD30F2C);
    std::memset(gear,0,44);gear[2]=1;gear[4]=gear[6]=1;gear[5]=1;gear[11]=4;
    for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);
    W16(gear+14,0x80B4);actors[0xF90+0x593]=0;
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    std::array<unsigned char,44> info{};
    const auto hit=[&](unsigned owner=1){return static_cast<int>(producer(0,actors.data(),owner,actors.data()+owner*0xF90,
        commands.data()+20+96*88,0x3058,info.data(),0,0,0,0));};
    Check(hit()==1250,"one actually equipped external ability contributes its delta to the native affinity stage");
    W16(gear+16,0x80B4);Check(hit()==1250,"duplicate native slots cannot multiply the same equipment binding");
    W16(gear+16,0x80B5);
    for(unsigned state=0;state<4;++state){actors[0xF90+0x63E]=static_cast<unsigned char>(state);
        Check(hit()==(state==1||state==2?1000:1250),"SOS uses the native condition state rather than an invented HP ratio");}
    actors[0xF90+0x63E]=0;gear[5]=0;
    Check(hit()==1500,"an armor binding cannot borrow a weapon record");gear[5]=1;
    gear[6]=2;Check(hit()==1500,"an unequipped or differently equipped record grants no affinity");gear[6]=1;
    gear[4]=2;Check(hit()==1500,"canonical owner is checked on the gear itself");gear[4]=1;
    gear[11]=0;Check(hit()==1500,"unused words outside the visible native slot count are ignored");gear[11]=4;
    gear[2]=0;Check(hit()==1500,"an absent piece cannot confer an external delta");gear[2]=1;
    E::ElementalView view{};
    Check(E::ReadElement(1,8,view)&&view.baseBp==15000&&view.effectiveBp==12500&&view.equipmentBp==-2500,
          "the numerical view identifies the actual equipment delta and shares the damage resolver");
    kernel[20+180*108+0x20]^=1;
    Check(hit()==1000&&!E::ReadElement(1,8,view),"changed equipped row data closes the whole affected custom affinity view");
    kernel[20+180*108+0x20]^=1;Check(hit()==1250,"restored exact admitted payload is recognized without a fabricated item ID");
    W16(reinterpret_cast<unsigned char*>(base+0xD2A970),static_cast<unsigned>(kernel.size()-1));
    Check(hit()==1000,"native ability-bank byte length participates in the live admission stamp");
    W16(reinterpret_cast<unsigned char*>(base+0xD2A970),static_cast<unsigned>(kernel.size()));
    auto replacement=kernel;const auto pointer=reinterpret_cast<std::uintptr_t>(replacement.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&pointer,4);
    Check(hit()==1000,"replacement ability banks cannot borrow old row pointers");
    E::TickMainThread();Check(hit()==1250,"fresh bank admission binds the replacement equipment payload");
    gear[4]=gear[6]=8;actors[8*0xF90+0x593]=0;actors[0xF90+0x593]=255;
    Check(hit(8)==750,"an explicitly allowed canonical Aeon owner can use the same declared external equipment delta");
    gear[4]=gear[6]=2;actors[2*0xF90+0x593]=0;actors[8*0xF90+0x593]=255;
    Check(hit(2)==1000,"another owner outside the binding cannot inherit its effect");
    E::RequestStop();Check(hit(2)==1000,"stopped equipment consumers retain native behavior");
}
