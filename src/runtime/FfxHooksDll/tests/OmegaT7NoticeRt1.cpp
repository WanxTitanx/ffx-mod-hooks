#include "../hooks/OmegaT7Notice.h"
#include "../hooks/OmegaT7Session.h"
#include "../hooks/OmegaT7Text.h"
#include "../hooks/SpiraNoticeMux.h"
#include <cstdio>
#include <cstring>
namespace O=FfxHooks::OmegaT7;namespace A=FfxHooks::Arcana;
static O::Campaign campaign;static std::uint64_t sessionId=1;static bool active=true,ready=true,persist=true;
static const char* locale="en";static unsigned checks=0,failures=0,fieldTicks=0;
namespace FfxHooks::Config {const char* GetString(const char*,const char*){return locale;}}
namespace FfxHooks::TextLanguage::Native {Snapshot Inspect() noexcept{return {};}
bool EncodeFieldLiteral(const char*,unsigned char*,std::size_t,std::size_t&) noexcept{return false;}}
namespace FfxHooks::OmegaT7::Runtime {bool Active() noexcept{return active;}}
namespace FfxHooks::OmegaT7::Session {
void Field(bool) noexcept{++fieldTicks;}
bool Snapshot(Campaign& out,std::uint64_t& session) noexcept{out=campaign;session=sessionId;return ready;}
bool SetLoreSeen(std::uint64_t session) noexcept{if(!persist||session!=sessionId)return false;campaign.seed.loreSeen=true;return true;}
bool ClearRenewal(std::uint64_t session,std::uint64_t epoch) noexcept{if(!persist||session!=sessionId||epoch!=campaign.seed.epoch)return false;campaign.seed.renewalPending=false;return true;}
}
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    campaign.seed=O::Seed::Adopt(7);const auto& source=O::Notice::Source();A::Presentation::View v;std::uint64_t session=0;
    Check(!source.capture(258,true,v,session),"default OFF produces no notice");O::Notice::Start();
    Check(source.capture(258,false,v,session)&&v.kind==A::Presentation::Kind::None,"unstable/battle entry cannot start lore");
    Check(source.capture(258,true,v,session)&&v.kind==A::Presentation::Kind::System&&v.card==0&&v.page==0,"first stable Omega entry starts the compact lore");
    char text[2048]{};Check(source.format(v,text,sizeof(text))&&std::strstr(text,"Seventh Seal"),"lore formats the native field message");
    locale="pt-BR";Check(source.format(v,text,sizeof(text))&&std::strstr(text,"Seventh Seal"),"missing paired Portuguese font safely uses English");locale="en";
    const auto first=v;Check(source.acknowledge(session+1,v,A::Presentation::Navigation::Skip)==A::Error::Stale&&!campaign.seed.loreSeen,"foreign session cannot dismiss or persist lore");
    for(unsigned page=0;page<O::Text::Pages;++page){Check(v.page==page,"each tutorial page is presented in order");
        Check(source.acknowledge(session,v,A::Presentation::Navigation::Next)==A::Error::None,"native next navigation accepted");source.capture(258,true,v,session);}
    Check(campaign.seed.loreSeen&&v.kind==A::Presentation::Kind::None,"completed lore persists and does not immediately repeat");
    Check(source.acknowledge(session,first,A::Presentation::Navigation::Next)==A::Error::Stale,"old dialog receipt cannot advance a later view");
    source.capture(10,true,v,session);source.capture(271,true,v,session);Check(v.card==2&&v.kind==A::Presentation::Kind::System,"later entry shows only the short reminder");
    Check(source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::None,"Escape dismisses a reentry notice");
    campaign.seed.renewalPending=true;++campaign.seed.epoch;source.capture(258,true,v,session);Check(v.card==1,"renewed seed takes priority over a reminder");
    ++campaign.seed.epoch;Check(source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::Stale&&campaign.seed.renewalPending,"old renewal notice cannot acknowledge a newer epoch");
    source.capture(258,true,v,session);Check(v.card==1&&source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::None&&!campaign.seed.renewalPending,"current epoch is acknowledged once");
    source.capture(10,true,v,session);O::Notice::RequestReview();source.capture(10,true,v,session);Check(v.card==0&&v.page==0&&v.kind==A::Presentation::Kind::System,"F7 review works outside Omega after returning to the field");
    Check(source.acknowledge(session,v,A::Presentation::Navigation::Next)==A::Error::None,"review advances");source.capture(10,true,v,session);
    Check(v.page==1&&source.acknowledge(session,v,A::Presentation::Navigation::Previous)==A::Error::None,"review supports the previous page");source.capture(10,true,v,session);
    Check(v.page==0&&source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::None,"Escape skips all remaining review pages");
    ++sessionId;campaign.seed.loreSeen=false;source.capture(258,true,v,session);persist=false;
    Check(source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::InvalidState&&!campaign.seed.loreSeen,"failed persistence never claims the tutorial was consumed");persist=true;
    Check(source.acknowledge(session,v,A::Presentation::Navigation::Skip)==A::Error::None&&campaign.seed.loreSeen,"retry can persist the same valid receipt");
    const auto before=fieldTicks;active=false;Check(!source.capture(258,true,v,session)&&fieldTicks==before,"inactive combat runtime does not write or show notices");active=true;
    ready=false;Check(!source.capture(258,true,v,session),"unbound campaign remains unavailable");ready=true;O::Notice::Stop();
    Check(!source.capture(258,true,v,session),"stopped source is inert");
    std::printf("OmegaT7NoticeRt1: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
