// Jarvis-HOOK: isolated native cast pricing, selection and queue completion.
// Native constructor count writes execute; only its geometry suffix is isolated.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../shared/Config.h"
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
namespace V=FfxHooks::Vanguard;
static std::uintptr_t base=0;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static void W16(unsigned char* p,unsigned n){p[0]=static_cast<unsigned char>(n);p[1]=static_cast<unsigned char>(n>>8);}
static void W32(unsigned char* p,unsigned n){std::memcpy(p,&n,4);}
static unsigned R32(const unsigned char* p){unsigned n=0;std::memcpy(&n,p,4);return n;}
static bool Jump(unsigned rva,void* destination){
    unsigned char bytes[5]={0xE9};const auto offset=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(destination)-base-rva-5);
    std::memcpy(bytes+1,&offset,4);DWORD old=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(base+rva),5,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(base+rva),bytes,5);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),5);
    return VirtualProtect(reinterpret_cast<void*>(base+rva),5,old,&ignored)!=FALSE;
}
static int __cdecl Notice(unsigned){return 0;}
static __declspec(naked) void MenuGeometryEndpoint(){
    __asm {
        pop esi
        mov esp,ebp
        pop ebp
        xor eax,eax
        ret
    }
}
int main(int argc,char** argv){
    if(argc!=3)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool quick=std::strcmp(argv[2],"quick")==0||std::strcmp(argv[2],"both")==0;
    const bool white=std::strcmp(argv[2],"white")==0||std::strcmp(argv[2],"both")==0;
    if(!quick&&!white&&std::strcmp(argv[2],"off"))return 2;
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private casting image relocates");
    std::array<unsigned char,31*0xF90> actors{};auto* actor=actors.data();
    std::vector<unsigned char> commands(20+320*96+1);W16(commands.data(),1);W16(commands.data()+10,319);
    W16(commands.data()+12,96);W16(commands.data()+14,320*96);W32(commands.data()+16,20);
    const auto ap=reinterpret_cast<std::uintptr_t>(actors.data()),cp=reinterpret_cast<std::uintptr_t>(commands.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&ap,4);std::memcpy(reinterpret_cast<void*>(base+0xD2A92C),&cp,4);
    *reinterpret_cast<unsigned char*>(base+0xD2A8E0)=1;
    for(unsigned i=0;i<31;++i){auto* a=actors.data()+i*0xF90;a[0xC]=static_cast<unsigned char>(i);W16(a+0xE,i);
        a[0xDC8]=1;a[0x592]=a[0x593]=255;a[0xDE5]=255;a[0x5BD]=100;W32(a+0x5D0,1000);W32(a+0x5D4,100);}
    auto* header=commands.data()+20+96*41;header[0x16]=16;header[0x17]=1;header[0x19]=255;
    for(unsigned id:{277u,278u}){auto* row=commands.data()+20+96*id;row[0x16]=8;row[0x17]=static_cast<unsigned char>(id==277?1:2);row[0x19]=255;}
    auto* black=commands.data()+20+96*66;auto* cure=commands.data()+20+96*43;
    for(auto* row:{black,cure}){row[0x19]=255;row[0x1A]=1;row[0x24]=3;row[0x25]=20;W32(row+0x1C,0x20002);}
    black[0x17]=black[0x18]=1;cure[0x17]=cure[0x18]=2;
    for(unsigned id:{41u,43u,66u,277u,278u}){const unsigned at=0x664+(id/16)*2;
        const unsigned old=actor[at]|(unsigned(actor[at+1])<<8);W16(actor+at,old|(1u<<(id%16)));}
    std::string settings="[vanguard]\nquickcast_replace_doublecast="+std::to_string(quick)+"\ndualcast_white_magic="+std::to_string(white)+
        "\n[f8_authority]\nvanguard_quickcast_replace_doublecast=1\nvanguard_dualcast_white_magic=1\n";
    FfxHooks::Config::ResetForTests();Check(FfxHooks::Config::LoadTextForTests(settings.c_str(),"C:\\private-casting.ini"),"casting flags are independent");
    Check(V::Start(base,false,nullptr)==(quick||white),"either cast option starts independently; default OFF stays native");
    Check(Jump(0x3B06C0,reinterpret_cast<void*>(&Notice))&&Jump(0x3B0CE0,reinterpret_cast<void*>(&Notice))&&
          Jump(0x4998A1,reinterpret_cast<void*>(&MenuGeometryEndpoint)),"only post-debit notifications and constructor geometry are isolated");
    auto* window=reinterpret_cast<unsigned char*>(base+0xF3C910+0xF0);std::memset(window,0,0xF0);
    *reinterpret_cast<unsigned char*>(base+0x1FCC092)=1;W16(window+0x0C,0x3029);
    using Menu=int(__cdecl*)(unsigned,unsigned,unsigned);
    reinterpret_cast<Menu>(base+0x4997E0)(1,0,0x3029);
    Check((window[0xA4]|(unsigned(window[0xA5])<<8))==(quick?1u:2u),"native Doublecast construction requests one spell only with Quickcast");
    std::vector<unsigned char> ring(18*0x478);
    for(std::size_t at=0;at<ring.size();at+=2)W16(ring.data()+at,255);
    W16(ring.data()+120,0x3042);W16(ring.data()+72,0x302B);
    const auto rp=reinterpret_cast<std::uintptr_t>(ring.data());std::memcpy(reinterpret_cast<void*>(base+0x1F10CD8),&rp,4);
    using List=const std::uint16_t*(__cdecl*)(unsigned,unsigned,int*);
    const auto list=reinterpret_cast<List>(base+0x49B510);int entries=0;window[1]=3;
    const auto* spells=list(0,1,&entries);
    Check(spells&&entries==(white?2:1)&&spells[0]==0x3042&&(!white||spells[1]==0x3116),
          "Doublecast/Quickcast adds a native White Magic submenu only with its independent option");
    const auto* whiteSpells=list(0,2,&entries);
    Check(whiteSpells&&entries==1&&whiteSpells[0]==0x302B,"White Magic submenu retains the native learned spell list");
    auto* whiteWindow=window+0xF0;std::memset(whiteWindow,0,0xF0);W16(whiteWindow+0x0C,0x3116);
    *reinterpret_cast<unsigned char*>(base+0x1FCC092)=2;
    reinterpret_cast<Menu>(base+0x4997E0)(2,0,0x3116);
    Check((whiteWindow[6]|(unsigned(whiteWindow[7])<<8))==2,"native White Magic header opens a separate category");
    *reinterpret_cast<unsigned char*>(base+0x1FCC092)=1;window[1]=0;
    std::array<unsigned char,72> action{};action[3]=quick?1:2;
    for(unsigned i=0;i<action[3];++i){W16(action.data()+8+16*i,0x3029);W16(action.data()+10+16*i,0x3042);W32(action.data()+16+16*i,1u<<18);}
    using Commit=int(__cdecl*)(const unsigned char*,int);using Cost=int(__cdecl*)(unsigned,const unsigned char*);
    using Selected=void(__cdecl*)(unsigned,unsigned char*,const unsigned char*,unsigned);using Debit=void(__cdecl*)(unsigned);
    const auto commit=reinterpret_cast<Commit>(base+0x38ABE0);const auto cost=reinterpret_cast<Cost>(base+0x38D030);
    const auto select=reinterpret_cast<Selected>(base+0x3B03F0);const auto debit=reinterpret_cast<Debit>(base+0x38E5F0);
    actor[0xDE8]=3;W32(actor+0x5D4,100);
    Check(commit(action.data(),0)==-1&&actor[0x6CC]==(quick?40:20),"native cast confirmation quotes the correct single-spell MP fee");
    select(0,actor,action.data(),0);debit(0);
    Check(R32(actor+0x5D4)==(quick?60u:80u)&&!actor[0x6CC],"native cast debit pays the approved MP exactly once");
    if(!quick){Check(commit(action.data(),1)==-1&&actor[0x6CC]==20,"Doublecast second spell keeps its ordinary fee");select(0,actor,action.data(),1);debit(0);}
    Check(cost(0,black)==20,"ordinary Black Magic outside a cast context is not doubled");
    window[1]=3;W16(window+0x1E,0x3042);
    Check(cost(0,black)==(quick?40:20),"native cast menu displays the same MP price as confirmation");
    window[1]=0;
    // An older closed cast window must not reclassify an unrelated spell menu.
    *reinterpret_cast<unsigned char*>(base+0x1FCC092)=2;
    whiteWindow[1]=4;W16(whiteWindow+0x0C,0x3115);W16(whiteWindow+8,0);
    Check(cost(0,black)==20,"a closed stale Doublecast parent cannot double an unrelated later spell's MP cost");
    whiteWindow[1]=0;*reinterpret_cast<unsigned char*>(base+0x1FCC092)=1;
    auto* queue=reinterpret_cast<unsigned char*>(base+0xD2AC70);std::memcpy(queue,action.data(),72);queue[2]=queue[3];
    *reinterpret_cast<unsigned char*>(base+0xD2BDE1)=1;actor[0xDE5]=0;actor[0xDE7]=1;
    const auto finish=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(base+0x3B0870);
    Check(finish(0,0,0)==1&&actor[0xDE8]==(quick?2:3),"completed Quickcast leaves rank2 for native CTB charging; Doublecast keeps its rank");
    if(quick){W32(actor+0x5D4,39);Check(commit(action.data(),0)==0&&!actor[0x6CC],"insufficient double MP rejects the action without a debit");
        W32(actor+0x5D4,100);action[3]=2;Check(commit(action.data(),0)==0,"Quickcast cannot accept two spells for one action price");action[3]=1;}
    if(quick||white){W16(action.data()+10,0x302B);W32(actor+0x5D4,100);
        Check(commit(action.data(),0)==(white?-1:0),"White Magic eligibility follows its independent option");
        if(white)Check(actor[0x6CC]==(quick?40:20),"White Magic receives the active cast mode price");}
    V::RequestStop();Check(cost(0,black)==20,"stopped cast hooks preserve ordinary pricing");
    std::printf("VANGUARD_CAST_RUNTIME %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
