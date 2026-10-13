#include <cstdio>
#if !__has_include("../hooks/ArcanaImageRouting.h")
int main(){std::puts("FAIL: Arcana field/menu artwork routing is not implemented");return 1;}
#else
#include "../hooks/ArcanaImageRouting.h"
using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0,failures=0;
void Check(bool b,const char* label){++checks;if(!b){++failures;std::printf("FAIL: %s\n",label);}}
NativeUi::Images Frame(unsigned id,std::uint64_t generation){NativeUi::Images f;f.generation=generation;f.count=1;f.quads[0]={id,.1f,.1f,.1f,.1f};return f;}
}
int main(){
    using namespace ImageRouting;
    Router router;
    Check(router.Publish(Source::World,Frame(kCardCount,100),1000,false),"world marker frame admitted");
    Check(router.Publish(Source::Message,Frame(36,1),1000,false),"message epoch is independent of marker epoch");
    Check(router.Select(1000,0,true).quads[0].resource==36,"introduction artwork takes priority over world symbols");
    Check(router.Publish(Source::Menu,Frame(7,1),1000,false)&&router.Select(1000,0,true).quads[0].resource==7,"native card menu owns its artwork over a hidden field message");
    router.Publish(Source::Menu,{},1001,true);
    Check(router.Select(1001,0,true).quads[0].resource==36,"closing a menu restores current field artwork");
    Check(!router.Publish(Source::Menu,Frame(7,1),1002,true),"late frame cannot reopen a closed menu epoch");
    Check(router.Select(1002,0,false).count==0,"field imagery is hidden in unrelated native menus or battles");
    Check(router.Publish(Source::Menu,Frame(9,2),1003,true)&&router.Select(1003,0,false).quads[0].resource==9,"native menu remains visible while field imagery is suppressed");
    Check(router.Select(1600,0,true).count==0,"orphaned frames expire without retaining stale map symbols");
    Check(router.Publish(Source::World,Frame(kCardCount,101),1700,false)&&router.Select(1700,1u<<unsigned(Source::World),true).count==0,"asynchronous close mask hides a marker before the producer obtains its mutex");
    auto bad=Frame(kCardCount,102);bad.quads[0].x=-.2f;
    Check(!router.Publish(Source::World,bad,1701,false)&&router.Select(1701,0,true).count==0,"invalid geometry clears only its own channel");
    std::printf("ArcanaImageRoutingRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#endif
