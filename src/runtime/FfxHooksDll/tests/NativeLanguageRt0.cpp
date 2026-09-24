#include "../hooks/NativeLanguageCore.h"
#include <cstdio>
using namespace FfxHooks::NativeLanguage;
int main(){
    unsigned checks=0,failed=0;
    auto check=[&](bool ok){++checks;if(!ok)++failed;};
    check(BuildPlan({}).count==0);
    check(!BuildPlan({static_cast<Choice>(3),Choice::GameDefault,Choice::GameDefault}).valid);
    for(unsigned voice=0;voice<3;++voice)for(unsigned sfx=0;sfx<3;++sfx)for(unsigned video=0;video<3;++video){
        const auto p=BuildPlan({static_cast<Choice>(voice),static_cast<Choice>(sfx),static_cast<Choice>(video)});
        check(p.valid && p.count==(voice?6u:0u)+(sfx?2u:0u)+(video?2u:0u)+(video==2?1u:0u));
        for(unsigned i=0;i<p.count;++i){
            check(p.patches[i].rva>=0x740000 && p.patches[i].rva<0x750000);
            check(std::strlen(p.patches[i].replacement)<=std::strlen(p.patches[i].original));
            for(unsigned j=i+1;j<p.count;++j)check(p.patches[i].rva!=p.patches[j].rva);
        }
    }
    std::printf("NativeLanguageRt0: %u/%u passed; failures=%u\n",checks-failed,checks,failed);return failed?1:0;
}
