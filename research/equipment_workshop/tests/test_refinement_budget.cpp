#include "workshop.h"
#include "customize_recipes.h"
#include <cstdio>
using namespace workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
int main(){
    Policy policy{};unsigned item=0,quantity=0,total=0;
    const unsigned curtains[]={2,3,4,5,6,7,9,10,11,12};
    for(unsigned rank=1;rank<=10;++rank){
        Check(RefinementCost(0x8055,rank,policy,item,quantity)&&item==57&&quantity==curtains[rank-1],
              "Auto-Protect has the reduced reviewed material price for every rank");total+=quantity;
    }
    Check(total==69&&total*5==345,"five Auto-Protect-sized recipes reach +50 with 345 specific materials instead of 1925");
    Check(RefinementCost(0x8001,10,policy,item,quantity)&&quantity==1,"rare one-item recipes do not grow into ten-item late fees");
    for(unsigned id=0;id<131;++id){unsigned last=0;
        for(unsigned rank=1;rank<=10;++rank){
            Check(RefinementCost(static_cast<std::uint16_t>(0x8000+id),rank,policy,item,quantity)&&quantity>=1&&quantity>=last&&quantity<=17,
                  "all supported native and fallback recipes remain affordable, positive and nondecreasing");last=quantity;
        }
        Check(last*5+policy.baseAmount<=99,"even five simultaneous maximum-rank costs fit one native stack plus the base sphere");
    }
    Check(RefinementGil(10)==10000&&RefinementGil(50)==100000,"the material rebalance preserves progressive Gil fees");
    Check(!RefinementCost(0x8055,0,policy,item,quantity)&&!RefinementCost(0x8055,11,policy,item,quantity),"ability rank caps remain unchanged");
    std::printf("WORKSHOP_REFINEMENT_BUDGET_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
