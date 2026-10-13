#include "../hooks/ArcanaVictory.h"
#include "../hooks/ArcanaPresentation.h"
#include <cstdio>
namespace A=FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
int main(){A::VictoryReceipt omega{9,4,1077,10,100,1,true,true};
    Check(A::VictoryCard(omega)==78,"real story Omega identifies the paired Weapon reward");
    for(unsigned monster:{99u,910u,911u,912u,913u,914u}){auto r=omega;r.monster=monster;Check(A::VictoryCard(r)==A::kCardCount,"Ultima, farm clone and guards cannot grant Weapon cards");}
    for(unsigned mode=0;mode<6;++mode){auto r=omega;switch(mode){case 0:r.session=0;break;case 1:r.battle=0;break;case 2:r.original=false;break;case 3:r.victory=false;break;case 4:r.field=1076;break;case 5:r.formation=0;break;}
        Check(A::VictoryCard(r)==A::kCardCount,"unbound, debug, defeat, escape or wrong encounter cannot grant Weapons");}
    for(unsigned monster=334;monster<=343;++monster){auto r=omega;r.monster=monster;r.stage=monster==336?2:monster==340?5:1;
        Check(A::VictoryCard(r)==80+monster-334,"each original Dark Aeon maps only its own appended card");
        r.original=false;Check(A::VictoryCard(r)==A::kCardCount,"Arena/debug versions are not challenge victories");
        r.original=true;r.victory=false;Check(A::VictoryCard(r)==A::kCardCount,"a Dark actor death without a real victory grants nothing");}
    for(unsigned stage=0;stage<2;++stage){auto r=omega;r.monster=336;r.stage=stage;Check(A::VictoryCard(r)==A::kCardCount,"Ixion requires its second-stage completion");}
    for(unsigned stage=0;stage<5;++stage){auto r=omega;r.monster=340;r.stage=stage;Check(A::VictoryCard(r)==A::kCardCount,"Yojimbo requires its fifth-duel completion");}
    A::State s;A::Presentation::State notices;const auto before=s.acquired;
    Check(A::AwardOmegaWeapons(s,s.revision)==A::Error::None&&A::Presentation::ObserveAwards(notices,before,s.acquired),"paired victory queues both native award notices");
    Check(s.acquired[78]&&s.acquired[79]&&notices.count==2,"both Weapon rewards are one collection transition");
    auto previous=s.acquired;const auto count=notices.count;
    Check(A::AwardOmegaWeapons(s,s.revision)==A::Error::None&&A::Presentation::ObserveAwards(notices,previous,s.acquired)&&notices.count==count,"duplicate victory does not duplicate notices");
    std::printf("ArcanaVictoryRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
