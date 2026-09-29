// Jarvis-HOOK: a Grid8 companion must never overwrite changed native-seven progress.
#include <cstdio>
#if !__has_include("../hooks/SphereGridProgress8RestoreCore.h")
int main(){std::puts("FAIL: native-seven restoration preservation is missing");return 1;}
#else
#include "../hooks/SphereGridProgress8RestoreCore.h"
namespace G=FfxHooks::SphereGridProgress8;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL: %s\n",why);}}
int main(){
    G::Snapshot native;native.nodes.resize(1003,{1,1});native.links.resize(1024,1);native.cursors.fill(860);native.cursors[7]=0;
    auto saved=native;saved.cursors[7]=1002;
    for(auto& node:saved.nodes)node.mask|=0x80;
    for(auto& link:saved.links)link|=0x80;
    Check(G::CanRestoreEighth(native,saved),"eighth mask/cursor restoration preserves every native-seven field");
    Check(G::CanRestoreEighth(saved,native),"clearing eighth progress still preserves the native seven");
    for(unsigned mode=0;mode<8;++mode){
        auto changed=native;
        if(mode==0)changed.nodes[0].mask^=1;
        if(mode==1)changed.nodes.back().mask^=0x40;
        if(mode==2)changed.nodes.back().content=2;
        if(mode==3)changed.links[0]^=1;
        if(mode==4)changed.links.back()^=0x40;
        if(mode==5)changed.cursors[6]=861;
        if(mode==6)changed.tilt=1;
        if(mode==7)changed.zoom=1;
        Check(!G::CanRestoreEighth(changed,saved),"a changed native field is not overwritten by the companion");
    }
    for(unsigned bit=0;bit<7;++bit)for(unsigned mask=0;mask<128;++mask){
        auto changed=native,desired=native;
        changed.nodes[500].mask=static_cast<unsigned char>(mask);
        desired.nodes[500].mask=static_cast<unsigned char>(mask|0x80);
        Check(G::CanRestoreEighth(changed,desired),"every native-seven mask accepts the independent eighth bit");
        desired.nodes[500].mask^=static_cast<unsigned char>(1u<<bit);
        Check(!G::CanRestoreEighth(changed,desired),"every changed native-seven bit is rejected");
    }
    auto invalid=saved;invalid.cursors[7]=5000;
    Check(!G::CanRestoreEighth(native,invalid),"out-of-range eighth cursor is rejected");
    invalid=saved;invalid.nodes.pop_back();
    Check(!G::CanRestoreEighth(native,invalid),"different logical array length is not a Grid8-only restoration");
    std::printf("SphereGridProgress8RestoreCoreRt0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#endif
