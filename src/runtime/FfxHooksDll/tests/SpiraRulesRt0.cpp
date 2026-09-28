// Jarvis-HOOK: defined effects; native callers prove eligibility separately.
#include <cstdio>
#include <cstdint>
#if __has_include("../hooks/SpiraRules.h")
#include "../hooks/SpiraRules.h"
namespace S=FfxHooks::SpiraAbilities;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    for(unsigned mask=0;mask<4;++mask){
        const bool source=(mask&1)!=0,target=(mask&2)!=0;
        Check(S::BargainDamage(1000,true,source,target)==(mask==3?2250:mask?1500:1000),"incoming and outgoing factors compose once");
        Check(S::BargainDamage(-1000,true,source,target)==-1000,"absorption keeps its native amount");
        Check(S::BargainDamage(1000,false,source,target)==1000,"MP CTB and nonnumeric outcomes remain unchanged");
        Check(S::BargainDamage(0,true,source,target)==0,"zero stays zero");
    }
    Check(S::BargainDamage(INT32_MAX,true,true,true)==INT32_MAX,"wide damage arithmetic saturates");
    Check(S::BargainDamage(3,true,true,false)==4&&S::BargainDamage(3,true,true,true)==6,"each multiplier retains its integer boundary");
    for(unsigned owner=0;owner<31;++owner){
        Check(S::HpCeiling(99999,owner,true,false)==(owner>=8&&owner<18?999999:99999),"paid ceiling is Aeon-only");
        Check(S::HpCeiling(99999,owner,false,true)==(owner==2?999999:99999),"Warden is Auron-only");
        Check(S::HpCeiling(9999,owner,false,true)==9999,"Warden needs effective BHP");
        Check(S::MpCeiling(999,owner,true)==(owner>=8&&owner<18?9999:999),"paid MP replaces BMP for eligible Aeons");
        Check(S::HpCeiling(99999,owner,false,false)==99999,"ordinary BHP is unchanged");
    }
    Check(S::HpCeiling(12345,8,true,false)==12345&&S::MpCeiling(1234,8,true)==1234,"unknown native ceilings are preserved");
    Check(S::ManaSpring(0,999)==5&&S::ManaSpring(998,999)==999&&S::ManaSpring(999,999)==999,"fixed MP gain respects the maximum");
    Check(S::ManaSpring(-1,999)==-1&&S::ManaSpring(1000,999)==1000&&S::ManaSpring(5,0)==5,"invalid or excessive current MP is preserved");
    Check(S::ManaSpring(INT32_MAX-2,INT32_MAX)==INT32_MAX,"MP addition cannot overflow");
    for(unsigned prior=1;prior<=3;++prior){
        Check(S::DropMaximum(prior,true,false)==(prior>2?prior:2),"Double uses party maximum");
        Check(S::DropMaximum(prior,true,true)==3,"Triple wins without x6");
    }
    std::printf("SPIRA_RULES_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production SpiraRules.h is missing");return 1;}
#endif
