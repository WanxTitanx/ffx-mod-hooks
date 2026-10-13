#include "OmegaT7Notice.h"
#include "OmegaT7Session.h"
#include "OmegaT7Runtime.h"
#include "OmegaT7Text.h"
#include "SpiraNoticeMux.h"
#include "UiLanguageSettings.h"
#include "TextLanguageHook.h"
#include <atomic>
namespace FfxHooks::OmegaT7::Notice {
namespace {
using View=Arcana::Presentation::View;using Navigation=Arcana::Presentation::Navigation;
std::atomic<bool> enabled{false},review{false};
std::uint64_t boundSession=0,serial=0,shownEpoch=0;unsigned kind=3,page=0;bool inOmega=false,entry=false,manual=false;
bool Capture(std::uint16_t room,bool stable,View& out,std::uint64_t& session) noexcept {
    if(!enabled.load()||!Runtime::Active())return false;
    Session::Field(stable);Campaign state;if(!Session::Snapshot(state,session))return false;
    if(session!=boundSession){boundSession=session;kind=3;page=0;inOmega=false;entry=false;manual=false;}
    const bool omega=room==258||room==271;
    if(stable){if(omega&&!inOmega)entry=true;inOmega=omega;}
    if(stable&&!omega&&!manual){kind=3;page=0;entry=false;}
    if(review.exchange(false)){kind=0;page=0;manual=true;if(serial!=UINT64_MAX)++serial;}
    if(kind==3&&stable&&omega){
        if(!state.seed.loreSeen){kind=0;page=0;}
        else if(state.seed.renewalPending){kind=1;shownEpoch=state.seed.epoch;}
        else if(entry)kind=2;
        if(kind!=3&&serial!=UINT64_MAX)++serial;
    }
    out={};if(kind<3&&serial){out.kind=Arcana::Presentation::Kind::System;out.card=static_cast<std::uint8_t>(kind);out.page=static_cast<std::uint8_t>(page);out.revision=serial;}
    return true;
}
Arcana::Error Ack(std::uint64_t session,const View& view,Navigation action) noexcept {
    if(!enabled.load()||session!=boundSession||view.kind!=Arcana::Presentation::Kind::System||view.card!=kind||view.page!=page||view.revision!=serial||kind>2||action==Navigation::None)return Arcana::Error::Stale;
    if(kind==0&&action==Navigation::Previous){if(page)--page;return Arcana::Error::None;}
    if(kind==0&&action==Navigation::Next&&page+1<Text::Pages){++page;return Arcana::Error::None;}
    if(kind==0&&!Session::SetLoreSeen(session))return Arcana::Error::InvalidState;
    if(kind==1&&!Session::ClearRenewal(session,shownEpoch)){kind=3;page=0;return Arcana::Error::Stale;}
    kind=3;page=0;entry=false;manual=false;return Arcana::Error::None;
}
bool Format(const View& view,char* out,std::size_t capacity) noexcept {
    const bool pt=UiLanguage::Settings::Current()==UiLanguage::Locale::Portuguese&&TextLanguage::Native::Inspect().fontReady;
    if(!Text::Format(view.card,view.page,pt,out,capacity))return false;
    if(pt){std::array<unsigned char,2048> bytes{};std::size_t size=0;if(!TextLanguage::Native::EncodeFieldLiteral(out,bytes.data(),bytes.size(),size))return Text::Format(view.card,view.page,false,out,capacity);}
    return true;
}
const Arcana::FieldPresentation::SupplementalSource source{Capture,Ack,Format,SpiraNoticeMux::Encode};
}
void Start() noexcept {enabled=true;(void)SpiraNoticeMux::Register(1,&source);}
void Stop() noexcept {enabled=false;review=false;SpiraNoticeMux::Unregister(1,&source);}
void RequestReview() noexcept {if(enabled.load())review=true;}
const Arcana::FieldPresentation::SupplementalSource& Source() noexcept {return source;}
}
