#include "../hooks/UiCaptionFrame.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 using namespace FfxHooks::UiCaption;
 Frame frame{};
 assert(frame.Add(u8"設定",.25f,.1f,.5f,.04f));
 assert(frame.count==1);
 assert(!frame.Add("bad",std::numeric_limits<float>::quiet_NaN(),0,.5f,.04f));
 assert(!frame.Add("bad",0,0,-1,.04f));
 for(unsigned i=1;i<MaxLines;++i)assert(frame.Add("row",.1f,.2f,.7f,.05f));
 assert(!frame.Add("overflow",0,0,.5f,.04f));
 assert(frame.count==MaxLines);
 assert(Fresh(10,20));
 assert(!Fresh(10,10+MaxAgeMs+1));
 assert(Fresh(0xFFFFFFF0u,10));
 assert(!Fresh(10,9));
 std::puts("UI caption bounds/expiry regressions passed");
}
