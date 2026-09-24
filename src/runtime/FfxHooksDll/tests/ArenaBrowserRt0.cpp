#include "../hooks/CustomMixUltraCore.h"
#if __has_include("../hooks/ArenaBrowserCore.h")
#include "../hooks/ArenaBrowserCore.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

// Compile the actual production summary function, not a rewritten imitation.
namespace FfxHooks::CustomMixUltra::Runtime {
struct StatusSnapshot { unsigned code=0; };
static StatusSnapshot ProductionStatus(){return {};}
static const char* StatusName(unsigned){return "READY";}
}
static FfxHooks::CustomMixUltra::SelectionInput g_arenaPlusUltraSelection;
static unsigned g_arenaPlusMixRequiredSlots=0;
static char g_arenaPlusUltraPreview[64]={};
const char* ArenaPlus_UltraPreviewSlotName(uint16_t id){return FfxHooks::ArenaMonsters::Name(id);}
#include "ArenaPreviewUnderTest.inc"

static unsigned invalidParameters=0,checks=0,failures=0;
static void InvalidParameter(const wchar_t*,const wchar_t*,const wchar_t*,unsigned,uintptr_t){++invalidParameters;}
static void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);}}
int main(){
    const auto previous=_set_invalid_parameter_handler(&InvalidParameter);
    constexpr unsigned monsters[]={304,309,308,306,281,276,318,105};
    for(unsigned count=1;count<=8;++count){
        g_arenaPlusUltraSelection={};g_arenaPlusUltraSelection.activationCount=static_cast<uint8_t>(count);
        for(unsigned i=0;i<count;++i)g_arenaPlusUltraSelection.activations[i]=static_cast<FfxHooks::CustomMixUltra::MonsterChoice>(0x100u+monsters[i]);
        const auto before=invalidParameters;
        ArenaPlus_BuildUltraPreview();
        Check(invalidParameters==before,"Back with a varied Arena roster never invokes the fatal CRT invalid-parameter path");
        Check(std::strncmp(g_arenaPlusUltraPreview,"Slots ",6)==0,"the summary retains its slot-count prefix");
        Check(std::strlen(g_arenaPlusUltraPreview)<sizeof(g_arenaPlusUltraPreview),"the summary stays inside the native label capacity");
    }
    Check(FfxHooks::ArenaBrowser::Matches("  MALBORO  ","Malboro Menace","monster_304","Arena Creations"),"search ignores ASCII case and surrounding spaces");
    Check(FfxHooks::ArenaBrowser::Matches("arena 304","Malboro Menace","monster_304","Arena Creations"),"search can combine category and stable ID terms");
    Check(!FfxHooks::ArenaBrowser::Matches("malboro ifrit","Malboro Menace","monster_304","Arena Creations"),"every query term must match");
    _set_invalid_parameter_handler(previous);
    BOOL animation=FALSE;const BOOL queried=SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
    std::printf("CLIENT_AREA_ANIMATION query=%d value=%d\n",queried,animation);
    std::printf("Arena browser RT0: %u checks, %u failures; CRT invalid calls=%u\n",checks,failures,invalidParameters);
    return failures?1:0;
}
