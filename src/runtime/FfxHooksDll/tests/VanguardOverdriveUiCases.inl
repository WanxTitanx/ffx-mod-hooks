// Original native actor-gauge body and getters execute. Only the GPU submission
// primitives are intercepted; positions, widths, colours and read-only state are
// asserted instead of claiming a live screenshot/GPU acceptance result.
struct OdRect {float x,y,w,h;unsigned top,bottom;};
static std::vector<OdRect> odRects;
static unsigned nativeGaugePrimitives=0;
static int __cdecl GaugeNativeRect(int,int,int,int,int,int,int,int){++nativeGaugePrimitives;return 0;}
static int __cdecl GaugeNativeGradient(int,int,int,int,unsigned,unsigned,int){++nativeGaugePrimitives;return 0;}
static int __cdecl GaugeCostRect(float x,float y,float w,float h,unsigned a,unsigned b){odRects.push_back({x,y,w,h,a,b});return 0;}
static bool OdClose(float a,float b){return a>b-.001f&&a<b+.001f;}
static void OverdriveUiCases(unsigned char* actor,unsigned char* command){
    using Gauge=void(__cdecl*)(unsigned,int,int);const auto draw=reinterpret_cast<Gauge>(base+(::FfxHooks::ExecutableProfile::Rva<0x4953F0>()));
    Check(Jump((::FfxHooks::ExecutableProfile::Rva<0x4FB150>()),reinterpret_cast<void*>(&GaugeNativeRect))&&
          Jump((::FfxHooks::ExecutableProfile::Rva<0x4F3EB0>()),reinterpret_cast<void*>(&GaugeNativeGradient))&&
          Jump((::FfxHooks::ExecutableProfile::Rva<0x4F4B20>()),reinterpret_cast<void*>(&GaugeCostRect)),"only terminal GPU primitives are intercepted in the isolated gauge fixture");
    auto* windows=reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0xF3C910>()));std::memset(windows,0,8*0xF0);
    auto* window=windows+0xF0;window[1]=3;window[8]=0;W16(window+0x1E,0x3040);
    std::uint16_t list[]={0x3040};const auto lp=reinterpret_cast<std::uintptr_t>(list);
    std::memcpy(window+0x20,&lp,4);W32(window+0x24,1);W16(window+0x42,0);
    *reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x1FCC092>()))=1;
    W32(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x1FCC08C>())),1);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AA2>())),4);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AA4>())),40);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AA6>())),5);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AA8>())),3);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AB0>())),6);
    W16(reinterpret_cast<unsigned char*>(base+(::FfxHooks::ExecutableProfile::Rva<0x21D0AB2>())),42);
    actor[0x5BC]=70;actor[0x5BD]=100;command[0x26]=40;
    const auto before=std::vector<unsigned char>(actor,actor+0xF90);
    const auto beforeCommand=std::vector<unsigned char>(command,command+96);
    nativeGaugePrimitives=0;odRects.clear();draw(0,100,200);
    Check(nativeGaugePrimitives==2,"the original gauge background and charge fill each render once");
    Check(odRects.size()==1&&OdClose(odRects[0].x,118)&&OdClose(odRects[0].y,205)&&
          OdClose(odRects[0].w,12)&&OdClose(odRects[0].h,4)&&odRects[0].top==0x80FFFFFF&&odRects[0].bottom==0x80FFFFFF,
          "affordable effective OD30 is the white consumed tail of the current70/100 native gauge");
    Check(!std::memcmp(actor,before.data(),before.size())&&!std::memcmp(command,beforeCommand.data(),96),
          "drawing the cost never stages/debits resources or rewrites command data");
    actor[0x5BC]=20;odRects.clear();draw(0,100,200);
    Check(odRects.size()==2&&OdClose(odRects[0].x,102)&&OdClose(odRects[0].w,8)&&
          odRects[0].top==0x80FFFFFF&&OdClose(odRects[1].x,110)&&OdClose(odRects[1].w,4)&&
          odRects[1].top==0x800000FF&&odRects[1].bottom==0x800000FF,
          "insufficient20/100 shows the available20 white and missing10 red in native ABGR order");
    actor[0x5BC]=150;actor[0x5BD]=200;odRects.clear();draw(0,100,200);
    Check(odRects.size()==1&&OdClose(odRects[0].x,126)&&OdClose(odRects[0].w,6),
          "cost rendering uses the actual200 pool maximum rather than assuming100");
    command[0x26]=80;odRects.clear();draw(0,100,200);
    Check(odRects.size()==1&&OdClose(odRects[0].w,12),"a changed current kernel price is reflected on the next draw, never cached");
    window[8]=1;odRects.clear();draw(0,100,200);Check(odRects.empty(),"another actor's selected command cannot annotate this gauge");
    window[8]=0;window[1]=0;odRects.clear();draw(0,100,200);Check(odRects.empty(),"closed menu state has no stale cost overlay");
    window[1]=3;W16(window+0x1E,0x3041);odRects.clear();draw(0,100,200);Check(odRects.empty(),"selected ID must match the bounded current native list");
    W16(window+0x1E,0x3040);actor[0x5BD]=0;odRects.clear();draw(0,100,200);Check(odRects.empty(),"invalid zero capacity cannot produce non-finite coordinates");
    std::memcpy(actor,before.data(),before.size());std::memcpy(command,beforeCommand.data(),96);
}
