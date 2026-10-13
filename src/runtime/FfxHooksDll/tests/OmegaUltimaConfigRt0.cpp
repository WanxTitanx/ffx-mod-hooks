#include "../hooks/OmegaUltimaConfig.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
namespace O=FfxHooks::OmegaUltima;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static std::string Read(const std::filesystem::path& p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static std::string Fake(unsigned clone=910,unsigned duplicate=99){
    std::ostringstream out;out<<"{\"schema\":\"ffx.spira.omega-ultima.v1\",\"enabled\":true,\"clone\":{\"id\":"<<clone<<",\"size\":128,\"hash\":\"123456789abcdef0\"},\"formations\":[";
    bool comma=false;unsigned row=0;
    for(unsigned field:{1076u,1077u})for(unsigned index=0;index<(field==1076?19u:9u);++index)if(index!=10){
        if(comma)out<<',';
        comma=true;
        out<<"{\"field\":"<<field<<",\"index\":"<<(row++==duplicate?0:index)<<",\"size\":128,\"hash\":\"0123456789abcdef\"}";}
    return out.str()+"]}";
}
int main(int argc,char** argv){
    O::Manifest manifest;
    Check(O::ParseManifest("{\"schema\":\"ffx.spira.omega-ultima.v1\",\"enabled\":false}",manifest)==O::ManifestCode::Disabled,"OFF does not need an asset pack");
    Check(O::ParseManifest(Fake(),manifest)==O::ManifestCode::Ready,"complete distinct normal formation catalog resolves");
    Check(O::ParseManifest(Fake(909),manifest)==O::ManifestCode::Invalid,"another monster cannot be bound as the clone");
    Check(O::ParseManifest(Fake(910,1),manifest)==O::ManifestCode::Invalid,"duplicate formation cannot hide a missing entry");
    auto text=Fake();text.replace(text.find("1076"),4,"1073");Check(O::ParseManifest(text,manifest)==O::ManifestCode::Invalid,"another region cannot be relabeled Omega");
    text=Fake();text.replace(text.find("123456789abcdef0"),16,"123456789abcdeg0");Check(O::ParseManifest(text,manifest)==O::ManifestCode::Invalid,"non-hex asset identity rejected");
    text=Fake();text.replace(text.find("\"index\":0"),9,"\"index\":10");Check(O::ParseManifest(text,manifest)==O::ManifestCode::Invalid,"original boss is never admitted by manifest");
    Check(O::ParseManifest(Fake()+"x",manifest)==O::ManifestCode::Invalid,"trailing malformed data rejected");
    if(argc==3){
        text=Read(argv[1]);Check(!text.empty(),"actual private manifest read");
        Check(O::ParseManifest(text,manifest)==O::ManifestCode::Disabled,"generated real bindings stay disabled");
        const auto at=text.find("false");if(at==std::string::npos)return 2;text.replace(at,5,"true");
        Check(O::ParseManifest(text,manifest)==O::ManifestCode::Ready,"exact Editor asset set resolves all 27 formations");
        unsigned views=0;const std::filesystem::path battle=argv[2];
        for(const auto& proof:manifest.formations){char name[16]{};std::snprintf(name,sizeof(name),"omeg%02u_%02u",proof.field-1076,proof.index);
            const auto file=Read(battle/"btl"/name/(std::string(name)+".bin"));O::Layout layout;
            Check(O::Inspect(proof,reinterpret_cast<const unsigned char*>(file.data()),file.size(),layout),"installed formation bytes and active slots match their binding");
            for(unsigned slot=0;slot<8;++slot)if(layout.eligible&(1u<<slot)){
                std::vector<unsigned char> out;Check(O::Build(proof,reinterpret_cast<const unsigned char*>(file.data()),file.size(),slot,out),"every installed eligible slot can carry the real clone ID");
                for(unsigned i=0;i<out.size();++i)if(i!=layout.slots+slot*2&&i!=layout.slots+slot*2+1)
                    Check(out[i]==static_cast<unsigned char>(file[i]),"native scripts, positions, other actors and all nonselected bytes are preserved");
                Check(O::Word(out.data()+layout.slots+slot*2)==O::CloneNative,"replacement is m910, independent of the original species");++views;
            }
        }
        Check(views==129,"27 formations expose exactly 129 reviewed initial combatant choices");
    }
    std::printf("OmegaUltimaConfigRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
