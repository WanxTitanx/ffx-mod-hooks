#include "../hooks/FmvSpeedCore.h"
#include <cstdio>
#include <initializer_list>
namespace F=FfxHooks::FmvSpeed;
static int checks,failures;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
struct Fake {unsigned calls=0,remaining=0,rate=1,restores=0,stopAfter=99;bool audio=true;};
static bool Next(void* raw){auto& f=*static_cast<Fake*>(raw);++f.calls;if(!f.remaining)return false;--f.remaining;return true;}
static bool Rate(void* raw,unsigned n){auto& f=*static_cast<Fake*>(raw);if(!f.audio)return false;f.rate=n;return true;}
static void Restore(void* raw){auto& f=*static_cast<Fake*>(raw);f.rate=1;++f.restores;}
static bool Admitted(void* raw){auto& f=*static_cast<Fake*>(raw);return f.calls<f.stopAfter;}
static F::Io Io(Fake& f){return {&f,Next,Rate,Restore,Admitted};}
int main(){
    for(unsigned wanted:{1u,2u,4u,8u})for(unsigned available=0;available<40;++available){
        const unsigned n=F::Budget(wanted,available,true);
        Check(n<=wanted&&(n==1||n==2||n==4||n==8),"frame bursts are capped at a supported requested factor");
        Check(n==1||n<=available,"known decoder starvation never requests extra frames");
    }
    Fake normal{0,20};auto result=F::Fetch(Io(normal),8,20,false);
    Check(result.frame&&normal.calls==1&&normal.rate==1,"non-movie or disabled paths preserve one native fetch");
    Fake fast{0,20};result=F::Fetch(Io(fast),8,20,true);
    Check(result.frame&&result.produced==8&&fast.calls==8&&fast.rate==8,"admitted movie advances frame and audio together");
    Fake bounded{0,3};result=F::Fetch(Io(bounded),8,3,true);
    Check(result.produced==2&&bounded.rate==2,"limited decoded queue uses a sustainable lower factor");
    Fake unsupported{0,20};unsupported.audio=false;result=F::Fetch(Io(unsupported),8,20,true);
    Check(unsupported.calls==1&&unsupported.rate==1,"audio-rate failure cannot accelerate only the picture");
    Fake ending{0,3};result=F::Fetch(Io(ending),8,8,true);
    Check(result.frame&&result.produced==3&&ending.calls==4&&ending.rate==3,"final partial burst preserves the last valid frame");
    Fake ended{};result=F::Fetch(Io(ended),4,4,true);
    Check(!result.frame&&ended.calls==1&&ended.rate==1,"native EOF remains false and restores the audio rate");
    Fake focus{0,20};focus.stopAfter=1;result=F::Fetch(Io(focus),8,8,true);
    Check(focus.calls==1&&focus.rate==1,"focus/ownership loss stops the burst and restores audio");
    Check(F::Budget(8,99999,true)==1&&F::Budget(3,8,true)==1,"corrupt queues and invalid factors stay native");
    std::printf("FmvSpeedRt0: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
