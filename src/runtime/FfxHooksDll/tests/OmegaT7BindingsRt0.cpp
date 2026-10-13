#include "../hooks/OmegaT7Config.h"
#include <cstdio>
#include <fstream>
#include <iterator>
namespace O=FfxHooks::OmegaT7;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
int main(int argc,char** argv){if(argc!=2)return 2;std::ifstream file(argv[1]);std::string text((std::istreambuf_iterator<char>(file)),{});O::Bindings bindings;
    Check(O::ParseBindings(text,bindings)==O::BindingCode::Disabled&&!bindings.Ready(),"generated production bindings are OFF");
    const auto enabled=text.find("false");if(enabled==std::string::npos)return 2;text.replace(enabled,5,"true");
    Check(O::ParseBindings(text,bindings)==O::BindingCode::Ready&&bindings.Ready(),"all twenty-six installed command witnesses resolve");
    for(unsigned skill=0;skill<26;++skill){const auto key=static_cast<O::Skill>(skill);const auto row=bindings.Get(key).row;
        for(unsigned byte=0;byte<16;++byte){auto translated=row;translated[byte]^=1;Check(bindings.Matches(key,translated),"localized text offsets are intentionally outside the gameplay identity");}
        for(unsigned byte=16;byte<92;++byte){auto altered=row;altered[byte]^=1;Check(!bindings.Matches(key,altered),"every changed gameplay or VFX byte rejects admission");}}
    auto duplicate=bindings;duplicate.rows[25].id=duplicate.rows[0].id;Check(!duplicate.Ready(),"duplicate command IDs reject the entire pack");
    auto row=bindings.Get(O::Skill::MightyGuard).row;row[0x47+O::DurationIndex[0]]=255;Check(!O::Semantics(O::Skill::MightyGuard,row),"undispellable support duration is rejected");
    row=bindings.Get(O::Skill::MightyGuard).row;row[0x1F]|=4;Check(!O::Semantics(O::Skill::MightyGuard,row),"monster group support cannot be reflected");
    row=bindings.Get(O::Skill::Firaja).row;row[0x2E + 15]=100;Check(!O::Semantics(O::Skill::Firaja,row),"offensive commands cannot carry a hidden beneficial status");
    row=bindings.Get(O::Skill::Hastega).row;row[0x2A]=1;Check(!O::Semantics(O::Skill::Hastega,row),"group support cannot carry damage");
    struct Protocol {O::Skill skill;unsigned char formula,type,flags;};
    for(const auto& native:std::array<Protocol,14>{{
        {O::Skill::Bio,3,0,2},{O::Skill::Silence,3,0,2},{O::Skill::Darkness,3,0,2},{O::Skill::Zombie,3,0,2},
        {O::Skill::Reflect,3,0,2},{O::Skill::MightyGuard,3,0,2},{O::Skill::Regen,3,0,2},
        {O::Skill::Slowga,13,4,2},{O::Skill::Hastega,13,4,18},
        {O::Skill::Focus,0,0,0},{O::Skill::Cheer,0,0,0},{O::Skill::Luck,0,0,0},
        {O::Skill::MentalBreak,1,1,13},{O::Skill::ArmorBreak,1,1,13}}}){
        auto corrected=bindings.Get(native.skill).row;corrected[0x28]=native.formula;corrected[0x23]=native.type;corrected[0x20]=native.flags;
        Check(O::Semantics(native.skill,corrected),"native support/debuff damage protocol is admitted without weakening its status contract");
        corrected[0x23]=8;Check(!O::Semantics(native.skill,corrected),"unrelated resource damage type is not a support protocol");
    }
    for(unsigned id:{0x61B9u,0x61BAu,0x61BBu,0x61DEu,0x61DFu,0x61E0u})Check(O::NativeOd(id),"native ODs stay separate from T7 extras");
    const char* invalid[]={"{}","{\"schema\":\"ffx.omega-t7.v1\",\"enabled\":true,\"commands\":[]}","{\"schema\":\"ffx.omega-t7.v1\",\"enabled\":true,\"enabled\":false}"};
    for(const char* bad:invalid){Check(O::ParseBindings(bad,bindings)!=O::BindingCode::Ready&&!bindings.Ready(),"malformed or incomplete pack clears previous admission");}
    std::printf("OmegaT7BindingsRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
