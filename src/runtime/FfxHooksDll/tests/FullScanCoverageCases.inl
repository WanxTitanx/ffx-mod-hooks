namespace FullScanTest {
static unsigned char actors[31][0xF90]{};
static int __cdecl Language(){return 1;}
static float __cdecl Angle(){return 0;}
static int __cdecl TextDispatch(unsigned,const unsigned char* text,float x,float y,unsigned style,float sx,float sy){return Text(text,x,y,style,sx,sy);}
static int __cdecl NumberRight(int value,float x,float y,unsigned style,float sx,float sy){numberDraws.push_back({false,value,x,y,style,sx,sy});return 1;}
static int __cdecl NumberLeft(int value,float x,float y,unsigned style,float sx,float sy){numberDraws.push_back({true,value,x,y,style,sx,sy});return 1;}
static int __cdecl Glyph(const void* text,int x,int y){glyphDraws.push_back({text,x,y});return 1;}
static int __cdecl Rotated(unsigned,float,float,float,float,float,float,float,float,unsigned,unsigned,float){return 1;}
static int __cdecl MeasureBlock(int,const void*,float* width,float* height,int,int){if(width)*width=80;if(height)*height=20;return 0;}
static std::string Encoded(const char* plain){
    static const char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    std::string expected;for(const char* at=plain;*at;++at){const char* found=std::strchr(alphabet,*at);expected.push_back(static_cast<char>(found?0x30+found-alphabet:0x3A));}
    return expected;
}
static bool Has(const char* plain){
    const auto expected=Encoded(plain);
    for(const auto& text:labels)if(text.find(expected)!=std::string::npos)return true;return false;
}
static std::uintptr_t NativeOperand(unsigned rva){
    std::uint32_t value=0;std::memcpy(&value,reinterpret_cast<const void*>(imageBase+rva),4);return value;
}
static void SeedActor(unsigned slot,unsigned value,unsigned mp){
    auto* actor=actors[slot];std::memset(actor,0,0xF90);
    const unsigned hp=1320;std::memcpy(actor+0x594,&hp,4);std::memcpy(actor+0x5D0,&hp,4);
    const unsigned currentMp=mp/2;std::memcpy(actor+0x598,&mp,4);std::memcpy(actor+0x5D4,&currentMp,4);
    for(unsigned i=0;i<8;++i)actor[0x5A8+i]=static_cast<unsigned char>(value+i);
    actor[0x540]=0x51;
}
static bool MpValues(int current,int maximum){return numberDraws.size()==4&&numberDraws[2].value==current&&numberDraws[3].value==maximum;}
static void Run(bool elements,bool expanded){
    // Decode the addresses the unmodified native dispatcher/content actually read.
    // The old fixture copied the hook's wrong RVA and bypassed the dispatcher.
    auto* state=reinterpret_cast<short*>(NativeOperand(0x495303));
    auto* target=reinterpret_cast<short*>(NativeOperand(0x49BEF5));
    auto* animation=reinterpret_cast<short*>(NativeOperand(0x49BC01));
    // Full Scan uses its own 410-pixel bands; Sensor uses 365-pixel bands.
    const float nativeBandWidth=*reinterpret_cast<const float*>(NativeOperand(0x49BFD2));
    const float nativeStatX=*reinterpret_cast<const float*>(NativeOperand(0x49C419));
    const float mpUv[]={*reinterpret_cast<const float*>(NativeOperand(0x4F16C7)),
                        *reinterpret_cast<const float*>(NativeOperand(0x4F16BD)),
                        *reinterpret_cast<const float*>(NativeOperand(0x4F16B3)),
                        *reinterpret_cast<const float*>(NativeOperand(0x4F16A6))};
    Check(reinterpret_cast<std::uintptr_t>(state)==imageBase+0xF3F668&&
          reinterpret_cast<std::uintptr_t>(target)==imageBase+0xF3F66A&&
          reinterpret_cast<std::uintptr_t>(animation)==imageBase+0xF3F6C2,
          "native Scan state/target/animation operands resolve from the pinned PE base");
    if(failures)return;
    const short savedState=*state,savedTarget=*target,savedAnimation=*animation;
    auto* actorRoot=reinterpret_cast<std::uintptr_t*>(imageBase+0xD334CC);
    const auto savedRoot=*actorRoot;*actorRoot=reinterpret_cast<std::uintptr_t>(actors);
    // Keep the real actor resolver and affinity accessors. Substitute only an
    // unloaded description table, language, measurement and graphics endpoints.
    auto maskPatch=std::find_if(patches.begin(),patches.end(),[](const Patch& p){return p.at==imageBase+0x4975C0;});
    Check(maskPatch!=patches.end()&&Write(maskPatch->at,maskPatch->before,5),"full Scan restores the actual native affinity reader");
    if(maskPatch!=patches.end())patches.erase(maskPatch);
    Redirect(0x38F810,reinterpret_cast<const void*>(&EmptyText));
    Redirect(0x4AC2A0,reinterpret_cast<const void*>(&Language));
    Redirect(0x2415C0,reinterpret_cast<const void*>(&Angle));Redirect(0x4B9600,reinterpret_cast<const void*>(&MeasureBlock));
    FfxHooks::ElementScanFullEnvironmentForTests(reinterpret_cast<void*>(&Rotated),reinterpret_cast<void*>(&TextDispatch),reinterpret_cast<void*>(&NumberRight),reinterpret_cast<void*>(&NumberLeft),reinterpret_cast<void*>(&Glyph));
    SeedActor(3,21,80);SeedActor(5,41,0);
    *animation=0;
    const auto dispatch=reinterpret_cast<void(__cdecl*)()>(imageBase+0x495300);
    const auto frame=reinterpret_cast<int(__cdecl*)()>(imageBase+0x49BBF0);
    const float savedWidth=viewportWidth,savedHeight=viewportHeight;
    std::puts("CASE full Scan through native state dispatcher and actor resolver");
    for(const auto viewport:{std::array<float,2>{512.f,416.f},std::array<float,2>{1024.f,576.f},std::array<float,2>{1920.f,1080.f}}){
    viewportWidth=viewport[0];viewportHeight=viewport[1];
    for(unsigned slot:{3u,5u})for(short phase=3;phase<=6;++phase)for(unsigned bit:{32u,64u})for(unsigned selected=0;selected<16;++selected){
        *state=phase;*target=static_cast<short>(0x1000+slot);
        char ini[300]{};_snprintf_s(ini,sizeof(ini),_TRUNCATE,
            "[element_scan]\nholy_enabled=%u\ndark_enabled=%u\nextra_enabled=%u\nextra_bit=%u\nholy_rgb=1122867\ndark_rgb=4478310\nextra_rgb=7833753\nother_enabled=%u\nother_rgb=11189196\n",
            selected&1,(selected>>1)&1,(selected>>2)&1,bit,(selected>>3)&1);
        FfxHooks::Config::LoadTextForTests(ini,"C:\\private-full-scan.ini");
        const unsigned visible=elements?((selected&1)+((selected>>1)&1)+((selected>>2)&1)+((selected>>3)&1)):0;
        // All four categories are read from this actor, not an actor-blind stub.
        for(unsigned i=0;i<4;++i)actors[slot][0x5DA+i]=0xF0;
        ResetDraw();dispatch();
        Check(std::fabs(lastWidth-ScaleX(1000.f+63.f*visible))<.01f,"Scan data frame grows by each enabled extra element");
        const char* names[]={"Strength","Defense","Magic","Magic Def","Agility","Luck","Evasion","Accuracy"};
        for(unsigned i=0;i<8;++i){char text[64]{};_snprintf_s(text,sizeof(text),_TRUNCATE,"%s  %u",names[i],unsigned(actors[slot][0x5A8+i]));
            Check(Has(text)==expanded,"Scan Expanded alone controls the eight actual monster stat values");
            const auto encoded=Encoded(text);
            auto drawn=std::find_if(textDraws.begin(),textDraws.end(),[&encoded](const TextDraw& row){return row.text==encoded;});
            Check(!expanded||(drawn!=textDraws.end()&&bands.size()==5&&drawn->y>=bands[0].y+bands[0].height&&drawn->y+ScaleY(28.f)<=bands[1].y),
                  "each added stat stays between native HP and elemental rows");}
        const bool showMp=expanded&&slot==3;
        if(!expanded){
            Check(numberDraws.size()==4&&numberDraws[2].value==actors[slot][0x5A8]&&numberDraws[3].value==actors[slot][0x5AA],"elements-only mode preserves the original Strength and Magic values");
            if(numberDraws.size()==4)for(unsigned i=2;i<4;++i)
                Check(std::fabs(numberDraws[i].x-ScaleX(nativeStatX-31.5f*visible))<.01f,"elements-only mode keeps the original stat values aligned with the shifted labels");
        }
        Check(mpLabels==unsigned(showMp)&&(!showMp||MpValues(40,80)),"native MP glyph and values require Scan Expanded and an actual MP pool");
        if(showMp&&numberDraws.size()==4)for(unsigned i=0;i<2;++i){const auto& hp=numberDraws[i];const auto& mp=numberDraws[i+2];
            Check(hp.left==mp.left&&hp.style==mp.style&&hp.sx==mp.sx&&hp.sy==mp.sy&&std::fabs(hp.x-mp.x)<.01f&&std::fabs(mp.y-hp.y-ScaleY(45.f))<.01f,"MP values retain native HP alignment, scale and style");}
        if(showMp){
            const auto hp=std::find_if(textureDraws.begin(),textureDraws.end(),[](const TextureDraw& q){return q.atlas==0xE9;});
            const auto mp=std::find_if(textureDraws.begin(),textureDraws.end(),[](const TextureDraw& q){return q.atlas==0xEA;});
            Check(hp!=textureDraws.end()&&mp!=textureDraws.end()&&mp->u0==mpUv[0]&&mp->v0==mpUv[1]&&mp->u1==mpUv[2]&&mp->v1==mpUv[3]&&
                  std::fabs(mp->x-hp->x)<.01f&&std::fabs(mp->y-hp->y-ScaleY(45.f))<.01f&&mp->h==hp->h,
                  "MP label samples the native resource glyph and follows HP label placement");
            Check(glyphDraws.size()==2&&glyphDraws[1].text==glyphDraws[0].text&&glyphDraws[1].x==glyphDraws[0].x&&
                  glyphDraws[1].y-glyphDraws[0].y==static_cast<int>(ScaleY(45.f)),"MP slash uses the native HP glyph and numeric alignment");
        }
        const auto mpBand=std::find_if(sprites.begin(),sprites.end(),[](const SpriteQuad& quad){return quad.texture==0x87654321u;});
        Check((mpBand!=sprites.end())==showMp,"only monsters with MP receive a blue native resource strip");
        if(showMp&&mpBand!=sprites.end()&&bands.size()==5){const auto& hp=bands[0];const auto& a=mpBand->corners[0];const auto& b=mpBand->corners[1];
            Check(std::fabs(a.x-hp.x)<.01f&&std::fabs(a.y-hp.y-ScaleY(45.f))<.01f&&std::fabs(b.x-a.x-hp.width)<.01f&&std::fabs(b.y-a.y-hp.height)<.01f,"blue MP strip follows the exact native HP geometry and element expansion");
            Check(((a.a<<24)|(a.b<<16)|(a.g<<8)|a.r)==E::NativeColor(0x70BCFFu),"MP strip uses the blue tint independently of element colors");}
        Check(highlights.size()==(expanded?4u:0u),"only Scan Expanded adds the four attribute highlights");
        for(const auto& highlight:highlights)Check(bands.size()==5&&std::fabs(highlight.x-bands[0].x)<.01f&&std::fabs(highlight.width-bands[0].width)<.01f&&highlight.y>=bands[0].y+bands[0].height&&highlight.y+highlight.height<=bands[1].y,"attribute highlights align with the resource bands and stay above affinities");
        const unsigned rgb[]={0x112233,0x445566,0x778899,0xAABBCC};
        for(unsigned i=0;i<4;++i)Check(HasColor(rgb[i])==bool(elements&&(selected&(1u<<i))),"full Scan uses every configured custom element and color independently of expanded stats");
        unsigned colored=0;for(const auto& sprite:sprites){const auto& c=sprite.corners[0];
            const auto packed=(c.a<<24)|(c.b<<16)|(c.g<<8)|c.r;
            for(unsigned i=0;i<4;++i)if(packed==E::NativeColor(rgb[i])){
                ++colored;
                const auto& end=sprite.corners[1];
                Check(std::any_of(bands.begin(),bands.end(),[&c,&end](const BandDraw& band){
                    return c.x>=band.x-.1f&&end.x<=band.x+band.width+.1f&&c.y>=band.y-.1f&&end.y<=band.y+band.height+.1f;}),
                    "each custom orb fits inside its native affinity background");
            }}
        Check(colored==4*visible,"each enabled custom element reaches all four native affinity rows");
        Check(bandWidths.size()==5&&std::all_of(bandWidths.begin(),bandWidths.end(),[visible,nativeBandWidth](float width){return std::fabs(width-ScaleX(nativeBandWidth+63.f*visible))<.01f;}),
              "full Scan HP and affinity backgrounds expand with their enabled columns");
    }
    }
    viewportWidth=savedWidth;viewportHeight=savedHeight;
    FfxHooks::Config::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-full-scan.ini");
    *target=0x1003;*state=2;*animation=1;ResetDraw();dispatch();
    Check(!Has("Accuracy")&&*state==2&&*animation==0,"native Scan opening animation preserves its final countdown frame");
    ResetDraw();dispatch();Check(!Has("Accuracy")&&*state==3,"native Scan opening advances without drawing body content early");
    ResetDraw();dispatch();Check(Has("Accuracy  28")==expanded&&(!expanded||MpValues(40,80)),"native transition opens only the selected Scan data on the following frame");
    Check(mpLabels==unsigned(expanded),"Scan Expanded draws the native colored MP label independently");
    Check(rectangles==(expanded?4u:0u),"Scan Expanded highlights the four pairs of monster stats independently");
    FfxHooks::Config::LoadTextForTests("[element_scan]\nholy_rgb=-1\n","C:\\private-full-scan.ini");
    ResetDraw();dispatch();Check(Has("Accuracy  28")==expanded&&std::fabs(lastWidth-ScaleX(1000.f))<.01f,"invalid element preferences do not disable independent Scan Expanded stats");
    for(short phase:{short(0),short(1),short(7),short(8)}){*state=phase;ResetDraw();dispatch();
        Check(labels.empty()&&textures==0&&sprites.empty(),"closed and unsupported native Scan states draw no extra data");}
    *state=3;*target=-1;ResetDraw();frame();
    Check(std::fabs(lastWidth-ScaleX(1000.f))<.01f&&!Has("MP"),"an invalid native Scan target retains the original frame");
    *state=savedState;*target=savedTarget;*animation=savedAnimation;*actorRoot=savedRoot;
    Redirect(0x4975C0,reinterpret_cast<const void*>(&Mask));
}
}
