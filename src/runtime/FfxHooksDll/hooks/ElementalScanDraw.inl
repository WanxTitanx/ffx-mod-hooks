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
    reinterpret_cast<Draw>(originals[Text])(0,encoded,x,y,0,sx,sy);
}
void DrawNumerical(ScanScope& current,float x,float y,float height){
    if(!current.numeric||!current.numerical.count)return;
    reinterpret_cast<PanelFn>(originals[Panel])(x,y,X(750.f),Y(height),3);
    char header[80]{};
    std::snprintf(header,sizeof(header),"Elemental Dominion  %u/%u",current.numerical.page+1,
        (current.numerical.total+ElementalScanView::PageSize-1)/ElementalScanView::PageSize);
    NumericalText(header,x+X(14),y+Y(8),.52f,.82f);
    for(unsigned i=0;i<current.numerical.count;++i){
        ElementalScanView::Lines lines{};if(!ElementalScanView::Format(current.numerical.rows[i],lines))continue;
        const float left=x+X(14.f+365.f*static_cast<float>(i&1));
        const float top=y+Y(35.f+58.f*static_cast<float>(i/2));
        const auto uv=ElementScan::SilverSphere;
        const auto color=ElementScan::NativeColor(current.numerical.rows[i].rgb);
        reinterpret_cast<TintFn>(module+0x503EE0)(0x1AF,left,top+Y(3),X(12),Y(12),uv.u0,uv.v0,uv.u1,uv.v1,color,color);
        NumericalText(lines.title,left+X(17),top,.42f,.72f);
        NumericalText(lines.effects,left,top+Y(19),.34f,.58f);
        NumericalText(lines.restriction,left,top+Y(36),.34f,.58f);
    }
    NumericalText("I Imperil   W Ward   N Nul   stacks/actions",x+X(14),y+Y(height-20),.36f,.62f);
}
void DrawSensorNumerical(ScanScope& current){
    if(!current.sensorPanel||!CaptureNumerical(current))return;
    const float width=X(750.f),height=NumericalBodyHeight(current);
    const float minimumX=X(20),maximumX=X(1900)-width;
    float left=current.panelX+current.panelW+X(12);
    if(left>maximumX)left=current.panelX-width-X(12);
    if(left<minimumX)left=minimumX;
    if(left>maximumX)left=maximumX;
    float top=current.panelY;
    if(top+Y(height)>Y(1060))top=Y(1060-height);
    if(top<Y(20))top=Y(20);
    DrawNumerical(current,left,top,height);
}
