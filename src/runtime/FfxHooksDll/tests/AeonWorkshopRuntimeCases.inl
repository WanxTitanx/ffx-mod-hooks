// Isolated inventory/upgrade producers and effect loops. The field bridge skips
// unrelated Sphere Grid/growth setup and final base-stat stores, as in the
// existing WorkshopFieldFixture. Battle aggregation still runs its whole body.
#include "../hooks/AeonAscensionBridge.h"
namespace PaidMappingFixture {
inline FfxHooks::AeonAscension::Mapping mapping{};
inline bool Read(FfxHooks::AeonAscension::Mapping& out) noexcept {out=mapping;return FfxHooks::AeonAscension::ValidMapping(out);}
inline const FfxHooks::AeonAscension::Provider provider{Read};
}
static const unsigned char* __cdecl AeonNameStub(){static const unsigned char name[]={0};return name;}
struct AeonNameBridge {
    std::uintptr_t at;unsigned char before[5]{};bool valid=false;
    explicit AeonNameBridge(std::uintptr_t base):at(base+(::FfxHooks::ExecutableProfile::Rva<0x3ABE10>())){
        std::memcpy(before,reinterpret_cast<void*>(at),5);unsigned char jump[]={0xE9,0,0,0,0};
        const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&AeonNameStub)-at-5);std::memcpy(jump+1,&delta,4);
        valid=WorkshopFieldFixture::Write(at,jump,5);
    }
    ~AeonNameBridge(){if(valid)WorkshopFieldFixture::Write(at,before,5);}
};
struct AeonFieldBridge {
    std::uintptr_t base;unsigned char* code=nullptr;unsigned char before[2][6]{};unsigned applied=0;
    explicit AeonFieldBridge(std::uintptr_t address):base(address){
        code=static_cast<unsigned char*>(VirtualAlloc(nullptr,512,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if(!code)return;
        std::vector<unsigned char> bytes={0x53,0x56,0x57};
        auto imm=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(v>>(8*i)));};
        for(int offset=-0x74;offset<=-8;offset+=4){bytes.insert(bytes.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(offset));imm(offset>=-0x3C?100:0);}
        bytes.insert(bytes.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(-0x90));imm(0);
        bytes.insert(bytes.end(),{0x8B,0x5D,0x08,0x69,0xDB});imm(0x94);
        bytes.insert(bytes.end(),{0x81,0xC3});imm(static_cast<std::uint32_t>(base+(::FfxHooks::ExecutableProfile::Rva<0xD3205C>())));
        bytes.insert(bytes.end(),{0x31,0xC0,0xE9});imm(static_cast<std::uint32_t>(base+(::FfxHooks::ExecutableProfile::Rva<0x386765>())-reinterpret_cast<std::uintptr_t>(code)-bytes.size()-4));
        const auto tail=bytes.size();bytes.insert(bytes.end(),{0x8B,0x45,0xEC,0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3});
        std::memcpy(code,bytes.data(),bytes.size());FlushInstructionCache(GetCurrentProcess(),code,bytes.size());
        for(unsigned i=0;i<2;++i){const auto at=base+(i?(::FfxHooks::ExecutableProfile::Rva<0x386850>()):(::FfxHooks::ExecutableProfile::Rva<0x3861B9>()));const unsigned size=i?6:5;
            std::memcpy(before[i],reinterpret_cast<void*>(at),size);unsigned char jump[]={0xE9,0,0,0,0,0x90};
            const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(code)+(i?tail:0)-at-5);std::memcpy(jump+1,&delta,4);
            if(!WorkshopFieldFixture::Write(at,jump,size))return;++applied;
        }
    }
    ~AeonFieldBridge(){while(applied){const unsigned i=--applied;WorkshopFieldFixture::Write(base+(i?(::FfxHooks::ExecutableProfile::Rva<0x386850>()):(::FfxHooks::ExecutableProfile::Rva<0x3861B9>())),before[i],i?6:5);}if(code)VirtualFree(code,0,MEM_RELEASE);}
};
static std::uintptr_t aeonDamageBase=0;
static unsigned __cdecl AeonDamageEndpoint(unsigned,void*,unsigned,void*,const void* command,unsigned,void* info,unsigned,unsigned,unsigned,unsigned){
    // Isolate hit/critical setup, keeping the real installed producer shim and
    // native Protect/Shell functions on this exact damage-info frame.
    const bool magic=(static_cast<const unsigned char*>(command)[0x20]&3)==2;
    unsigned flags=0;int divisor=0;
    return static_cast<unsigned>(reinterpret_cast<int(__cdecl*)(const void*,unsigned*,int*,const void*,int)>(aeonDamageBase+(magic?(::FfxHooks::ExecutableProfile::Rva<0x38AE80>()):(::FfxHooks::ExecutableProfile::Rva<0x38AE00>())))(command,&flags,&divisor,info,1000));
}
static void AeonRuntimeCases(std::uintptr_t base,const std::wstring& directory,SaveImage image,const char* kernelPath){
    std::puts("CASE Aeon runtime native fixture");
    WorkshopEconomyFixture::Seed(image,50000000);
    AeonFieldBridge fieldBridge(base);Check(fieldBridge.applied==2,"Aeon field fixture isolates only unrelated growth setup and final stores");if(failures)return;
    AeonNameBridge names(base);Check(names.valid,"private fixture substitutes the unloaded equipment-name table only");if(failures)return;
    std::ifstream kernelFile(kernelPath,std::ios::binary);
    std::vector<unsigned char> kernel((std::istreambuf_iterator<char>(kernelFile)),{});
    Check(kernel.size()>=20+131*108,"Aeon fixture uses the real ability kernel");if(failures)return;
    const auto kernelAddress=reinterpret_cast<std::uintptr_t>(kernel.data());
    std::memcpy(reinterpret_cast<void*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2A944>())),&kernelAddress,4);
    static unsigned char actors[31][0xF90]{};
    const auto actorAddress=reinterpret_cast<std::uintptr_t>(actors);
    std::memcpy(reinterpret_cast<void*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD334CC>())),&actorAddress,4);
    for(unsigned i=0;i<18;++i){const auto id=static_cast<std::uint16_t>(i);std::memcpy(actors[i]+0xE,&id,2);}
    image[64+0xC6C]=0;
    auto* ply=image.data()+64+0x55CC+8*0x94;ply[0x2C]|=0x10;
    const unsigned weapon=ply[0x2D],armor=ply[0x2E];
    Check(weapon<200&&armor<200,"native Valefor gear slots are in the persistent inventory");if(failures)return;
    const std::uint16_t unpowered[]={0x807B,255,255,255};
    std::memcpy(image.data()+0x44DC+weapon*22+14,unpowered,8);
    const std::uint16_t armorWords[]={0x8000,0x806A,0x8017,0x8018};
    std::memcpy(image.data()+0x44DC+armor*22+14,armorWords,8);
    FfxHooks::RonsoPool::SealSave(image);
    const auto path=directory+L"\\ffx_096";
    workshop::State s{};Check(LoadForTests(path.c_str(),image,image)&&CommitLoadForTests(image)&&Capture(s),"Aeon fixture loads through native save association");if(failures)return;
    workshop::Request r{};r.op=workshop::Op::UnlockFifth;r.slot=static_cast<std::uint16_t>(weapon);r.pieceId=s.pieces[weapon].id;r.revision=s.revision;
    workshop::Plan p{};Check(Preview(r,p)==workshop::Error::Locked,"unpowered Celestial denies live Aeon edits");
    const auto identity=s.pieces[weapon].id;
    const auto legend=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(base+(::FfxHooks::ExecutableProfile::Rva<0x4C3150>()));
    std::puts("CASE real Celestial producer");
    Check(legend(8,1)==1,"real Celestial producer applies the first Aeon upgrade");
    Check(Capture(s)&&s.pieces[weapon].id==identity&&workshop::Ability(s.pieces[weapon],0)==0x807B,
          "native Celestial writes retain the managed Aeon identity and immunity");if(failures)return;
    *reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2CA90>())+0xC6C)=2;
    r.revision=s.revision;
    Check(Preview(r,p)==workshop::Error::Ok,"the actual applied-Crest byte admits the equipped Aeon weapon");
    if(failures)return;
    const auto beforeUnlock=s;
    *reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2CA90>())+0xC6C)=0;
    Check(!Commit(r,p)&&Capture(s)&&std::memcmp(&s,&beforeUnlock,sizeof(s))==0,"Crest drift rejects confirmation without consuming materials");
    *reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2CA90>())+0xC6C)=2;
    Check(Commit(r,p)&&Capture(s)&&s.pieces[weapon].fifthUnlocked,"native transaction unlocks the Aeon fifth slot");
    if(failures)return;
    for(unsigned i=75;i<=79;++i)Check(s.items[i]+4==beforeUnlock.items[i],"native fifth unlock deducts four of each sphere");
    r.op=workshop::Op::Clear;r.value=0;r.revision=s.revision;
    Check(Preview(r,p)==workshop::Error::Protected,"native preview refuses Aeon Immunity removal");
    unsigned char donor[22]{};donor[2]=1;donor[6]=255;donor[11]=4;
    const std::uint16_t donorWords[]={0x8062,0x8066,255,255};std::memcpy(donor+14,donorWords,8);
    const auto create=reinterpret_cast<unsigned(__cdecl*)(const void*)>(base+(::FfxHooks::ExecutableProfile::Rva<0x3AB930>()));
    const unsigned created=create(donor);
    Check(created>=0x5000&&created<0x50C8&&Capture(s),"real native producer supplies a regular Fusion donor");if(failures)return;
    r={};r.op=workshop::Op::Fuse;r.slot=static_cast<std::uint16_t>(weapon);r.pieceId=s.pieces[weapon].id;r.revision=s.revision;
    r.other=static_cast<std::uint16_t>(created&0xFFF);r.otherId=s.pieces[r.other].id;r.count=2;r.from[1]=1;r.to[0]=2;r.to[1]=3;
    Check(Preview(r,p)==workshop::Error::Ok&&p.gilCost==40000&&Commit(r,p)&&Capture(s),"equipped Aeon receives two abilities for doubled Gil while only its ordinary donor is destroyed");if(failures)return;
    auto step=[&](workshop::Op op,unsigned slot,unsigned value=0){
        if(!Capture(s))return false;r={};r.op=op;r.slot=static_cast<std::uint16_t>(slot);r.pieceId=s.pieces[slot].id;r.revision=s.revision;r.value=static_cast<std::uint16_t>(value);
        return Preview(r,p)==workshop::Error::Ok&&Commit(r,p)&&Capture(s);
    };
    Check(step(workshop::Op::SetFifth,weapon,0x800B)&&p.gilCost==400000,"Aeon fifth customization charges 400000 Gil");
    const auto field=reinterpret_cast<int(__cdecl*)(unsigned)>(base+(::FfxHooks::ExecutableProfile::Rva<0x3861B0>()));
    Check(field(8)==103,"real field loop sees the unrefined Aeon Strength ability");
    FfxHooks::Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=1\n","C:\\private-aeon-refine.ini");
    Check(step(workshop::Op::Refine,weapon)&&workshop::AbilityRank(s.pieces[weapon],0)==0&&field(8)==106,
          "Aeon field effects include native and fifth ranks without refining immunity");
    Check(step(workshop::Op::UnlockFifth,armor)&&step(workshop::Op::SetFifth,armor,0x8055),"Aeon armor supports its own fifth slot and Auto-Protect");
    const auto aggregate=reinterpret_cast<int(__cdecl*)(unsigned)>(base+(::FfxHooks::ExecutableProfile::Rva<0x39C610>()));
    aggregate(8);Check((actors[8][0x632]&0x10)!=0,"complete native battle aggregator applies fifth Auto-Protect to the Aeon");
    Check(step(workshop::Op::Refine,armor),"Aeon armor refinement is admitted while equipped");if(failures)return;
    aeonDamageBase=base;DamageProducerForTests(reinterpret_cast<void*>(&AeonDamageEndpoint));
    using Damage=int(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
    const auto damage=reinterpret_cast<Damage>(base+(::FfxHooks::ExecutableProfile::Rva<0x38E680>()));
    unsigned char command[96]{},info[128]{};command[0x20]=1;info[0xB]=1;
    Check(damage(0,nullptr,8,nullptr,command,0,info,0,0,0,0)==495,"native Protect and producer identity apply the refined Aeon reduction");
    Check(damage(0,nullptr,9,nullptr,command,0,info,0,0,0,0)==500,"another Aeon cannot borrow the selected Aeon's armor refinement");
    info[0xB]=0;Check(damage(0,nullptr,8,nullptr,command,0,info,0,0,0,0)==1000,"refinement never invents an absent Protect status");
    namespace A=FfxHooks::AeonAscension;
    PaidMappingFixture::mapping.enabled=true;PaidMappingFixture::mapping.proof=0xA510u;
    PaidMappingFixture::mapping.replacements={0x8017,0x8018,0x8019};
    Check(A::RegisterProvider(&PaidMappingFixture::provider),"controller fixture registers read-only mapping, not a cap consumer");
    A::Request upgrade{};upgrade.slot=armor;upgrade.position=2;upgrade.effect=0;upgrade.replace=true;
    upgrade.revision=s.revision;upgrade.pieceId=s.pieces[armor].id;A::Plan purchase{};
    const auto beforePurchase=s;const auto gilBefore=WorkshopEconomyFixture::Gil(base);
    Check(PreviewAscension(upgrade,purchase)==workshop::Error::Ok&&Capture(s)&&
          !std::memcmp(&s,&beforePurchase,sizeof(s)),"native paid preview preserves the existing inventory");
    ++PaidMappingFixture::mapping.proof;
    Check(!CommitAscension(upgrade,purchase),"mapping drift rejects the previous confirmation");
    --PaidMappingFixture::mapping.proof;
    Check(CommitAscension(upgrade,purchase)&&Capture(s)&&AscensionEffect(8,0),"controller admits permission after the durable payment");
    if(failures)return;
    Check(WorkshopEconomyFixture::Gil(base)+10000000==gilBefore&&s.items[108]+60==beforePurchase.items[108]&&
          s.items[80]+2==beforePurchase.items[80],"controller debits exact Gil and material quantities once");
    Check(!CommitAscension(upgrade,purchase)&&!AscensionEffect(9,0)&&!AscensionEffect(7,0),"replay and other owners cannot borrow the permission");
    PaidMappingFixture::mapping.enabled=false;Check(!AscensionEffect(8,0),"provider OFF suspends the paid permission");PaidMappingFixture::mapping.enabled=true;
    SaveImage saved=image;std::memcpy(saved.data()+64,reinterpret_cast<const void*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD2CA90>())),0x68C0);FfxHooks::RonsoPool::SealSave(saved);
    const auto prior=s;Check(WriteForTests(path.c_str(),saved),"native save persists Aeon weapons/armor and extension together");
    Check(LoadForTests(path.c_str(),saved,saved)&&CommitLoadForTests(saved)&&Capture(s)&&
          std::memcmp(&s.pieces[weapon],&prior.pieces[weapon],sizeof(workshop::Piece))==0&&
          std::memcmp(&s.pieces[armor],&prior.pieces[armor],sizeof(workshop::Piece))==0,
          "native reload preserves Aeon piece IDs, immunity, fifths and all ranks");
    Check(AscensionEffect(8,0),"receipt survives save/load and a changed session revision");
    Check(legend(8,2)==1&&Capture(s)&&s.pieces[weapon].id==identity&&s.pieces[weapon].fifth==prior.pieces[weapon].fifth&&
          s.pieces[weapon].abilities[4]==prior.pieces[weapon].abilities[4]&&s.pieces[weapon].ranks[4]==prior.pieces[weapon].ranks[4],
          "a repeated native legend producer retains the independent fifth identity and rank");
    Check(workshop::Ability(s.pieces[weapon],0)==0x807B,"the native producer and Workshop retain Aeon Immunity");
    upgrade.revision=s.revision;upgrade.remove=true;A::Plan removal{};const auto paidGil=WorkshopEconomyFixture::Gil(base);
    Check(PreviewAscension(upgrade,removal)==workshop::Error::Ok&&CommitAscension(upgrade,removal)&&Capture(s)&&
          !AscensionEffect(8,0)&&workshop::Ability(s.pieces[armor],2)==workshop::Empty,"dedicated removal retires the paid slot and receipt together");
    Check(WorkshopEconomyFixture::Gil(base)==paidGil,"dedicated removal does not refund the purchase");
    A::UnregisterProvider(&PaidMappingFixture::provider);
}
