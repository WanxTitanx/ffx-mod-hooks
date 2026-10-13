#include "../hooks/SpiraNoticeMux.h"
#include <cstdio>
namespace M=FfxHooks::SpiraNoticeMux;namespace A=FfxHooks::Arcana;
namespace FfxHooks::TextLanguage::Native {bool EncodeFieldLiteral(const char*,unsigned char*,std::size_t,std::size_t&) noexcept{return false;}}
static unsigned checks=0,failures=0,acked=2;static bool cascade=true,omega=true;
static void Check(bool v,const char* s){++checks;if(!v){++failures;std::printf("FAIL %s\n",s);}}
static bool CaptureCascade(std::uint16_t,bool,A::Presentation::View& v,std::uint64_t& g) noexcept {g=7;v={};if(cascade){v.kind=A::Presentation::Kind::System;v.card=1;v.revision=10;}return true;}
static bool CaptureOmega(std::uint16_t,bool,A::Presentation::View& v,std::uint64_t& g) noexcept {g=7;v={};if(omega){v.kind=A::Presentation::Kind::System;v.card=0;v.revision=20;}return true;}
static A::Error AckCascade(std::uint64_t g,const A::Presentation::View& v,A::Presentation::Navigation) noexcept {if(g!=7||v.card!=1)return A::Error::Stale;acked=0;cascade=false;return A::Error::None;}
static A::Error AckOmega(std::uint64_t g,const A::Presentation::View& v,A::Presentation::Navigation) noexcept {if(g!=7||v.card!=0)return A::Error::Stale;acked=1;omega=false;return A::Error::None;}
static bool Format(const A::Presentation::View&,char* p,std::size_t n) noexcept {if(n<2)return false;p[0]='x';p[1]=0;return true;}
int main(){const M::Source a{CaptureCascade,AckCascade,Format,nullptr},b{CaptureOmega,AckOmega,Format,nullptr};
    Check(M::Register(0,&a)&&M::Register(1,&b),"both immutable native notice sources register");
    A::Presentation::View v;std::uint64_t g=0;Check(M::Capture(258,true,v,g)&&v.card==1&&g==14,"existing Cascade notice keeps priority and its tagged identity");
    Check(M::Acknowledge(g,v,A::Presentation::Navigation::Next)==A::Error::None&&acked==0,"Cascade acknowledgment is routed only to its source");
    Check(M::Capture(258,true,v,g)&&v.card==10&&g==15,"Omega receives a distinct identity inside the native dialog's bounded card range");
    A::FieldDialog::Controller dialog;Check(dialog.Begin(g,258,v),"Omega source tag is accepted by the actual native dialog controller");
    cascade=true;Check(M::Capture(258,true,v,g)&&v.card==10,"another pending source cannot steal a live Omega dialog");
    Check(M::Acknowledge(g^1,v,A::Presentation::Navigation::Skip)==A::Error::Stale,"cross-source generation aliases are rejected");
    Check(M::Acknowledge(g,v,A::Presentation::Navigation::Skip)==A::Error::None&&acked==1,"Escape routes only the Omega acknowledgment");
    Check(M::Capture(258,true,v,g)&&v.card==1,"next queued source resumes after the prior source finishes");
    M::Unregister(0,&a);M::Unregister(1,&b);Check(!M::Capture(258,true,v,g),"retired source descriptors are inert");
    std::printf("SpiraNoticeMuxRt0: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
