#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: render a value snapshot; never recompute gameplay or write actors.
bool CaptureNumerical(ScanScope& current){
    if(!numericScanEnabled)return false;
    const int page=Config::GetInt("elemental.scan_page",0);
    if(page<0||page>=static_cast<int>(ElementalScanView::MaximumPages))return false;
    current.numeric=ElementalScanView::Capture(static_cast<unsigned>(current.actor)&255u,
                                               static_cast<unsigned>(page),current.numerical);
    return current.numeric;
}
float NumericalBodyHeight(const ScanScope& current){return 52.f+58.f*static_cast<float>((current.numerical.count+1)/2);}
float NumericalExtraHeight(){
    if(!scope||!scope->numeric)return 0.f;
    const float needed=NumericalBodyHeight(*scope)-240.f;return needed>0?needed:0.f;
}
void NumericalText(const char* plain,float x,float y,float sx,float sy){
    static constexpr char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_\140abcdefghijklmnopqrstuvwxyz";
    unsigned char encoded[128]{};unsigned n=0;
    for(const char* p=plain;*p&&n+1<sizeof(encoded);++p){
        const char* found=std::strchr(alphabet,*p);encoded[n++]=static_cast<unsigned char>(found?0x30+found-alphabet:0x3A);
    }
    using Draw=int(__cdecl*)(unsigned,const unsigned char*,float,float,unsigned,float,float);
    NativeText::DrawPlain(reinterpret_cast<Draw>(originals[Text]),0,encoded,x,y,0,sx,sy);
}
void DrawNumerical(ScanScope& current,float x,float y,float height){
    if(!current.numeric||!current.numerical.count)return;
    using Fill=void(__cdecl*)(float,float,float,float,unsigned,unsigned);
    const auto fill=reinterpret_cast<Fill>(module+::FfxHooks::ExecutableProfile::Rva<0x4F4B20>());
    const unsigned surface=ElementScan::NativeColor(0x162737u);
    const unsigned cardTop=ElementScan::NativeColor(0x253B50u),cardBottom=ElementScan::NativeColor(0x1C2E40u);
    const unsigned divider=ElementScan::NativeColor(0x526A7Eu);
    fill(x,y,X(750.f),Y(height),surface,surface);
    char header[80]{};
    std::snprintf(header,sizeof(header),"Elemental Dominion  %u/%u",current.numerical.page+1,
        (current.numerical.total+ElementalScanView::PageSize-1)/ElementalScanView::PageSize);
    NumericalText(header,x+X(14),y+Y(7),.52f,.82f);
    fill(x+X(12),y+Y(31),X(726),Y(1),divider,divider);
    for(unsigned i=0;i<current.numerical.count;++i){
        ElementalScanView::Lines lines{};if(!ElementalScanView::Format(current.numerical.rows[i],lines))continue;
        const float left=x+X(10.f+365.f*static_cast<float>(i&1));
        const float top=y+Y(35.f+58.f*static_cast<float>(i/2));
        const auto uv=ElementScan::SilverSphere;
        const auto color=ElementScan::NativeColor(current.numerical.rows[i].rgb);
        fill(left,top,X(354),Y(54),cardTop,cardBottom);
        fill(left,top,X(3),Y(54),color,color);
        reinterpret_cast<TintFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x503EE0>()))(0x1AF,left+X(9),top+Y(3),X(18),Y(18),uv.u0,uv.v0,uv.u1,uv.v1,color,color);
        NumericalText(lines.title,left+X(32),top+Y(2),.42f,.72f);
        NumericalText(lines.effects,left+X(9),top+Y(21),.32f,.57f);
        NumericalText(lines.restriction,left+X(9),top+Y(38),.32f,.55f);
    }
    NumericalText("I Imperil   W Ward   N Nul   stacks/actions",x+X(14),y+Y(height-18),.34f,.59f);
}
