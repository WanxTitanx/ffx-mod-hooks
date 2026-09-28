// Jarvis-HOOK: specification vectors for the single positive-HP clamp policy.
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>

#if __has_include("../hooks/BattleDamagePolicy.h")
#include "../hooks/BattleDamagePolicy.h"
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
}
int main(){
    namespace D=FfxHooks::BattleDamage;
    struct Selector {unsigned flags;bool equipment;std::int32_t expected;};
    const Selector selectors[]={{0,false,9999},{0,true,99999},{0x40,false,9999},
        {0x40,true,9999},{0x80,false,99999},{0x80,true,99999},
        {0xC0,false,99999},{0xC0,true,99999},{0x3F,false,9999},{0x3F,true,99999}};
    for(const auto& row:selectors)
        Check(D::NativeUpper(row.flags,row.equipment)==row.expected,
              "command force overrides suppression; suppression overrides equipment BDL");
    D::Policy p{};
    for(const auto cap:{9999,99999}){
        p.nativeUpper=cap;
        for(const auto damage:{-200000,-1,0,1,450000,1200000})
            Check(D::ClampUpper(damage,p).damage==(damage>cap?cap:damage),
                  "OFF reproduces the native upper clamp without changing its separate lower floor");
    }
    p={};p.nativeUpper=99999;p.magicEnabled=true;p.offensiveSpell=true;
    Check(D::ClampUpper(450000,p).damage==450000,"eligible magic retains its precap damage");
    Check(D::ClampUpper(1200000,p).damage==999999,"eligible magic has a finite six-digit ceiling");
    Check(D::ClampUpper(-200000,p).damage==-200000,"the upper policy does not expand or reapply the negative floor");
    p.nativeUpper=9999;
    Check(D::ClampUpper(450000,p).damage==9999,"magic without effective BDL remains at 9999");
    p.nativeUpper=99999;p.offensiveSpell=false;
    Check(D::ClampUpper(450000,p).damage==99999,"physical attacks do not inherit magic BDL");
    p.offensiveSpell=true;
    for(const auto component:{D::Component::Mp,D::Component::Ctb,D::Component::Other}){
        p.component=component;
        Check(D::ClampUpper(450000,p).damage==99999,"MP CTB and unknown passes preserve their native cap");
    }
    p.component=D::Component::Hp;p.magicEnabled=false;p.aeonBreakAuthorized=true;
    Check(D::ClampUpper(450000,p).damage==450000,"authorized Aeon damage has an independent finite policy");
    p.nativeUpper=9999;
    Check(D::ClampUpper(450000,p).damage==9999,"Aeon cap respects the native BDL suppression decision");
    p.nativeUpper=99999;p.magicEnabled=true;
    const auto combined=D::ClampUpper(1200000,p);
    Check(combined.damage==999999&&combined.upper==999999,"composed finite policies select a maximum, never add ceilings");
    p={};p.nativeUpper=99999;p.legacyNovaBypass=true;
    Check(D::ClampUpper(1200000,p).damage==1200000,"standalone legacy Nova retains its explicitly selected behavior");
    p.magicEnabled=true;p.offensiveSpell=true;
    Check(D::ClampUpper(1200000,p).damage==999999,"an applicable finite policy wins over Nova bypass");
    p.nativeUpper=9999;
    Check(D::ClampUpper(1200000,p).damage==9999,"Nova cannot bypass missing or suppressed BDL on a finite-policy spell");
    p={};p.nativeUpper=99999;p.nonlethal=true;p.targetCurrentHp=5000;
    Check(D::ClampUpper(500000,p).damage==4999,"nonlethal is applied after every amplifier and cap request");
    p.legacyNovaBypass=true;p.magicEnabled=true;p.offensiveSpell=true;
    Check(D::ClampUpper(500000,p).damage==4999,"finite magic and Nova cannot override nonlethal Gravity");
    for(const auto hp:{0,1}){p.targetCurrentHp=hp;
        Check(D::ClampUpper(500000,p).damage==0,"zero and one HP never underflow the nonlethal bound");}
    p.targetCurrentHp=5000;
    Check(D::ClampUpper(-500,p).damage==-500,"nonlethal does not invert or clamp absorption");
    p.nativeUpper=-1;
    Check(!D::ClampUpper(100,p).valid&&D::ClampUpper(100,p).damage==100,
          "an invalid native cap is rejected without inventing a replacement");
    D::GravityInput g{};g.currentHp=40000;g.maximumHp=160000;
    Check(D::Gravity(g).mode==D::GravityMode::Native,"Gravity is independent and OFF by default");
    g.enabled=true;
    Check(D::Gravity(g).mode==D::GravityMode::Native,"unlisted targets retain their native formula");
    g.profileMatched=true;
    Check(D::Gravity(g).mode==D::GravityMode::ReplaceBase&&D::Gravity(g).base==10000,
          "a listed boss uses floor(maximum HP / 16), not current HP / 8");
    g.maximumHp=160015;
    Check(D::Gravity(g).base==10000,"Gravity rounds its maximum-HP base down");
    g.nativeImmune=true;
    Check(D::Gravity(g).mode==D::GravityMode::PreserveImmunity,"native Gravity immunity is preserved by default");
    g.overrideNativeImmunity=true;
    Check(D::Gravity(g).mode==D::GravityMode::ReplaceBase,"only an explicit profile can override native immunity");
    g.maximumHp=-1;
    Check(D::Gravity(g).mode==D::GravityMode::Invalid,"invalid HP data cannot be admitted as a boss formula");
    Check(D::FromNativePass(3)==D::Component::Hp&&D::FromNativePass(2)==D::Component::Mp&&
          D::FromNativePass(1)==D::Component::Ctb&&D::FromNativePass(0)==D::Component::Other,
          "native three-pass writeback order is HP then MP then CTB");
    std::printf("BATTLE_DAMAGE_POLICY_RT0 %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
#else
int main(){std::puts("FAIL production BattleDamagePolicy.h is missing");return 1;}
#endif
