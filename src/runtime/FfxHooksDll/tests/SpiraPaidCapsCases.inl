// Jarvis-HOOK: actual paid controller, native aggregation and maximum clamps.
#include "../hooks/RonsoPoolSave.h"
static bool Serialize(const unsigned char* in,unsigned char* out,std::size_t n,FfxHooks::NativeSaveEvents::CheckpointOwnership* p) noexcept {
    if(n!=W::kSaveBytes)return false;std::memcpy(out,in,n);*p={};return true;
}
static void PaidCases(const std::wstring& root,const char* path){
    namespace A=FfxHooks::AeonAscension;
    W::SaveImage original{};std::ifstream input(path,std::ios::binary);
    Check(static_cast<bool>(input.read(reinterpret_cast<char*>(original.data()),original.size())),"private save fixture is complete");if(failures)return;
    WorkshopEconomyFixture::Seed(original,50000000);std::memset(original.data()+0x44DC,0,4400);original[64+0xC6C]=127;
    for(unsigned owner=0;owner<18;++owner){auto* p=original.data()+64+0x55CC+owner*0x94;
        if(owner>=8)p[0x2C]|=0x10;
        for(unsigned kind=0;kind<2;++kind){const unsigned slot=owner*2+kind;p[0x2D+kind]=static_cast<unsigned char>(slot);
            auto* gear=original.data()+0x44DC+slot*22;gear[2]=1;gear[3]=owner>=8&&!kind?4:0;
            gear[4]=gear[6]=static_cast<unsigned char>(owner);gear[5]=static_cast<unsigned char>(kind);gear[11]=4;
            for(unsigned i=0;i<4;++i)W16(gear+14+2*i,owner>=8&&!kind&&!i?0x807B:255);}
        W32(p+0x1C,321);W32(p+0x20,123);W32(p+0x24,9999);W32(p+0x28,999);W16(p+0x4C,0);
    }
    FfxHooks::RonsoPool::SealSave(original);
    FieldLimits field;Check(field.armed,"paid case executes native max/current clamps");if(failures)return;
    W::DamageProducerForTests(reinterpret_cast<void*>(&DamageEndpoint));
    const auto aggregate=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x39C610);
    const auto maximum=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3861B0);
    const auto doubling=reinterpret_cast<int(__cdecl*)(unsigned,void*,int)>(base+0x38D330);
    using Hit=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    const auto damage=reinterpret_cast<Hit>(base+0x38E680);
    Check(FfxHooks::NativeSaveEvents::RegisterCheckpointSerializer(&Serialize),"fixture provides native checkpoint serialization");
    for(unsigned owner=8;owner<18;++owner){const auto save=root+L"\\ffx_"+std::to_wstring(100+owner);
        *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=0;
        Check(W::LoadForTests(save.c_str(),original,original)&&W::CommitLoadForTests(original),"each canonical owner starts with its observed native save");
        S::TickMainThread();workshop::State state{};Check(S::Ready()&&W::Capture(state),"load admits mapping and inventory before purchase");if(failures)return;
        for(unsigned id=0;id<18;++id){Actor(id)[0x592]=Ply(id)[0x2D];Actor(id)[0x593]=Ply(id)[0x2E];}
        maximum(owner);Check(R32(Ply(owner)+0x24)==9999&&R32(Ply(owner)+0x28)==999,"no paid entitlement preserves native maxima");
        A::Request hp{};hp.slot=owner*2+1;hp.position=0;hp.effect=0;hp.pieceId=state.pieces[hp.slot].id;hp.revision=state.revision;A::Plan preview{};
        const auto before=state;
        Check(W::PreviewAscension(hp,preview)==workshop::Error::Ok&&W::Capture(state)&&!std::memcmp(&state,&before,sizeof(state)),"review does not charge or mutate");
        Check(W::CommitAscension(hp,preview)&&W::Capture(state)&&W::AscensionEffect(owner,0),"controller grants permission after durable payment");if(failures)return;
        Check(R32(Ply(owner)+0x24)==600000&&R32(Ply(owner)+0x28)==9999,"paid maxima replace vanilla BHP and BMP");
        Check(R32(Ply(owner)+0x1C)==321&&R32(Ply(owner)+0x20)==123,"buying a cap never heals or restores MP");
        aggregate(owner);Check((S::Word(Actor(owner)+0x6BE)&0x600)==0x600,"native aggregator receives paid HP/MP flags");
        W32(Actor(owner)+0x59C,600000);W32(Actor(owner)+0x5A0,9999);W32(Actor(owner)+0x594,600000);W32(Actor(owner)+0x598,9999);
        W32(Actor(owner)+0x5D0,321);W32(Actor(owner)+0x5D4,123);Actor(owner)[0x640]=0;
        doubling(owner,Actor(owner),3);Check(R32(Actor(owner)+0x594)==999999&&R32(Actor(owner)+0x598)==9999,"Double HP/MP uses finite upgraded maxima");
        Check(R32(Actor(owner)+0x5D0)==321&&R32(Actor(owner)+0x5D4)==123,"Double never refills current values");
        doubling(owner,Actor(owner),0);Check(R32(Actor(owner)+0x594)==600000&&R32(Actor(owner)+0x598)==9999,"Double removal restores its owned base");
        A::Request offense{};offense.slot=owner*2;offense.position=1;offense.effect=1;offense.pieceId=state.pieces[offense.slot].id;offense.revision=state.revision;
        Check(W::PreviewAscension(offense,preview)==workshop::Error::Ok&&W::CommitAscension(offense,preview)&&W::Capture(state),"damage cap uses the dedicated paid recipe");if(failures)return;
        aggregate(owner);Check((S::Word(Actor(owner)+0x6BE)&0x800)!=0,"paid damage row supplies real native BDL bit0x800");
        unsigned char row[96]{},info[128]{};row[0x23]=1;row[0x2A]=16;row[0x2B]=1;
        const auto hit=[&](unsigned src,unsigned dst){return damage(src,Actor(src),dst,Actor(dst),row,0x3042,info,0,0,0,0);};
        for(unsigned kind:{1u,2u,0u}){row[0x20]=static_cast<unsigned char>(kind);row[0x28]=static_cast<unsigned char>(kind==1?1:kind==2?3:15);
            amount=450000;Check(hit(owner,1)==450000,"physical spell and numeric OD retain precap damage");
            amount=1200000;Check(hit(owner,1)==999999,"each paid numeric hit has one finite ceiling");
            row[0x20]|=0x40;Check(hit(owner,1)==9999,"explicit native BDL suppression wins");
            row[0x20]=static_cast<unsigned char>(kind|0x10);Check(hit(owner,1)==99999,"intentional healing keeps ordinary native ceiling");}
        row[0x20]=1;row[0x28]=1;row[0x23]=2;component=2;Check(hit(owner,1)==99999,"paid HP cap never affects MP");
        component=1;Check(hit(owner,1)==99999,"paid HP cap never affects CTB");component=3;row[0x23]=1;row[0x20]=0x81;
        Check(hit(1,owner)==99999,"attacker cannot borrow the target's permission");
        amount=-150000;Check(static_cast<int>(hit(owner,1))==-99999,"absorption preserves the separate native floor");amount=450000;
        W::SaveImage saved=original;std::memcpy(saved.data()+64,reinterpret_cast<const void*>(base+0xD2CA90),0x68C0);FfxHooks::RonsoPool::SealSave(saved);
        Check(W::WriteForTests(save.c_str(),saved)&&W::LoadForTests(save.c_str(),saved,saved)&&W::CommitLoadForTests(saved),"high maxima and identities survive native save/load");
        S::TickMainThread();Check(W::Capture(state)&&W::AscensionEffect(owner,0)&&W::AscensionEffect(owner,1),"reloaded receipts remain bound to both pieces");
        const auto gil=WorkshopEconomyFixture::Gil(base);offense.revision=state.revision;offense.remove=true;
        Check(W::PreviewAscension(offense,preview)==workshop::Error::Ok&&W::CommitAscension(offense,preview)&&W::Capture(state),"confirmed removal retires damage permission");
        hp.revision=state.revision;hp.remove=true;W32(Ply(owner)+0x1C,550000);
        Check(W::PreviewAscension(hp,preview)==workshop::Error::Ok&&W::CommitAscension(hp,preview)&&W::Capture(state),"confirmed removal retires HP/MP permission");
        Check(R32(Ply(owner)+0x24)==9999&&R32(Ply(owner)+0x1C)==9999&&R32(Ply(owner)+0x20)==123,"removal clamps excess HP down without refilling MP");
        Check(WorkshopEconomyFixture::Gil(base)==gil&&!W::AscensionEffect(owner,0)&&!W::AscensionEffect(owner,1),"removal refunds nothing and grants no exclusive");if(failures)return;
    }
}
