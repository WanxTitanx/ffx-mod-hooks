// Jarvis-HOOK: independent OFF/ON switches through both real module adapters.
// Reuse the private native equipment/clamp fixture, never a game session.
#define main SpiraHarnessMain
#include "SpiraRuntimeRt1.cpp"
#undef main
#include "../hooks/ElementalRuntime.h"
namespace E=FfxHooks::ElementalDominion;
static std::string CompositionHash(const void* bytes,std::size_t size){
    W::Hash hash{};if(!W::Fingerprint(bytes,size,hash))return {};
    constexpr char hex[]="0123456789abcdef";std::string out;
    for(const auto b:hash){out+=hex[b>>4];out+=hex[b&15];}return out;
}
int main(int argc,char** argv){
    if(argc!=5)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool elemental=std::strcmp(argv[4],"elemental-only")==0||std::strcmp(argv[4],"both-on")==0;
    const bool spira=std::strcmp(argv[4],"spira-only")==0||std::strcmp(argv[4],"both-on")==0;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private PE relocates");
    std::ifstream input(argv[2],std::ios::binary);std::vector<unsigned char> source((std::istreambuf_iterator<char>(input)),{});
    auto kernel=SpiraKernelFixture::Build(source);std::vector<unsigned char> commands(20+68*96+1);
    W32(commands.data(),1);W16(commands.data()+10,67);W16(commands.data()+12,96);W16(commands.data()+14,68*96);W32(commands.data()+16,20);
    auto* row=commands.data()+20+66*96;row[0x19]=255;row[0x20]=2;row[0x23]=1;row[0x28]=4;row[0x2A]=16;
    std::array<unsigned,8> language{};language[1]=1;
    const auto kp=reinterpret_cast<std::uintptr_t>(kernel.data()),ap=reinterpret_cast<std::uintptr_t>(actors.data());
    const auto cp=reinterpret_cast<std::uintptr_t>(commands.data()),lp=reinterpret_cast<std::uintptr_t>(language.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&kp,4);W16(reinterpret_cast<unsigned char*>(base+0xD2A970),static_cast<unsigned>(kernel.size()));
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&ap,4);std::memcpy(reinterpret_cast<void*>(base+0xD2A92C),&cp,4);
    std::memcpy(reinterpret_cast<void*>(base+0x8DED48),&lp,4);
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    for(unsigned owner=0;owner<18;++owner){auto* actor=Actor(owner);W16(actor+0xC,owner);W16(actor+0xE,owner);actor[0xDC8]=1;
        W32(actor+0x594,1000);W32(actor+0x5D0,1000);Equip(owner,0,{});Equip(owner,1,{});}
    std::string pack=R"({"schema":"ffx.mod007.elements.v1","package_id":"tests.composition","version":1,
      "exe_sha256":"78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced",
      "requires":["mod007.registry.v1","mod007.context.v1","mod007.spell-cap.v1"],
      "fallback":"native-unmodified","elements":[)";
    for(unsigned i=0;i<8;++i){if(i)pack+=",";pack+="{\"key\":\"native.e"+std::to_string(i)+
        "\",\"label_key\":\"label.e"+std::to_string(i)+"\",\"label\":\"Element\",\"rgb\":16777215,\"native_bit\":"+std::to_string(1u<<i)+"}";}
    pack+="],\"banks\":[{\"key\":\"table.command\",\"kind\":\"command\",\"locale\":\"us\",\"bytes\":"+
        std::to_string(commands.size())+",\"sha256\":\""+CompositionHash(commands.data(),commands.size())+
        "\",\"sections\":[{\"first\":0,\"last\":67,\"width\":96,\"offset\":20}]}],\"commands\":[{\"key\":\"spell.composition\",\"bank\":\"table.command\",\"index\":66,\"row_sha256\":\""+
        CompositionHash(row,96)+"\",\"elements\":[],\"spell\":\"native_magic\"}],\"profiles\":[],\"equipment\":[]}";
    unsigned char before[16]{};std::memcpy(before,reinterpret_cast<void*>(base+0x38E680),16);
    Check(S::Prepare(base,{spira,spira},false,nullptr)==spira,"Spira switch owns only its requested subscriptions");
    E::RuntimeOptions options{};options.magicBdl=elemental;
    Check(E::PrepareText(base,options,pack,false,nullptr)==elemental,"Elemental switch owns only its requested subscriptions");
    if(!elemental&&!spira){
        Check(!B::Required()&&!std::memcmp(before,reinterpret_cast<void*>(base+0x38E680),16),"both OFF leave the native producer untouched");
    }else{
        Check(FfxHooks::InstallNovaSuperDamageHook(base,false,false,false,nullptr).ok,"finite clamp starts with legacy Nova OFF");
        const std::wstring directory(argv[3],argv[3]+std::strlen(argv[3]));
        Check(W::StartForTests(base,false,directory.c_str(),nullptr),"shared producer starts independently of Workshop gameplay");
        if(spira){Check(S::Activate(),"Spira activates");S::TickMainThread();Check(S::Ready(),"Spira admits its loaded kernel");}
        if(elemental){Check(E::Activate(),"Elemental activates");E::TickMainThread();Check(E::RuntimeState().code==E::RuntimeCode::Ready,"Elemental admits its loaded bank");}
        if(failures)return 1;
        W::DamageProducerForTests(reinterpret_cast<void*>(&DamageEndpoint));
        Equip(0,1,{0x8098});W16(Actor(0)+0x6BE,0x800);unsigned char info[44]{};
        using Damage=unsigned(__cdecl*)(unsigned,void*,unsigned,void*,const void*,unsigned,void*,unsigned,unsigned,unsigned,unsigned);
        const auto damage=reinterpret_cast<Damage>(base+0x38E680);
        const auto hit=[&](){return static_cast<int>(damage(0,Actor(0),1,Actor(1),row,0x3042,info,0,0,0,0));};
        amount=450000;Check(hit()==(elemental?(spira?675000:450000):99999),"Bargain and Magic BDL compose once before the finite ceiling");
        amount=1200000;Check(hit()==(elemental?999999:99999),"simultaneous requests do not add or multiply ceilings");
        W16(Actor(0)+0x6BE,0);Check(hit()==9999,"neither enabled module grants BDL to an unentitled attacker");
        W16(Actor(0)+0x6BE,0x800);amount=-500;Check(hit()==-500,"signed absorption bypasses Bargain and positive caps");
        amount=1000;component=2;Check(hit()==1000,"MP outcomes bypass the positive HP policy");component=3;
        E::RequestStop();amount=450000;Check(hit()==99999,"stopping Elemental leaves Spira and the native finite cap usable");
        S::RequestStop();amount=1000;Check(hit()==1000,"stopping both subscribers leaves the original producer usable");
        W::RequestStop();FfxHooks::RemoveNovaSuperDamageHook(nullptr);
    }
    std::printf("MOD_COMPOSITION %s %u/%u passed\n",argv[4],checks-failures,checks);return failures?1:0;
}
