#include "../hooks/NativeGamepadCore.h"
#include <cstdio>
#include <cstring>
using namespace FfxHooks::NativeGamepad;
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
int main(){
    Check(ValidCombo(Back|A) && ValidCombo(LeftTrigger|RightTrigger|StartButton),"button and trigger combinations are supported");
    Check(!ValidCombo(A) && !ValidCombo(0x80000000u) && ValidCombo(0),"single accidental presses rejected and unbind accepted");
    EdgeState edge{};
    Check(!Pressed(edge,Back|A,Back|A,false),"disabled action observes held buttons without firing");
    Check(!Pressed(edge,Back|A,Back|A,true),"reenabling with the combo held cannot trigger");
    Check(!Pressed(edge,0,Back|A,true) && Pressed(edge,Back|A,Back|A,true),"release then a deliberate combo fires once");
    Check(!Pressed(edge,Back|A,Back|A,true),"held combo never repeats");
    Check(!Pressed(edge,Back|A|B,Back|A,true),"extra buttons cannot trigger a narrower combination");
    Capture capture{};capture.Begin(Back|A);
    Check(!capture.Sample(Back|A).done && !capture.Sample(0).done,"opening combo must release before capture begins");
    Check(!capture.Sample(LeftShoulder).done && !capture.Sample(LeftShoulder|Y).done,"capture accumulates a complete chord");
    const auto result=capture.Sample(0);
    Check(result.done && result.mask==(LeftShoulder|Y) && result.valid,"releasing the captured combo commits the full chord");
    capture.Begin(0);capture.Sample(A);
    Check(!capture.Sample(0).valid,"single-button capture is rejected instead of arming an accidental action");
    auto map=IdentityMap();
    Check(ValidMap(map) && !Changed(map),"remapping defaults to exact identity");
    Check(Remap(A|B|DpadUp,map)==(A|B|DpadUp),"identity preserves all buttons");
    Check(SwapDestination(map,0,1) && ValidMap(map) && Changed(map),"choosing a used destination swaps the pair and preserves a permutation");
    Check(Remap(A|DpadUp,map)==(B|DpadUp) && Remap(B,map)==A,"remapped face buttons preserve directional input");
    map[0]=map[1];Check(!ValidMap(map),"duplicate persisted destinations are rejected");
    char text[128]{};
    Check(Format(Back|A,text,sizeof(text)) && std::strstr(text,"Back") && std::strstr(text,"A"),"readable combination text");
    Check(Format(0,text,sizeof(text)) && std::strcmp(text,"Unassigned")==0,"unassigned state is explicit");
    std::printf("NativeGamepadRt0: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
