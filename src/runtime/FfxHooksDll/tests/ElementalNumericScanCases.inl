// Jarvis-HOOK: the actual Sensor/full-Scan producers draw one numerical source.
#include "../hooks/ElementalScanView.h"
namespace NumericScanTest {
namespace S=FfxHooks::ElementalScanView;
static unsigned total=10,requestedPage=0,readCalls=0;
static bool available=true;
static bool Source(unsigned actor,unsigned page,S::Snapshot& output) noexcept {
    ++readCalls;if(!available||actor!=0)return false;
    output={};output.total=total;output.page=page;output.count=S::VisibleCount(total,page);output.generation=7;
    for(unsigned i=0;i<output.count;++i){auto& row=output.rows[i];
        std::snprintf(row.label,sizeof(row.label),"Element%u",page*S::PageSize+i);
        row.baseBp=10000;row.effectiveBp=i==1?-10000:12500;row.rgb=0x55AAFF;
        row.equipmentBp=-2500;row.imperil=2;row.imperilTurns=3;row.nul=1;row.nulTurns=2;
        row.locked=i==1;row.imperilImmune=i==1;row.imperilResistanceBp=i==2?5000:0;
    }
    return true;
}
static void Configure(unsigned page){
    requestedPage=page;
    const auto text="[elemental]\nnumeric_scan=1\nscan_page="+std::to_string(page)+"\n";
    Check(FfxHooks::Config::LoadTextForTests(text.c_str(),"C:\\private-numerical-scan.ini"),"numerical page configuration is explicit and bounded");
}
static void CheckRows(unsigned count){
    char text[96]{};
    Check(FullScanTest::Has("Elemental Dominion"),"native drawing contains the numerical panel heading");
    for(unsigned i=0;i<count;++i){
        std::snprintf(text,sizeof(text),"Element%u 100%% > %d%%",requestedPage*S::PageSize+i,i==1?-100:125);
        Check(FullScanTest::Has(text),"every visible descriptor prints base and the exact signed effective percentage");
    }
    Check(FullScanTest::Has("Gear -25pp I2/3 W0/0 N1/2"),"native text exposes modifiers and remaining action counts");
    if(count>1)Check(FullScanTest::Has("LOCK | Imperil immune"),"lock and status immunity are visible text rather than color alone");
    if(count>2)Check(FullScanTest::Has("Imperil resist 50%"),"status resistance has an independent numerical label");
    for(const auto& draw:textDraws){
        if(draw.text.rfind(FullScanTest::Encoded("Element"),0)==0||draw.text.rfind(FullScanTest::Encoded("Gear "),0)==0||
           draw.text.rfind(FullScanTest::Encoded("Imperil "),0)==0||draw.text.rfind(FullScanTest::Encoded("LOCK"),0)==0)
            Check(draw.x>=0&&draw.y>=0&&draw.x<viewportWidth&&draw.y<viewportHeight,
                  "numerical drawing positions remain inside the actual viewport");
    }
}
static bool NumberValue(int value){for(const auto& drawn:numberDraws)if(drawn.value==value)return true;return false;}
static void Run(int(__cdecl* sensor)(int,int,int),bool legacyElements=false){
    FullScanTest::Run(legacyElements,false);
    static const S::Provider source{Source};
    Check(S::Register(&source),"the renderer receives a separately owned numerical provider");
    const auto frame=reinterpret_cast<int(__cdecl*)()>(imageBase+0x49BBF0);
    const auto data=reinterpret_cast<int(__cdecl*)(int,int)>(imageBase+0x49BEE0);
    const auto root=reinterpret_cast<std::uintptr_t>(FullScanTest::actors);
    std::memcpy(reinterpret_cast<void*>(imageBase+0xD334CC),&root,4);
    *reinterpret_cast<short*>(imageBase+0xF3F6C2)=0;
    const float sizes[][2]={{512,416},{1280,720},{1920,1080},{2560,1440}};
    for(const auto& size:sizes){
        viewportWidth=size[0];viewportHeight=size[1];FullScanTest::SeedActor(0,21,80);
        const unsigned currentHp=123456,maximumHp=999999;
        std::memcpy(FullScanTest::actors[0]+0x5D0,&currentHp,4);
        std::memcpy(FullScanTest::actors[0]+0x594,&maximumHp,4);
        *reinterpret_cast<short*>(imageBase+0xF3F668)=3;
        *reinterpret_cast<short*>(imageBase+0xF3F66A)=0x1000;
        for(unsigned descriptors:{8u,10u,32u}){
            total=descriptors;const unsigned pages=(total+S::PageSize-1)/S::PageSize;
            for(unsigned page=0;page<pages;++page){Configure(page);ResetDraw();
                frame();data(0,0);CheckRows(S::VisibleCount(total,page));
                Check(NumberValue(123456)&&NumberValue(999999),
                      "six-digit HP values still reach the original native resource-number consumers");
            }
        }
        total=10;Configure(0);ResetDraw();showScan=true;
        Check(sensor(0x1000,0,0)==23,"numerical Sensor preserves the native producer return value");CheckRows(10);
        const unsigned before=readCalls;ResetDraw();showScan=false;
        Check(sensor(0x1000,0,0)==17&&readCalls==before&&textDraws.empty(),
              "a hidden Sensor never requests or draws numerical actor data");showScan=true;
    }
    available=false;ResetDraw();frame();data(0,0);
    Check(!FullScanTest::Has("Elemental Dominion"),"unavailable numerical source immediately restores native presentation");
    Check(S::Unregister(&source),"the provider retires without changing Scan feature ownership");
    Configure(0);available=true;ResetDraw();sensor(0x1000,0,0);
    Check(!FullScanTest::Has("Elemental Dominion"),"the renderer cannot reuse a retired snapshot");
}
}
