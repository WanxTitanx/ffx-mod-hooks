#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../hooks/NativeLanguageHook.h"
#include "MusicPeFixture.inc"
namespace L=FfxHooks::NativeLanguage;
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* image=MapPe(Read(argv[1]));if(!image)return 2;
    const auto base=reinterpret_cast<uintptr_t>(image);
    Check(!L::Start(0,{}),"unsupported executable cannot apply language changes");
    Check(L::Start(base,{}) && L::Status()==L::Result::Default,"default language requests own no mutation");
    for(unsigned voice=0;voice<3;++voice)for(unsigned sfx=0;sfx<3;++sfx)for(unsigned video=0;video<3;++video){
        const L::Settings settings{static_cast<L::Choice>(voice),static_cast<L::Choice>(sfx),static_cast<L::Choice>(video)};
        const auto plan=L::BuildPlan(settings);
        for(unsigned i=0;i<plan.count;++i)Check(std::strcmp(reinterpret_cast<const char*>(image+plan.patches[i].rva),plan.patches[i].original)==0,"exact native asset reference matches before admission");
        Check(L::Start(base,settings),"selected asset-language transaction applies to the exact PE image");
        for(unsigned i=0;i<plan.count;++i)Check(std::strcmp(reinterpret_cast<const char*>(image+plan.patches[i].rva),plan.patches[i].replacement)==0,"native string consumer observes selected language");
        const auto applied=L::Applied();Check(applied.voice==settings.voice&&applied.sfx==settings.sfx&&applied.video==settings.video,"status reports the committed startup choices");
        L::Stop();
        for(unsigned i=0;i<plan.count;++i)Check(std::strcmp(reinterpret_cast<const char*>(image+plan.patches[i].rva),plan.patches[i].original)==0,"normal stop restores each exact original reference");
    }
    L::Settings japanese{L::Choice::Japanese,L::Choice::GameDefault,L::Choice::GameDefault};
    const auto plan=L::BuildPlan(japanese);auto* first=reinterpret_cast<char*>(image+plan.patches[0].rva);
    DWORD protection=0;VirtualProtect(first,64,PAGE_READWRITE,&protection);const char original=*first;*first='!';
    Check(!L::Start(base,japanese)&&L::Status()==L::Result::Conflict,"foreign preexisting language references reject the whole transaction");*first=original;
    VirtualProtect(first,64,PAGE_READONLY,&protection);
    Check(L::Start(base,japanese),"read-only image pages support bounded transactional writes");
    MEMORY_BASIC_INFORMATION info{};VirtualQuery(first,&info,sizeof(info));Check(info.Protect==PAGE_READONLY,"applying restores the original read-only protection");
    VirtualProtect(first,64,PAGE_READWRITE,&protection);const char applied=*first;*first='!';VirtualProtect(first,64,PAGE_READONLY,&protection);
    L::Stop();Check(*first=='!'&&L::Status()==L::Result::Conflict,"teardown preserves another writer's subsequent edit");
    VirtualProtect(first,64,PAGE_READWRITE,&protection);*first=applied;VirtualProtect(first,64,PAGE_READONLY,&protection);
    L::Stop();Check(std::strcmp(first,plan.patches[0].original)==0,"restoration can complete when ownership returns");
    VirtualQuery(first,&info,sizeof(info));Check(info.Protect==PAGE_READONLY,"restoration retains the original page protection");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("NativeLanguageWindowsRt1: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
