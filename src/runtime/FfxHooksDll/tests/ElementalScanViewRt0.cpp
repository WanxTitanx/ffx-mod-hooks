// Jarvis-HOOK: a read-only display snapshot retains the resolver's numbers.
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/ElementalScanView.h")
#include "../hooks/ElementalScanView.h"
namespace S=FfxHooks::ElementalScanView;
static unsigned checks=0,failures=0,reads=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
static bool Read(unsigned actor,unsigned page,S::Snapshot& output) noexcept {
    ++reads;if(actor!=18)return false;output={};output.total=10;output.page=page;output.count=10;
    auto& row=output.rows[0];std::memcpy(row.label,"Poison",7);row.baseBp=10000;row.effectiveBp=12500;
    row.equipmentBp=-2500;row.imperil=2;row.imperilTurns=3;row.ward=0;row.nul=1;row.nulTurns=2;
    for(unsigned i=1;i<output.count;++i)output.rows[i]=row;
    return true;
}
int main(){
    S::Snapshot snapshot{};
    Check(!S::Capture(18,0,snapshot),"an absent runtime cannot manufacture a numerical view");
    const S::Provider first{Read},second{Read};
    Check(S::Register(&first)&&S::Register(&first)&&!S::Register(&second),"only one immutable numerical source owns the view");
    Check(S::Capture(18,0,snapshot)&&reads==1&&snapshot.rows[0].effectiveBp==12500,
          "display snapshot retains exact effective affinity rather than recomputing native masks");
    S::Lines lines{};Check(S::Format(snapshot.rows[0],lines),"valid source values format without changing their units");
    Check(!std::strcmp(lines.title,"Poison 100% > 125%"),"the main row shows base and effective damage percentages");
    Check(std::strstr(lines.effects,"Gear -25pp")&&std::strstr(lines.effects,"I2/3")&&std::strstr(lines.effects,"N1/2"),
          "equipment deltas and stacks/actions are independently readable");
    auto row=snapshot.rows[0];row.baseBp=-10000;row.effectiveBp=-10000;row.locked=true;row.imperilImmune=true;
    Check(S::Format(row,lines)&&std::strstr(lines.title,"-100% > -100%")&&std::strstr(lines.restriction,"LOCK")&&
          std::strstr(lines.restriction,"Imperil immune"),"absorption, affinity lock and status immunity are distinct visible facts");
    row.locked=false;row.imperilImmune=false;row.imperilResistanceBp=5000;
    Check(S::Format(row,lines)&&std::strstr(lines.restriction,"Imperil resist 50%"),"status resistance is not described as elemental resistance");
    for(int value=-10000;value<=25000;value+=2500){row.effectiveBp=value;
        Check(S::Format(row,lines),"all fifteen declared affinity tiers are printable");}
    row.effectiveBp=26000;Check(!S::Format(row,lines),"an invalid resolver value cannot be presented as an approved tier");
    row.effectiveBp=10000;row.imperilResistanceBp=10001;
    Check(!S::Format(row,lines),"invalid status resistance is rejected");
    Check(S::VisibleCount(10,0)==10&&S::VisibleCount(32,0)==10&&S::VisibleCount(32,3)==2&&S::VisibleCount(8,1)==0,
          "ten descriptors fit one page and the engineering maximum is bounded into four pages");
    Check(!S::Capture(31,0,snapshot)&&!S::Capture(18,4,snapshot),"out-of-range actor and page requests never call the provider");
    Check(!S::Unregister(&second)&&S::Unregister(&first),"only the registered provider can retire its view");
    Check(!S::Capture(18,0,snapshot),"retired source restores unavailable presentation immediately");
    std::printf("ELEMENTAL_SCAN_VIEW_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementalScanView.h is missing");return 1;}
#endif
