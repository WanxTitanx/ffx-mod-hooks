// Independent native frame, content and description paths for full Scan data.
// The native instructions use VA 0133F668/0133F66A in a PE based at 00400000.
// These globals belong to F3xxxx, not the D3xxxx actor/save region.
constexpr std::uintptr_t FullScanStateRva=0x0133F668u-0x00400000u;
constexpr std::uintptr_t FullScanTargetRva=0x0133F66Au-0x00400000u;
using FullFrameFn=int(__cdecl*)();
using FullDataFn=int(__cdecl*)(int,int);
using FullDescriptionFn=int(__cdecl*)(int);
using TextFn=int(__cdecl*)(unsigned,const unsigned char*,float,float,unsigned,float,float);
using NumberFn=int(__cdecl*)(int,float,float,unsigned,float,float);
using GlyphFn=int(__cdecl*)(const void*,int,int);
using RotatedFn=int(__cdecl*)(unsigned,float,float,float,float,float,float,float,float,unsigned,unsigned,float);
bool FullScope(){return active.load()&&scope&&scope->depth<4&&scope->full;}
float ExtraWidth(){return scope->numeric?320.f:scope->extras?63.f*ElementScan::VisibleCount(scope->settings):0.f;}
float ExtraHeight(){return NumericalExtraHeight()+(scope->expanded?70.f+(scope->maxMp?45.f:0.f):0.f);}
bool PrepareFull(ScanScope& current,ScanSection section){
    if(!active.load())return false;
    unsigned expected=0;const auto thread=GetCurrentThreadId();drawingThread.compare_exchange_strong(expected,thread);
    if(drawingThread.load()!=thread)return false;
    short state=0,target=0;
    if(!NativeUiSupport::Copy(&state,reinterpret_cast<const void*>(module+FullScanStateRva),2)||state<2||state>6||
       !NativeUiSupport::Copy(&target,reinterpret_cast<const void*>(module+FullScanTargetRva),2)||target<0||(target&0xFF)>=0x1F)return false;
    current.expanded=expandedStatsEnabled;
    // Invalid element preferences must not suppress the independent stats view.
    current.extras=extraElementsEnabled&&ElementScan::ReadSettings(current.settings);
    // RVA394030 resolves the low BYTE; rows also pass a narrowed actor slot.
    current.actor=target&0xFF;current.section=section;current.depth=scope?scope->depth+1:0;
    if(current.depth>=4)return false;
    if(CaptureNumerical(current))current.extras=false;
    if(!current.expanded&&!current.extras&&!current.numeric)return false;
    // Failed target reads during scene teardown preserve the native path.
    if(current.expanded)__try{
        const auto* actor=reinterpret_cast<const unsigned char*(__cdecl*)(unsigned)>(module+0x394030)(static_cast<unsigned>(target));
        if(!actor||!NativeUiSupport::Copy(current.stats,actor+0x5A8,8)||
           !NativeUiSupport::Copy(&current.mp,actor+0x5D4,4)||!NativeUiSupport::Copy(&current.maxMp,actor+0x598,4))return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    current.full=true;return current.depth<4;
}
void StatText(const char* text,float x,float y){
    static constexpr char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    unsigned char encoded[96]{};unsigned n=0;
    for(const char* p=text;*p&&n+1<sizeof(encoded);++p){const char* found=std::strchr(alphabet,*p);encoded[n++]=static_cast<unsigned char>(found?0x30+found-alphabet:0x3A);}
    reinterpret_cast<TextFn>(originals[Text])(0,encoded,x,y,0,.60f,.84f);
}
void FullStats(int offset){
    if(!FullScope()||!scope->expanded||scope->section!=ScanSection::Data)return;
    const float left=static_cast<float>(offset)+X(500.f-ExtraWidth()*.5f);
    char text[80]{};const float y0=630.f+(scope->maxMp?45.f:0.f)-ExtraHeight();
    const auto& band=scope->hpBand;
    if(scope->maxMp&&band.present){
        // Reuse the native HP strip's texture coordinates and physical width.
        // The MP row follows the same horizontal expansion and 45-design-pixel pitch.
        const unsigned blue=ElementScan::NativeColor(0x70BCFFu);
        reinterpret_cast<TintFn>(originals[Tinted])(band.atlas,band.x,band.y+Y(45.f),band.w,band.h,band.u0,band.v0,band.u1,band.v1,blue,blue);
        const auto& label=scope->hpLabel;
        if(label.present){
            // Native large MP glyph: selector EA, UV (505,2,586,64)/1024,
            // sampled by preferred-VA 008F172B in the supported executable.
            reinterpret_cast<TextureFn>(originals[Texture])(0xEA,label.x,label.y+Y(45.f),label.w*81.f/84.f,label.h,
                505.f/1024.f,2.f/1024.f,586.f/1024.f,64.f/1024.f);
        }
        for(unsigned i=0;i<2;++i){const auto& number=scope->hpNumbers[i];if(number.present)
            reinterpret_cast<NumberFn>(originals[i?NumberLeft:NumberRight])(static_cast<int>(i?scope->maxMp:scope->mp),number.x,number.y+Y(45.f),number.style,number.sx,number.sy);}
        if(scope->hpSlash)reinterpret_cast<GlyphFn>(originals[Glyph])(scope->hpSlash,scope->slashX,scope->slashY+static_cast<int>(Y(45.f)));
    }
    if(band.present){
        using HighlightFn=void(__cdecl*)(float,float,float,float,unsigned,unsigned);
        const auto highlight=reinterpret_cast<HighlightFn>(module+0x4F4B20);
        const unsigned top=(ElementScan::NativeColor(0x354A68u)&0xFFFFFFu)|0x48000000u;
        const unsigned bottom=(ElementScan::NativeColor(0x1E293Fu)&0xFFFFFFu)|0x28000000u;
        for(unsigned row=0;row<4;++row)highlight(band.x,Y(y0-2.f+35.f*row),band.w,Y(33.f),top,bottom);
    }
    const char* names[]={"Strength","Defense","Magic","Magic Def","Agility","Luck","Evasion","Accuracy"};
    for(unsigned i=0;i<8;++i){std::snprintf(text,sizeof(text),"%s  %u",names[i],static_cast<unsigned>(scope->stats[i]));
        StatText(text,left+X((i&1)?220.f:0.f),Y(y0+35.f*(i/2)));}
}
int __cdecl FullFrameShim(){
    ScanScope current{};if(!PrepareFull(current,ScanSection::Frame))return reinterpret_cast<FullFrameFn>(originals[FullFrame])();
    auto* previous=scope;scope=&current;int result=0;
    __try{result=reinterpret_cast<FullFrameFn>(originals[FullFrame])();}__finally{scope=previous;}return result;
}
int __cdecl FullDataShim(int x,int y){
    ScanScope current{};if(!PrepareFull(current,ScanSection::Data))return reinterpret_cast<FullDataFn>(originals[FullData])(x,y);
    auto* previous=scope;scope=&current;int result=0;
    __try{result=reinterpret_cast<FullDataFn>(originals[FullData])(x,y);FullStats(x);
        if(current.numeric)DrawNumerical(current,static_cast<float>(x)+X(480.f-ExtraWidth()*.5f),
            Y(696.f-NumericalExtraHeight()),252.f+NumericalExtraHeight());
    }__finally{scope=previous;}return result;
}
int __cdecl FullDescriptionShim(int x){
    ScanScope current{};if(!PrepareFull(current,ScanSection::Description))return reinterpret_cast<FullDescriptionFn>(originals[FullDescription])(x);
    auto* previous=scope;scope=&current;int result=0;
    __try{result=reinterpret_cast<FullDescriptionFn>(originals[FullDescription])(x);}__finally{scope=previous;}return result;
}
int __cdecl FullRowShim(int actor,int category,int x,int y){

    if(FullScope()&&scope->section==ScanSection::Data)x-=static_cast<int>(X(ExtraWidth()*.5f));
    const int result=reinterpret_cast<RowFn>(originals[FullRow])(actor,category,x,y);
    Extras(actor,category,static_cast<float>(x),static_cast<float>(y));return result;
}
int __cdecl TintShim(unsigned atlas,float x,float y,float w,float h,float u0,float v0,float u1,float v1,unsigned c0,unsigned c1){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(FullScope()&&scope->section==ScanSection::Frame&&(caller==0x49BD7F||caller==0x49BE11)){x-=X(ExtraWidth()*.5f);y-=Y(ExtraHeight());}
    return reinterpret_cast<TintFn>(originals[Tinted])(atlas,x,y,w,h,u0,v0,u1,v1,c0,c1);
}
int __cdecl RotateShim(unsigned atlas,float x,float y,float w,float h,float u0,float v0,float u1,float v1,unsigned c0,unsigned c1,float angle){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    // Rotated sprites use design coordinates, not pre-scaled coordinates.
    if(FullScope()&&scope->section==ScanSection::Frame&&(caller==0x49BC87||caller==0x49BCF7)){x-=ExtraWidth()*.5f;y-=ExtraHeight();}
    return reinterpret_cast<RotatedFn>(originals[Rotated])(atlas,x,y,w,h,u0,v0,u1,v1,c0,c1,angle);
}
int __cdecl TextShim(unsigned font,const unsigned char* text,float x,float y,unsigned style,float sx,float sy){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(FullScope()){
        if(scope->section==ScanSection::Description&&caller==0x49C7BF){x+=X(ExtraWidth()*.5f);y-=Y(ExtraHeight());}
        else if(scope->section==ScanSection::Data&&caller>=0x49BEE0&&caller<0x49C740){x-=X(ExtraWidth()*.5f);if(caller==0x49BF98)y-=Y(ExtraHeight());}
    }
    return reinterpret_cast<TextFn>(originals[Text])(font,text,x,y,style,sx,sy);
}
int NumberAt(unsigned target,std::uintptr_t caller,int value,float x,float y,unsigned style,float sx,float sy){
    if(FullScope()&&scope->section==ScanSection::Data){
        if(scope->expanded&&(caller==0x49C447||caller==0x49C4A2))return 0;
        if(caller==0x49C447||caller==0x49C4A2)x-=X(ExtraWidth()*.5f);
        if(scope->numeric&&(caller==0x49C447||caller==0x49C4A2))y-=Y(NumericalExtraHeight());
        if(caller==0x49C198||caller==0x49C232){x-=X(ExtraWidth()*.5f);y-=Y(ExtraHeight());}
        if(scope->expanded&&(caller==0x49C198||caller==0x49C232))scope->hpNumbers[caller==0x49C232?1:0]={x,y,style,sx,sy,true};
    }
    return reinterpret_cast<NumberFn>(originals[target])(value,x,y,style,sx,sy);
}
int __cdecl NumberRightShim(int value,float x,float y,unsigned style,float sx,float sy){return NumberAt(NumberRight,reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module,value,x,y,style,sx,sy);}
int __cdecl NumberLeftShim(int value,float x,float y,unsigned style,float sx,float sy){return NumberAt(NumberLeft,reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module,value,x,y,style,sx,sy);}
int __cdecl GlyphShim(const void* text,int x,int y){
    if(FullScope()&&scope->section==ScanSection::Data&&reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module==0x49C1D7){
        x-=static_cast<int>(X(ExtraWidth()*.5f));y-=static_cast<int>(Y(ExtraHeight()));
        if(scope->expanded){scope->hpSlash=text;scope->slashX=x;scope->slashY=y;}
    }
    return reinterpret_cast<GlyphFn>(originals[Glyph])(text,x,y);
}
