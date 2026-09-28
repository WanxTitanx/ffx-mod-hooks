// Jarvis-HOOK: real Ronso cost owner + Vanguard + native command commit/debit.
// All game globals, tables and storage belong to this isolated mapped fixture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoCommandCosts.h"
#include "../hooks/MinHookBatchCoordinator.h"
#include "../shared/Config.h"
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
namespace V=FfxHooks::Vanguard;
namespace R=FfxHooks::RonsoPool;
namespace C=FfxHooks::Config;
static unsigned checks=0,failures=0;
static std::uintptr_t base=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void StartupLog(const char* text){std::fputs(text,stdout);}
static void W16(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
static void W32(unsigned char* p,int v){std::memcpy(p,&v,4);}
static bool Put(void* address,const void* data,std::size_t size){
    DWORD old=0,ignored=0;if(!VirtualProtect(address,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(address,data,size);FlushInstructionCache(GetCurrentProcess(),address,size);
    return VirtualProtect(address,size,old,&ignored)!=FALSE;
}
static int __cdecl Notice(int){return 0;}
static bool Jump(unsigned rva,void* destination){
    unsigned char patch[5]={0xE9};const auto offset=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(destination)-base-rva-5);
    std::memcpy(patch+1,&offset,4);return Put(reinterpret_cast<void*>(base+rva),patch,5);
}
static std::vector<unsigned char> AbilityTable(){
    std::vector<unsigned char> out(20+148*108+1);W16(out.data(),1);W16(out.data()+10,147);W16(out.data()+12,108);W16(out.data()+14,148*108);W32(out.data()+16,20);
    for(const auto& ability:V::Abilities){W16(out.data()+20+108*ability.id,static_cast<unsigned>(out.size())-(20+148*108));
        for(const char* c=ability.label;*c;++c)out.push_back(*c==' '?58:*c=='\''?65:*c=='-'?71:static_cast<unsigned char>(*c+15));out.push_back(0);}
    return out;
}
#include "VanguardEquipmentCommandCases.inl"
#include "VanguardOverdriveUiCases.inl"
int main(int argc,char** argv){
    if(argc<3||argc>4)return 2;const bool equipment=argc==4&&std::strcmp(argv[3],"equipment")==0;
    if(argc==4&&!equipment)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES),crt=LoadLibraryW(L"msvcr110.dll");
    if(!image||!crt)return 2;base=reinterpret_cast<std::uintptr_t>(image);
    Check(PrivatePeFixture::NormalizeRelocations(image),"OD private native image relocates");
    const auto reader=GetProcAddress(crt,"fread"),writer=GetProcAddress(crt,"fwrite");
    Check(Put(reinterpret_cast<void*>(base+0x70C3F4),&reader,4)&&Put(reinterpret_cast<void*>(base+0x70C428),&writer,4),"only private native CRT imports are resolved");
    C::ResetForTests();Check(C::LoadTextForTests("[vanguard]\nefficiency=1\nequipment_partial_overdrive=1\n[f8_authority]\nvanguard_efficiency=1\nvanguard_equipment_partial_overdrive=1\n","C:\\private-vanguard-od.ini"),"independent cost options are explicit");
    if(equipment)Check(C::LoadTextForTests("[vanguard]\nefficiency=1\nequipment_partial_overdrive=1\nequipment_active_commands=1\n[f8_authority]\nvanguard_efficiency=1\nvanguard_equipment_partial_overdrive=1\nvanguard_equipment_active_commands=1\n[vanguard_commands]\nhero_bravery=864321\n","C:\\private-vanguard-equipment.ini"),"equipment command binding is configured explicitly");
    V::CaptureStartup();
    Check(R::CommandCosts::Requested(),"Vanguard primes reusable Ronso cost infrastructure before native startup");
    // Keep the fixture running after a RED startup request assertion to exercise
    // downstream behavior; this is not a fallback in production.
    R::CommandCosts::Request(true);
    const std::wstring directory(argv[2],argv[2]+std::strlen(argv[2]));R::PreparedRuntime prepared{};
    Check(R::PrepareRuntime(base,false,nullptr,&prepared,directory.c_str())&&prepared.count==4,
          "Vanguard borrows cost/entry/reset without giving any character Kimahri's capacity override");
    if(prepared.count!=4)return 1;
    using namespace FfxHooks::MinHookBatch;
    Check(EnsureProcessInitialized()==InitializationResult::Ready,"single native coordinator initializes");
    std::array<std::uintptr_t,5> targets{};
    for(std::size_t i=0;i<prepared.count;++i){const auto& h=prepared.hooks[i];targets[i]=h.address;
        Check(MH_CreateHook(reinterpret_cast<void*>(h.address),h.replacement,h.original)==MH_OK,"existing Ronso owner creates its native entry");}
    Check(EnableBatch(&ProcessCoordinator(),RuntimeBatchIo(),Owner::NovaSuperDamage,targets.data(),prepared.count).result==BatchResult::Applied,"existing Ronso batch activates once");
    R::ActivateRuntime();
    std::array<unsigned char,31*0xF90> actors{};
    std::vector<unsigned char> commands(20+320*96+1);W16(commands.data(),1);W16(commands.data()+10,319);W16(commands.data()+12,96);W16(commands.data()+14,320*96);W32(commands.data()+16,20);
    auto abilities=AbilityTable();const auto ap=reinterpret_cast<std::uintptr_t>(actors.data()),cp=reinterpret_cast<std::uintptr_t>(commands.data()),kp=reinterpret_cast<std::uintptr_t>(abilities.data());
    Put(reinterpret_cast<void*>(base+0xD334CC),&ap,4);Put(reinterpret_cast<void*>(base+0xD2A92C),&cp,4);Put(reinterpret_cast<void*>(base+0xD2A944),&kp,4);
    const auto size=static_cast<unsigned short>(abilities.size());Put(reinterpret_cast<void*>(base+0xD2A970),&size,2);
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    for(unsigned i=0;i<31;++i){auto* a=actors.data()+i*0xF90;a[0xC]=static_cast<unsigned char>(i);W16(a+0xE,i);a[0xDC8]=a[0xDC9]=1;a[0x592]=a[0x593]=255;a[0x5BD]=100;W32(a+0x5D0,1000);W32(a+0x5D4,1000);}
    auto* gear=reinterpret_cast<unsigned char*>(base+0xD30F2C);std::memset(gear,0,4400);
    auto* actor=actors.data();gear[2]=1;gear[11]=4;for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);W16(gear+14,0x808A);actor[0x592]=0;
    constexpr unsigned id=64;auto* command=commands.data()+20+96*id;command[0x19]=255;command[0x1A]=3;command[0x24]=3;command[0x25]=20;command[0x26]=40;
    W16(actor+0x664+(id/16)*2,1u<<(id%16));
    std::array<unsigned char,72> action{};action[3]=1;W16(action.data()+8,0x3000+id);W16(action.data()+10,255);
    const bool started=V::Start(base,false,StartupLog);
    Check(started,"Vanguard joins the same native cost owner");
    if(!started){for(unsigned rva:{0x4953F0u,0x4F4B20u}){
        std::printf("GAUGE_PROFILE base=%08X rva=%08X bytes=",static_cast<unsigned>(base),rva);
        for(unsigned at=0;at<16;++at)std::printf("%02X",*reinterpret_cast<unsigned char*>(base+rva+at));std::puts("");
    }return 1;}
    Check(Jump(0x3B06C0,reinterpret_cast<void*>(&Notice))&&Jump(0x3B0CE0,reinterpret_cast<void*>(&Notice)),"only post-debit notifications are isolated");
    using Commit=int(__cdecl*)(const unsigned char*,int);using Debit=void(__cdecl*)(unsigned);
    const auto commit=reinterpret_cast<Commit>(base+0x38ABE0);const auto debit=reinterpret_cast<Debit>(base+0x38E5F0);
    R::CommandCosts::Quote quote{};actor[0x5BC]=30;
    Check(R::CommandCosts::Read(0,command,quote)&&quote.allowed&&quote.cost==30&&quote.charge==30&&quote.maximum==100,
          "equipped Efficiency quotes 30 of the native 40 OD cost without changing maximum");
    Check(commit(action.data(),0)==-1&&actor[0x6CD]==30&&actor[0x6CC]==15&&actor[0x5BC]==30,
          "native confirmation stages the same reduced OD and MP costs without an early debit");
    debit(0);Check(actor[0x5BC]==0&&actor[0x5BD]==100&&*reinterpret_cast<int*>(actor+0x5D4)==985&&!actor[0x6CC]&&!actor[0x6CD],
          "unchanged native debit charges the staged fee once and clears it");
    debit(0);Check(actor[0x5BC]==0&&*reinterpret_cast<int*>(actor+0x5D4)==985,"repeated native debit cannot double-charge");
    actor[0x5BC]=29;Check(commit(action.data(),0)==0&&!actor[0x6CD],"insufficient reduced OD cannot enter a paid action");
    actor[0x5BC]=40;actor[0x592]=255;Check(commit(action.data(),0)==-1&&actor[0x6CD]==40,"unequipping Efficiency restores the native fee, not its previous discount");debit(0);
    actor[0x592]=0;actor[0x5BC]=30;W16(actor+0x616,0x400);
    Check(commit(action.data(),0)==0&&!actor[0x6CD],"Curse blocks discounted partial Overdrive");W16(actor+0x616,0);
    W32(command+0x1C,0x20000);actor[0x609]=1;
    Check(commit(action.data(),0)==0&&!actor[0x6CD],"Silence restrictions survive reuse of the cost owner");actor[0x609]=0;
    W32(actor+0x5D4,14);Check(commit(action.data(),0)==0&&!actor[0x6CD],"discounted MP affordability is still required");W32(actor+0x5D4,1000);
    W16(actor+0x6BC,0x4000);Check(commit(action.data(),0)==-1&&actor[0x6CC]==5&&actor[0x6CD]==30,"Half MP and Efficiency reductions add while the OD fee remains 75 percent");debit(0);W16(actor+0x6BC,0);
    Check(command[0x26]==40&&command[0x25]==20&&actor[0x5BD]==100,"every quote and transaction leaves kernel costs and pool capacity unchanged");
    actor[0x5BC]=100;W32(actor+0x5D4,1000);
    Check(commit(action.data(),0)==-1,"full execution-chain cost fixture commits an affordable selection");
    using ResolveCost=void(__cdecl*)(unsigned,unsigned char*,const unsigned char*,unsigned);
    reinterpret_cast<ResolveCost>(base+0x3B03F0)(0,actor,action.data(),0);
    Check(actor[0x6CD]==30,"post-selection native cost resolution retains the approved Efficiency fee");
    debit(0);Check(actor[0x5BC]==70&&*reinterpret_cast<int*>(actor+0x5D4)==985,
                  "complete native selection-cost-debit chain pays the displayed discount, not the raw kernel fee");
    if(equipment)EquipmentCommandCases(actor,gear,commands);
    else OverdriveUiCases(actor,command);
    V::RequestStop();R::RequestStop();
    Check(!R::CommandCosts::nativeReady.load(),"cost-owner teardown is an atomic admission close");
    std::printf("VANGUARD_OVERDRIVE_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
