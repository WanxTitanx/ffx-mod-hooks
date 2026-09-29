// Jarvis-HOOK: exact read-buffer identity and confirmed native load; no live game.
#include <cstdio>
#if !__has_include("../hooks/SphereGridProgress8LoadCore.h")
int main(){std::puts("FAIL: Grid8 confirmed-load binding is missing");return 1;}
#else
#include "../hooks/SphereGridProgress8LoadCore.h"
namespace L=FfxHooks::SphereGridProgress8Load;
namespace R=FfxHooks::RonsoPool;
using Token=FfxHooks::SeymourSession::Token;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL: %s\n",text);}}
static L::Identity Identity(unsigned n){L::Identity id;id.path.fill(static_cast<unsigned char>(n));id.image.fill(9);return id;}
static R::SaveImage Image(unsigned n){R::SaveImage image{};image[77]=static_cast<unsigned char>(n);R::SealSave(image);return image;}
static constexpr std::uintptr_t Dest=0x112ca90,A=0x1000,B=0x2000;
static bool End(L::Tracker& t,std::uint64_t cookie,const R::SaveImage& image,Token session={11,7}){
    return t.End(cookie,true,7,session,image.data()+64,image.size()-64);
}
int main(){
    const auto image=Image(1);const auto id=Identity(1);
    L::Tracker t(Dest);
    Check(t.ReadCompleted(A,id,image),"completed file read is observed");
    Check(!t.Capture({11,7},3).Valid(),"preview does not activate a save");
    t.Begin(1,Dest,A,7,3,&image);Check(End(t,1,image),"confirmed exact copy binds the save");
    const auto first=t.Capture({11,7},3);
    Check(first.Valid()&&first.save==id&&t.Current(first,{11,7},3),"path/image match the exact read buffer");
    Check(!End(t,1,image)&&t.Current(first,{11,7},3),"replayed completion cannot revoke the accepted save");
    t.ReadStarting(A);Check(t.Current(first,{11,7},3),"later preview cannot revoke an accepted load");
    t.ReadCompleted(B,Identity(2),image);t.Begin(2,0x1234,B,7,3,&image);
    Check(!End(t,2,image)&&t.Current(first,{11,7},3),"preview destination never switches the active identity");
    Check(!t.Current(first,{12,7},3)&&!t.Current(first,{11,8},3)&&!t.Current(first,{11,7},4),"session/thread/request mismatch closes restoration");
    for(unsigned mode=0;mode<15;++mode){
        L::Tracker state(Dest);auto observed=image,readback=image;
        Check(state.ReadCompleted(A,id,image),"negative fixture has actual completed provenance");
        if(mode==0)state.ReadStarting(A);
        if(mode==1){observed[80]^=1;R::SealSave(observed);}
        if(mode==2)state.ReadCompleted(A,Identity(2),Image(2));
        state.Begin(1,Dest,mode==3?B:A,7,3,mode==4?nullptr:&observed);
        if(mode==5)state.ReadStarting(A);
        if(mode==6)state.ReadCompleted(A,Identity(2),image);
        if(mode==7)state.Invalidate();
        if(mode==8)state.RequestStop();
        if(mode==9)readback[100]^=1;
        Token session{11,7};if(mode==11)session={};if(mode==12)session.thread=9;
        const bool accepted=state.End(1,mode!=10,mode==13?9:7,session,readback.data()+64,readback.size()-(mode==14?65:64));
        Check(!accepted&&!state.Capture({11,7},3).Valid(),"failed or mismatched native load is rejected");
        Check(!End(state,1,observed),"rejected provenance cannot be revived by retry");
    }
    auto second=Image(2);t.ReadCompleted(B,Identity(2),second);
    t.Begin(3,Dest,B,7,3,nullptr);Check(!t.Current(first,{11,7},3)&&!End(t,3,second),"null-source load retires the old save before copying");
    for(unsigned i=0;i<4;++i)second[25844+i]=0;
    t.Begin(4,Dest,B,7,3,&second);Check(End(t,4,second,{14,7}),"native four-byte CRC clear preserves exact observed identity");
    Check(t.Capture({14,7},3).save==Identity(2),"CRC normalization never selects another path");
    t.Begin(5,Dest,B,7,3,&second);t.Begin(6,Dest,B,7,3,&second);
    Check(!End(t,6,second)&&!End(t,5,second),"nested loads cannot admit ambiguous inner/outer progress");
    for(unsigned n=0;n<256;++n){
        const auto cookie=std::uint64_t(n)+7;const auto bytes=Image(n%250);const auto key=Identity(n%250+1);const Token session{cookie+11,7};
        Check(t.ReadCompleted(A,key,bytes),"reused buffer receives new read serial");
        t.Begin(cookie,Dest,A,7,3,&bytes);Check(End(t,cookie,bytes,session),"new exact copy owns the current session");
        const auto claim=t.Capture(session,3);Check(claim.Valid()&&claim.save==key,"new session keeps its own companion key");
    }
    t.Invalidate();Check(!t.Capture({273,7},3).Valid(),"reset retires restoration provenance");
    t.RequestStop();Check(!t.ReadCompleted(A,id,image),"stop prevents new source registration");
    L::Tracker exhausted(Dest);exhausted.ReadCompleted(A,id,image);
    for(unsigned n=1;n<=L::Tracker::PendingCapacity+1;++n)exhausted.Begin(n,Dest,A,7,3,&image);
    for(unsigned n=1;n<=L::Tracker::PendingCapacity+1;++n)Check(!End(exhausted,n,image),"exhaustion fails closed without a partial admission");
    std::printf("SphereGridProgress8LoadRt0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#endif
