#include "../hooks/SpiraItemMpRegen.h"
#include <cstdio>
#include <climits>
using namespace FfxHooks::SpiraAbilities::ItemMpRegen;
namespace {unsigned checks=0,failures=0;void Check(bool b,const char* why){++checks;if(!b){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}}
int main(){
    unsigned char row[96]{};row[0x5A]=4;
    int mask=5;
    Check(!Convert(false,0x2042,row,96,1,1,mask)&&mask==5,"OFF preserves native Twin Stars and other buffs");
    Check(Convert(true,0x2042,row,96,1,1,mask)&&mask==1,"Twin Stars replaces only newly granted MP Cost 0");
    mask=4;Check(Convert(true,0x2045,row,96,2,0,mask)&&mask==0,"Three Stars is converted independently for each native target");
    mask=4;Check(Convert(true,0x2045,row,96,2,4,mask)&&mask==4,"an existing non-item Spellspring is preserved");
    mask=4;Check(!Convert(true,0x3042,row,96,1,0,mask)&&mask==4,"spells and Mix commands keep their native effect");
    Check(!Convert(true,0x2042,row,92,1,0,mask)&&!Convert(true,0x2042,row,96,18,0,mask),"wrong record extent and monster targets are rejected");
    mask=-1;Check(!Convert(true,0x2042,row,96,1,0,mask),"native refresh does not grant regeneration");
    State state;Check(state.Turn(1,100,1,1,500,100,999)==100,"no item use means no regeneration");
    Check(state.Grant(1,100,1)&&state.Turn(1,100,1,2,500,100,999)==109,"one percent of maximum MP is granted on the recipient's turn");
    Check(state.Turn(1,100,1,2,500,109,999)==109,"duplicate native edge cannot grant twice");
    Check(state.Grant(1,100,1)&&state.Turn(1,100,1,3,500,109,999)==118,"reusing the item refreshes without stacking percentages");
    Check(state.Turn(1,100,1,4,500,1490,1500)==1500,"regeneration saturates at the current MP maximum");
    Check(state.Turn(1,100,1,5,500,0,75)==1,"small MP pools receive the minimum representable one MP");
    Check(state.Turn(1,100,1,6,0,20,999)==20,"dead target does not regenerate");
    Check(state.Turn(1,100,1,7,500,20,999)==20,"KO retires the item effect before revival");
    state.Grant(1,100,1);Check(state.Turn(1,200,1,8,500,100,999)==100,"reused actor slot cannot inherit the previous recipient's buff");
    Check(state.Turn(1,100,2,9,500,100,999)==100,"another battle generation does not inherit the buff");
    state.Grant(1,100,2);state.Forget(1);Check(state.Turn(1,100,2,10,500,100,999)==100,"explicit retirement clears the target");
    state.Grant(1,100,2);state.Reset();Check(state.Turn(1,100,2,11,500,100,999)==100,"battle exit, load and stop reset every recipient");
    Check(!state.Grant(18,100,2)&&!state.Grant(1,0,2)&&!state.Grant(1,100,0),"unbound identity is never granted an effect");
    state.Grant(1,100,2);Check(state.Turn(1,100,2,12,500,INT_MAX-1,INT_MAX)==INT_MAX,"regeneration cannot overflow signed MP");
    std::printf("Spira item MP regeneration RT0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
