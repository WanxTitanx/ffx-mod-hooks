#include <cstdio>
#include <string>
#if !__has_include("../hooks/ArcanaFieldDialogCore.h")
int main(){std::puts("FAIL: Arcana native field dialog ownership is not implemented");return 1;}
#else
#include "../hooks/ArcanaFieldDialogCore.h"
using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0,failures=0;
void Check(bool b,const char* label){++checks;if(!b){++failures;std::printf("FAIL: %s\n",label);}}
FieldDialog::Sample Ready(){FieldDialog::Sample s;s.generation=2;s.scene=42;s.owned=true;s.available=true;s.focused=true;s.window=2;s.text=3;return s;}
}
int main(){
    using namespace FieldDialog;
    for(const auto kind:{Presentation::Kind::Notice,Presentation::Kind::Introduction,Presentation::Kind::System}){
        Controller native;native.Begin(2,42,{kind,0,0,false,1});auto sample=Ready();sample.text=1;
        native.Poll(sample);sample.back=true;const auto decision=native.Poll(sample);
        Check(decision.action==Action::Close&&decision.navigation==(kind==Presentation::Kind::Notice?Presentation::Navigation::Next:Presentation::Navigation::Skip),"native Cancel dismisses each notice while text is revealing");
    }
    const Presentation::View notice{Presentation::Kind::Notice,36,0,false};
    Controller c;Check(!c.Begin(0,42,notice)&&!c.Begin(2,42,{}),"invalid epoch and absent content never acquire a dialog");
    Check(c.Begin(2,42,notice)&&!c.Begin(2,42,notice),"one controller owns at most one native window");
    auto s=Ready();s.confirm=true;
    Check(c.Poll(s).action==Action::None,"held opening input cannot close a receipt");
    s.confirm=false;c.Poll(s);s.confirm=true;
    auto d=c.Poll(s);Check(d.action==Action::Close&&d.navigation==Presentation::Navigation::Next,"fresh confirmation requests native close before acknowledgement");
    Check(c.Poll(s).action==Action::None,"closing is requested only once");
    s.window=3;Check(c.Poll(s).action==Action::None,"closing animation does not count as completed");
    s.window=0;d=c.Poll(s);
    Check(d.action==Action::Release&&d.navigation==Presentation::Navigation::Next&&d.view.card==36&&d.generation==2,"native completion releases the exact receipt");
    Check(!c.Active()&&c.Poll(s).action==Action::None,"released window cannot acknowledge twice");
    c.Begin(2,42,notice);s=Ready();s.window=0;d=c.Poll(s);
    Check(d.action==Action::Release&&d.navigation==Presentation::Navigation::None,"unconfirmed native closure leaves notice pending");
    c.Begin(2,42,notice);s=Ready();s.generation=3;d=c.Poll(s);
    Check(d.action==Action::Close&&d.navigation==Presentation::Navigation::None,"new save closes a surviving owned window without acknowledgement");
    s.window=0;d=c.Poll(s);Check(d.action==Action::Release&&d.navigation==Presentation::Navigation::None,"stale owned slot is restored only after native close");
    c.Begin(2,42,notice);s=Ready();s.generation=3;s.suspended=true;
    Check(c.Poll(s).action==Action::None&&c.Active(),"save change in another bank retains a cleanup lease");
    s.suspended=false;Check(c.Poll(s).action==Action::Close,"return to the owned bank closes the stale window");
    s.window=0;c.Poll(s);
    c.Begin(2,42,notice);s=Ready();s.scene=43;Check(c.Poll(s).action==Action::Abandon,"scene change never restores a stale native window");
    c.Begin(2,42,notice);s=Ready();s.owned=false;Check(c.Poll(s).action==Action::Abandon,"foreign text or callback ownership is never overwritten");
    c.Begin(2,42,notice);s=Ready();s.suspended=true;
    Check(c.Poll(s).action==Action::None&&c.Active(),"another native buffer bank suspends without touching its windows");
    s.suspended=false;s.stopping=true;d=c.Poll(s);
    Check(d.action==Action::Close&&d.navigation==Presentation::Navigation::None,"logical stop closes only the owned window without granting acknowledgement");
    s.window=0;d=c.Poll(s);Check(d.action==Action::Release&&d.navigation==Presentation::Navigation::None,"stopped close preserves unread receipt");
    c.Begin(2,42,{Presentation::Kind::Introduction,36,3,false});s=Ready();c.Poll(s);s.previous=true;d=c.Poll(s);
    Check(d.action==Action::Close&&d.navigation==Presentation::Navigation::Previous,"tutorial back is delivered only after native close");
    s.foreign=true;d=c.Poll(s);Check(d.action==Action::None,"foreign window while closing cannot cause a second close call");
    s.window=0;d=c.Poll(s);Check(d.navigation==Presentation::Navigation::None,"foreign takeover cancels a queued acknowledgement");
    c.Begin(2,42,notice);s=Ready();s.focused=false;c.Poll(s);s.focused=true;s.confirm=true;
    Check(c.Poll(s).action==Action::None,"focus return requires release before a new confirmation");
    Controller skip;
    const Presentation::View introduction{Presentation::Kind::Introduction,36,0,false};
    Check(skip.Begin(2,42,introduction),"optional guide can acquire its own dialog");
    s=Ready();s.back=true;s.text=1;
    Check(skip.Poll(s).action==Action::None,"held native Cancel on opening does not discard the guide");
    s.back=false;skip.Poll(s);s.back=true;d=skip.Poll(s);
    Check(d.action==Action::Close&&d.navigation==Presentation::Navigation::Skip,"fresh native Cancel skips during text reveal through the configured action");
    Check(skip.Poll(s).action==Action::None,"held native Cancel cannot request two closes");
    s.window=4;d=skip.Poll(s);
    Check(d.action==Action::Release&&d.navigation==Presentation::Navigation::Skip,"skip persists only after the native closing animation");
    skip.Begin(2,42,introduction);s=Ready();skip.Poll(s);s.back=true;skip.Poll(s);
    s.generation=3;s.window=4;d=skip.Poll(s);
    Check(d.navigation==Presentation::Navigation::None,"a save change cancels pending skip acknowledgement");
    skip.Begin(2,42,introduction);s=Ready();skip.Poll(s);s.focused=false;skip.Poll(s);s.focused=true;s.back=true;
    Check(skip.Poll(s).action==Action::None,"returning focus requires native Cancel release before skipping");
    skip.Reset();skip.Begin(2,42,introduction);s=Ready();skip.Poll(s);s.suspended=true;skip.Poll(s);s.suspended=false;s.back=true;
    Check(skip.Poll(s).action==Action::None,"closing another menu cannot skip the suspended tutorial");
    Controller supplemental;
    const Presentation::View tutorial{Presentation::Kind::System,0,1,false,47};
    Check(supplemental.Begin(8,42,tutorial),"independent system tutorial uses the same owned native dialog");
    s=Ready();s.generation=8;supplemental.Poll(s);s.confirm=true;
    Check(supplemental.Poll(s).action==Action::Close,"system tutorial requires native close before acknowledgement");
    s.window=0;d=supplemental.Poll(s);
    Check(d.action==Action::Release&&d.view.revision==47&&d.view.kind==Presentation::Kind::System,"system acknowledgement preserves the source revision and ownership");
    EncodedText text;
    Check(Encode("0Aaz\n",text)&&text.bytes[0]==0x30&&text.bytes[1]==0x50&&text.bytes[2]==0x70&&text.bytes[3]==0x89&&text.bytes[4]==3&&text.bytes[5]==0,"field text uses actual FFX glyph codes and newline control");
    Check(!Encode("bad\x01",text)&&!Encode("",text),"unsupported control bytes and empty messages fail instead of silently changing text");
    std::array<char,4096> huge{};huge.fill('A');huge.back()=0;
    Check(!Encode(huge.data(),text),"oversized native text is rejected without truncation");
    Check(!Encode(nullptr,text),"missing text fails closed");
    char wrapped[64]{};
    Check(Wrap("One two three",wrapped,sizeof(wrapped),7)&&std::string(wrapped)=="One two\nthree","native tutorial wraps whole words");
    Check(Wrap(u8"Ação nova",wrapped,sizeof(wrapped),4)&&std::string(wrapped)==u8"Ação\nnova","Portuguese UTF-8 stays intact at the wrap boundary");
    Check(!Wrap("long",wrapped,3,4)&&!Wrap("bad\xC3",wrapped,sizeof(wrapped),4),"overflow and malformed UTF-8 are rejected");
    std::printf("ArcanaFieldDialogRt0: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
#endif
