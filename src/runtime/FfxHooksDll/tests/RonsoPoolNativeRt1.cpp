#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "../hooks/MinHookBatchCoordinator.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolEvidence.h"
#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <fstream>
#include <vector>
using namespace FfxHooks::RonsoPool;
namespace {
int checks=0,failures=0;
std::vector<uint8_t> commands;
int resetCalls=0;
int protectionCalls=0;
bool FailSecondProtection(uintptr_t address,size_t bytes,uint32_t desired,uint32_t* prior) noexcept {
    if(++protectionCalls==2)return false;
    DWORD value=0;const bool ok=VirtualProtect(reinterpret_cast<void*>(address),bytes,desired,&value)!=FALSE;
    *prior=value;return ok;
}
void* __cdecl KernelEntry(int command,int) {
    const unsigned index=static_cast<unsigned>(command)&0xFFFu;
    return index<367&&commands.size()>20+96*index+95?commands.data()+20+96*index:nullptr;
}
int __cdecl ResetProvider() {++resetCalls;return 41;}
int fullEvents=0;
int __cdecl FullEvent(int,int) {++fullEvents;return 0;}
int ringBuilds=0,ringKind=-1;
int leftRequests=0,leftActor=-1,leftCommand=-1;
int __cdecl LeftMenuBoundary(int actor,int command) {
    ++leftRequests;leftActor=actor;leftCommand=command;return 1;
}
int __cdecl InactiveOtherInput(int) {return 0;}
int __cdecl UnrelatedGroupList(int,int* count) {if(count)*count=0;return 0;}
int __cdecl RingBuilderBoundary(int,int,int kind,int,int,int) {
    ++ringBuilds;ringKind=kind;return 0;
}
__declspec(naked) void __cdecl InvokeOverdriveEntry(int,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        mov ebx,[ebp+8]
        mov eax,[ebp+0Ch]
        push offset entryReturn
        sub esp,18h
        jmp eax
    entryReturn:
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void __cdecl InvokeOverdriveRefresh(int,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        mov ebx,[ebp+0Ch]
        add ebx,540h
        mov eax,[ebp+10h]
        push offset refreshReturn
        sub esp,10h
        push dword ptr [ebp+8]
        jmp eax
    refreshReturn:
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void __cdecl InvokeTransfer(void*,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,8
        push esi
        mov eax,[ebp+8]
        mov [ebp-8],eax
        mov esi,[ebp+0Ch]
        call dword ptr [ebp+10h]
        pop esi
        mov esp,ebp
        pop ebp
        ret
    }
}
__declspec(naked) int __cdecl InvokeAvailability(uint32_t,int32_t,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov ebx,[ebp+8]
        mov esi,[ebp+0Ch]
        mov edi,esi
        call dword ptr [ebp+10h]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void __cdecl InvokeMaximum(int,int,void*,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov esi,[ebp+10h]
        mov edi,[ebp+14h]
        lea ebx,[esi+540h]
        call dword ptr [ebp+18h]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
struct MaximumCall {void* actor;void* entry;void* site;};
DWORD WINAPI NativeInitializationThread(void* raw) {
    const auto& call=*static_cast<MaximumCall*>(raw);
    InvokeMaximum(0,3,call.actor,call.entry,call.site);return 0;
}
DWORD WINAPI ForeignMenuThread(void* site) {
    InvokeOverdriveEntry(3,site);return 0;
}
void Expect(bool ok,const char* why) {++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
bool Write(void* address,const void* bytes,size_t size) {
    DWORD prior=0;if(!VirtualProtect(address,size,PAGE_EXECUTE_READWRITE,&prior))return false;
    std::memcpy(address,bytes,size);DWORD ignored=0;
    const bool ok=VirtualProtect(address,size,prior,&ignored)!=FALSE;
    FlushInstructionCache(GetCurrentProcess(),address,size);return ok;
}
void TestNativeCommandList(uintptr_t base,uint8_t* kimahri,uint8_t* auron,bool active=true) {
    std::array<uint8_t,0xF90> actorBefore{},otherBefore{};
    std::memcpy(actorBefore.data(),kimahri,actorBefore.size());std::memcpy(otherBefore.data(),auron,otherBefore.size());
    uint8_t priorGroup[5]={};std::memcpy(priorGroup,reinterpret_cast<void*>(base+0x3B6BD0),5);
    uint8_t stopOtherGroups[5]={0xE9};const uint32_t displacement=static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&UnrelatedGroupList)-(base+0x3B6BD0+5));
    std::memcpy(stopOtherGroups+1,&displacement,4);
    Expect(Write(reinterpret_cast<void*>(base+0x3B6BD0),stopOtherGroups,5),
           "native list fixture replaces only unrelated summon/special group enumeration");
    std::vector<uint16_t> ring(0x8A88/2,0xFF);auto* ringPointer=ring.data();uint32_t previousRing=0;
    std::memcpy(&previousRing,reinterpret_cast<void*>(base+0x1F10CD8),4);
    Expect(Write(reinterpret_cast<void*>(base+0x1F10CD8),&ringPointer,4),"actual native list builder gets private ring storage");
    std::array<uint8_t,32> learnedBefore{};std::memcpy(learnedBefore.data(),reinterpret_cast<void*>(base+0xD307FC),32);
    const auto tableBefore=commands;
    std::memset(kimahri+0x690,0,40);kimahri[0x6CB]=0;
    const auto build=reinterpret_cast<void(__cdecl*)(int,uint8_t*)>(base+0x39BB70);
    const auto classify=reinterpret_cast<int(__cdecl*)(int,int)>(base+0x49AC10);
    const auto listed=[&](int slot,uint16_t command) {
        const size_t start=(slot*0x478+0x128)/2;
        for(size_t i=0;i<24;++i)if(ring[start+i]==command)return true;
        return false;
    };
    kimahri[0x5BC]=100;build(3,kimahri);
    const auto cachedList=ring;
    kimahri[0x5BC]=200;
    Expect(listed(3,0x3073)&&ring==cachedList&&classify(3,0x3073)==0xFE,
           "Nova built at100 stays visible and becomes usable at200 without rebuilding the cached list");
    for(uint8_t balance:{uint8_t(100),uint8_t(200),uint8_t(180),uint8_t(20),uint8_t(0),uint8_t(200)}) {
        kimahri[0x5BC]=balance;build(3,kimahri);
        Expect(listed(3,0x3073),"learned Nova stays in the actual built/sorted list at every charge");
        Expect(listed(3,0x3068)&&listed(3,0x3069),"learned Jump and cost100 children never disappear when unaffordable");
        Expect(!listed(3,0x3071),"the actual list builder does not grant unlearned Bad Breath");
        Expect(classify(3,0x3073)==(balance==200?0xFE:-2),"listed Nova is grey below200 and selectable at200");
        Expect(classify(3,0x3068)==((balance==200||(active&&balance>=20))?0xFE:-2),
               "list membership and Jump affordability are independent");
        Expect(classify(3,0x3069)==((balance==200||(active&&balance>=100))?0xFE:-2),
               "list membership and cost100 affordability are independent");
        Expect(kimahri[0x5BC]==balance&&kimahri[0x5BD]==200,"building/classifying the list cannot change charge or capacity");
    }
    uint16_t learned=0;std::memcpy(&learned,reinterpret_cast<void*>(base+0xD307FE),2);
    const uint16_t noNova=learned&~uint16_t(1u<<3);
    Write(reinterpret_cast<void*>(base+0xD307FE),&noNova,2);build(3,kimahri);
    Expect(!listed(3,0x3073),"unlearned Nova is absent even at200");
    Write(reinterpret_cast<void*>(base+0xD307FE),&learned,2);build(3,kimahri);
    Expect(listed(3,0x3073),"native rebuilding restores a genuinely learned Nova without fabricating a save flag");
    const uint16_t disabled=1u<<3;std::memcpy(kimahri+0x69E,&disabled,2);build(3,kimahri);
    Expect(listed(3,0x3073)&&classify(3,0x3073)<0,"disabled learned Nova remains listed while native selection restriction wins");
    auron[0x6CB]=0;build(2,auron);
    Expect(!listed(2,0x3073)&&!listed(2,0x3068),"another actor cannot inherit Kimahri's learned Ronso list");
    Expect(commands==tableBefore&&std::memcmp(learnedBefore.data(),reinterpret_cast<void*>(base+0xD307FC),32)==0,
           "native list cases leave the command table and learned save bank unchanged");
    Write(reinterpret_cast<void*>(base+0x1F10CD8),&previousRing,4);
    Write(reinterpret_cast<void*>(base+0x3B6BD0),priorGroup,5);
    std::memcpy(kimahri,actorBefore.data(),actorBefore.size());std::memcpy(auron,otherBefore.data(),otherBefore.size());
}
void TestLeftKeyRoute(uintptr_t base,uint8_t* kimahri,uint8_t* auron,bool active=true) {
    const auto jump=[&](uint32_t rva,uintptr_t destination) {
        uint8_t code[5]={0xE9};const uint32_t displacement=static_cast<uint32_t>(destination-(base+rva+5));
        std::memcpy(code+1,&displacement,4);return Write(reinterpret_cast<void*>(base+rva),code,sizeof(code));
    };
    // Keep the actual UI prologues, LEFT key tests, eligibility routine and
    // command provider. Stop at the menu-open/audio boundary and unrelated input.
    const uint8_t epilogue[]={0x5F,0x5E,0x8B,0xE5,0x5D,0xC3};
    Expect(jump(0x49A2A0,reinterpret_cast<uintptr_t>(&LeftMenuBoundary))&&
           jump(0x486B00,reinterpret_cast<uintptr_t>(&InactiveOtherInput))&&
           jump(0x230EA0,reinterpret_cast<uintptr_t>(&InactiveOtherInput))&&
           Write(reinterpret_cast<void*>(base+0x49CA65),epilogue,sizeof(epilogue))&&
           Write(reinterpret_cast<void*>(base+0x49CF42),epilogue,sizeof(epilogue)),
           "LEFT fixture preserves real input/admission and replaces only external UI/audio boundaries");
    std::vector<uint16_t> ring(0x8A88/2,0xFF);
    auto* ringPointer=ring.data();
    Expect(Write(reinterpret_cast<void*>(base+0x1F10CD8),&ringPointer,4),"real native left-header list uses private ring storage");
    const size_t kimLeft=(3*0x478+0x28)/2,auronLeft=(2*0x478+0x28)/2;
    ring[kimLeft]=0x311A;ring[auronLeft]=0x311A;
    constexpr size_t headerMask=0x690+(282/16)*2;
    uint16_t blockedBefore=0;std::memcpy(&blockedBefore,kimahri+headerMask,2);
    uint16_t unblocked=blockedBefore&~uint16_t(1u<<(282%16));
    std::memcpy(kimahri+headerMask,&unblocked,2);
    // +690 is the native disabled mask, not the learned bank. The earlier
    // availability-only fixture deliberately used this bit; real LEFT rejects it.
    uint16_t presentBefore=0;constexpr size_t presentMask=0x664+(282/16)*2;
    std::memcpy(&presentBefore,kimahri+presentMask,2);
    const uint16_t present=presentBefore|uint16_t(1u<<(282%16));std::memcpy(kimahri+presentMask,&present,2);
    const uint32_t zero=0,padLeft=0x8000,menuOpen=1;
    Write(reinterpret_cast<void*>(base+0xD333E8),&zero,4); // Native full-OD cheat OFF.
    Write(reinterpret_cast<void*>(base+0xF3D6A0),&zero,4);Write(reinterpret_cast<void*>(base+0xF3D6AC),&zero,4);
    Write(reinterpret_cast<void*>(base+0x1FCC08C),&menuOpen,4);
    Write(reinterpret_cast<void*>(base+0x1FCC092),&zero,1);
    const auto press=[&](int slot,uint32_t route) {
        uint8_t widget[0x90]={};const uint16_t id=static_cast<uint16_t>(slot);
        const uintptr_t renderer=base+0x498DB0;
        std::memcpy(widget+8,&id,2);std::memcpy(widget+0x88,&renderer,4);
        Write(reinterpret_cast<void*>(base+0x21D09D4),&padLeft,4);
        leftRequests=0;leftActor=-1;leftCommand=-1;
        reinterpret_cast<void(__cdecl*)(void*)>(base+route)(widget);
        return leftRequests==1&&leftActor==slot&&leftCommand==0xFFFF;
    };
    const auto nativeLeft=reinterpret_cast<int(__cdecl*)(int)>(base+0x38F750);
    const size_t headerCost=20+96*282+38;
    Expect(commands[headerCost]==100,"installed Ronso header retains its real nonzero100 cost");
    kimahri[0x5BC]=20;
    Expect(nativeLeft(3)==0,"unmodified native LEFT predicate requires a full gauge for any nonzero header cost");
    commands[headerCost]=0;
    Expect(nativeLeft(3)==1,"changing only a detached header cost to zero bypasses native full-gauge admission");
    commands[headerCost]=100;
    // Continue past opening LEFT: execute the real header/leaf classification,
    // command resolution/commit and scalar native debit with the same table.
    std::array<uint8_t,40> disabledMasks{};std::memcpy(disabledMasks.data(),kimahri+0x690,disabledMasks.size());
    for(unsigned id:{104u,105u,115u,282u}) {
        uint16_t bits=0;const size_t offset=0x690+(id/16)*2;
        std::memcpy(&bits,kimahri+offset,2);bits&=~uint16_t(1u<<(id%16));std::memcpy(kimahri+offset,&bits,2);
    }
    const size_t children=(3*0x478+0x128)/2;
    ring[children]=0x3068;ring[children+1]=0x3069;ring[children+2]=0x3073;
    Write(reinterpret_cast<void*>(base+0xF3C90C),&zero,4);
    Write(reinterpret_cast<void*>(base+0xF3F0C6),&zero,2);
    Expect(jump(0x3B06C0,reinterpret_cast<uintptr_t>(&InactiveOtherInput))&&
           jump(0x3B0CE0,reinterpret_cast<uintptr_t>(&InactiveOtherInput)),
           "native debit keeps its scalar writes and stops only at post-debit notifications");
    const auto classify=reinterpret_cast<int(__cdecl*)(int,int)>(base+0x49AC10);
    const auto commit=reinterpret_cast<int(__cdecl*)(const void*,int)>(base+0x38ABE0);
    const auto debit=reinterpret_cast<void(__cdecl*)(int)>(base+0x38E5F0);
    for(uint8_t balance:{uint8_t(20),uint8_t(100),uint8_t(180),uint8_t(200)}) {
        kimahri[0x5BC]=balance;const bool affordable=active||balance==200;
        const auto beforeCharge=kimahri[0x5BC];
        Expect(classify(3,0x311A)==(affordable?4:-2),"real Ronso header classification opens category4 below a full gauge");
        Expect(classify(3,0x3068)==(affordable?0xFE:-2),"real Jump classification is selectable at its own20 cost");
        Expect(kimahri[0x5BC]==beforeCharge&&kimahri[0x5BD]==200,"classification does not debit or spoof the pool");
        uint8_t action[32]={};action[0]=3;
        const uint16_t header=0x311A,jumpId=0x3068;
        std::memcpy(action+8,&header,2);std::memcpy(action+10,&jumpId,2);
        kimahri[0x6CC]=0;kimahri[0x6CD]=0;
        Expect(commit(action,0)==(affordable?-1:0),"native confirmation accepts the resolved affordable child after header selection");
        Expect(kimahri[0x6CD]==(affordable?20:0),"native commitment retains the real Jump20 debit, not the UI-only bypass");
        debit(3);
        Expect(kimahri[0x5BC]==(affordable?balance-20:balance)&&kimahri[0x6CD]==0,
               "actual native debit spends Jump20 once and preserves the remainder");
    }
    kimahri[0x5BC]=180;
    Expect(classify(3,0x3073)==-2,"Nova remains unselectable below its real200 cost");
    kimahri[0x5BC]=19;
    Expect(classify(3,0x311A)==-2&&classify(3,0x3068)==-2,"no affordable learned child keeps header and Jump grey");
    kimahri[0x5BC]=200;
    uint8_t novaAction[32]={};novaAction[0]=3;
    const uint16_t header=0x311A,nova=0x3073;
    std::memcpy(novaAction+8,&header,2);std::memcpy(novaAction+10,&nova,2);
    Expect(classify(3,nova)==0xFE&&commit(novaAction,0)==-1&&kimahri[0x6CD]==200,
           "Nova requires and queues its actual200 charge through native confirmation");
    debit(3);
    Expect(kimahri[0x5BC]==0&&kimahri[0x6CD]==0,"native Nova debit spends exactly200");
    // Inject extra MP/status restrictions only into the detached command fixture.
    // The bypass must still execute the native checks after the full-gauge branch.
    auto* jumpRow=commands.data()+20+96*104;
    const uint8_t oldMp=jumpRow[37];uint32_t oldFlags=0,oldActorMp=0;
    std::memcpy(&oldFlags,jumpRow+28,4);std::memcpy(&oldActorMp,kimahri+0x5D4,4);
    const uint32_t magicFlags=oldFlags|0x20000u,enoughMp=15,lowMp=14;
    jumpRow[37]=15;std::memcpy(jumpRow+28,&magicFlags,4);
    kimahri[0x5BC]=active?180:200;
    std::memcpy(kimahri+0x5D4,&lowMp,4);
    Expect(classify(3,0x3068)==-2,"partial-gauge admission never bypasses native MP affordability");
    std::memcpy(kimahri+0x5D4,&enoughMp,4);kimahri[0x609]=1;
    Expect(classify(3,0x3068)==-2,"partial-gauge admission preserves native Silence restriction");
    kimahri[0x609]=0;
    Expect(classify(3,0x3068)==0xFE,"the same affordable row becomes selectable when MP/status allow it");
    uint16_t curse=0x400;std::memcpy(kimahri+0x616,&curse,2);
    Expect(classify(3,0x3068)==-2,"Curse cannot be bypassed by the call-local zero-cost view");
    curse=0;std::memcpy(kimahri+0x616,&curse,2);
    jumpRow[37]=oldMp;std::memcpy(jumpRow+28,&oldFlags,4);std::memcpy(kimahri+0x5D4,&oldActorMp,4);
    kimahri[0x5BC]=180;
    const auto rawCost=reinterpret_cast<int(__cdecl*)(int,const uint8_t*,int)>(base+0x38C750);
    Expect(rawCost(3,jumpRow,0)==-1,"unknown callers retain the native full-gauge gate");
    std::memcpy(kimahri+0x690,disabledMasks.data(),disabledMasks.size());
    for(uint32_t route:{0x49C870u,0x49CD50u}) {
        for(uint8_t balance:{uint8_t(19),uint8_t(20),uint8_t(80),uint8_t(100),uint8_t(199),uint8_t(200)}) {
            kimahri[0x5BC]=balance;
            std::array<uint8_t,0xF90> before{};std::memcpy(before.data(),kimahri,before.size());
            Expect(press(3,route)==(balance==200||(active&&balance>=20)),
                   "actual LEFT key handler follows affordability only while the patch is active");
            Expect(std::memcmp(before.data(),kimahri,before.size())==0,"LEFT dispatch does not change actor charge, maximum or flags");
        }
        kimahri[0x5BC]=20;
        const uint16_t blocked=unblocked|uint16_t(1u<<(282%16));std::memcpy(kimahri+headerMask,&blocked,2);
        Expect(!press(3,route),"native disabled Ronso header cannot gain LEFT admission");
        std::memcpy(kimahri+headerMask,&unblocked,2);
        ring[kimLeft]=0xFF;Expect(!press(3,route),"absent native left header cannot be fabricated");ring[kimLeft]=0x311A;
        ring[kimLeft+1]=0x3068;
        Expect(!press(3,route),"a foreign costed left header prevents widening the shared native mask");ring[kimLeft+1]=0xFF;
        uint16_t learned=0;std::memcpy(&learned,reinterpret_cast<void*>(base+0xD307FC),2);
        const uint16_t withoutJump=learned&~uint16_t(0x100);
        Write(reinterpret_cast<void*>(base+0xD307FC),&withoutJump,2);
        Expect(!press(3,route),"the LEFT handler cannot use an unlearned cheap skill as its admission floor");
        Write(reinterpret_cast<void*>(base+0xD307FC),&learned,2);
        uint16_t cursed=0x400;std::memcpy(kimahri+0x616,&cursed,2);
        Expect(!press(3,route),"Curse also blocks actual LEFT input");cursed=0;std::memcpy(kimahri+0x616,&cursed,2);
        kimahri[0x5BD]=100;Expect(!press(3,route),"unowned capacity cannot gain partial LEFT input");kimahri[0x5BD]=200;
        ring[kimLeft+1]=0x3000;kimahri[0x5BC]=19;
        Expect(press(3,route),"native zero-cost left neighbors remain available below Ronso affordability");ring[kimLeft+1]=0xFF;
        kimahri[0x5BC]=20;
        const uint32_t nativeCheat=1;Write(reinterpret_cast<void*>(base+0xD333E8),&nativeCheat,4);
        Expect(press(3,route)&&kimahri[0x5BC]==200,"existing native full-OD cheat side effects are forwarded exactly once");
        Write(reinterpret_cast<void*>(base+0xD333E8),&zero,4);
        auron[0x5BC]=20;auron[0x5BD]=100;
        Expect(!press(2,route),"another character's partial gauge retains the native LEFT block");
        auron[0x5BC]=100;Expect(press(2,route),"another character's full native gauge still opens LEFT");
    }
    Expect(commands[headerCost]==100&&commands[20+96*115+38]==200,"header and Nova costs remain unchanged after LEFT scenarios");
    std::memcpy(kimahri+headerMask,&blockedBefore,2);std::memcpy(kimahri+presentMask,&presentBefore,2);
    ringPointer=nullptr;Write(reinterpret_cast<void*>(base+0x1F10CD8),&ringPointer,4);
}
void Signatures() {
    for(const auto& span:Evidence::kSpans)for(uintptr_t base:{uintptr_t(0x400000),uintptr_t(0x10000000)}) {
        std::vector<uint8_t> bytes(span.bytes,span.bytes+span.size);
        for(size_t i=0;i<span.count;++i) {
            uint32_t value=0;std::memcpy(&value,bytes.data()+span.relocations[i],4);
            value+=static_cast<uint32_t>(base)-0x400000;std::memcpy(bytes.data()+span.relocations[i],&value,4);
        }
        Expect(Evidence::Matches(span,bytes.data(),bytes.size(),base),"supported native evidence relocates exactly");
        for(size_t i=0;i<bytes.size();++i) {
            bytes[i]^=1;
            Expect(!Evidence::Matches(span,bytes.data(),bytes.size(),base),"every altered native evidence byte is rejected");
            bytes[i]^=1;
        }
    }
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    Signatures();
    HMODULE game=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    HMODULE crt=LoadLibraryW(L"msvcr110.dll");
    Expect(game&&crt,"private supported image and native CRT loaded without starting FFX");
    if(!game||!crt)return 1;
    const uintptr_t base=reinterpret_cast<uintptr_t>(game);
    const auto freadFn=GetProcAddress(crt,"fread"),fwriteFn=GetProcAddress(crt,"fwrite");
    Expect(freadFn&&fwriteFn&&Write(reinterpret_cast<void*>(base+0x70C3F4),&freadFn,4)&&
           Write(reinterpret_cast<void*>(base+0x70C428),&fwriteFn,4),"fixture resolves only the two native save imports");
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));
    PreparedRuntime prepared{};
    Expect(PrepareRuntime(base,true,nullptr,&prepared,root.c_str()),"production preparation accepts exact image and private store");
    Expect(prepared.ioRequired&&prepared.count==5,"pool prepares capacity, ring entry, left input, cost admission and reset; presence stays native");
    if(prepared.count==5) {
        SetImportProtectionForFixture(&FailSecondProtection);
        Expect(!InstallIoImports(),"page-protection restoration failure rejects publication");
        Expect(*reinterpret_cast<FARPROC*>(base+0x70C3F4)==freadFn,
               "a CAS mutation remains owned and is rolled back even when protection restoration fails");
        SetImportProtectionForFixture(nullptr);
        Write(reinterpret_cast<void*>(base+0x70C3F4),&freadFn,4);
        Expect(InstallIoImports(),"FFX-only IO imports publish atomically with ownership checks");
        Expect(RestoreIoImports(),"unpublished IO rollback restores exactly the original imports");
        auto* actors=static_cast<uint8_t*>(VirtualAlloc(nullptr,31*0xF90,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Expect(actors!=nullptr,"private party actor table allocated");
        if(actors) {
            Expect(Write(reinterpret_cast<void*>(base+0xD334CC),&actors,4),"private native actor table pointer published");
            SaveImage saved{};std::ifstream saveFile(argv[2],std::ios::binary);
            Expect(static_cast<bool>(saveFile.read(reinterpret_cast<char*>(saved.data()),saved.size())),"actual autosave copied as learned-mask fixture");
            Expect(Write(reinterpret_cast<void*>(base+0xD307FC),saved.data()+15788,32),"native party command bank seeded from save");
            const auto learned=reinterpret_cast<int(__cdecl*)(int,int)>(base+0x3850E0);
            Expect(learned(3,0x3073)==1&&learned(3,0x3071)==0,
                   "actual encoded learned helper agrees with saved Nova and missing Bad Breath");
            const auto nativeCrc=reinterpret_cast<uint16_t(__cdecl*)(int,const void*)>(base+0x4B1400);
            for(unsigned seed=0;seed<16;++seed) {
                auto sample=saved;sample[1000+seed*37]^=static_cast<uint8_t>(seed*17);
                std::memset(sample.data()+25844,0,4);
                Expect(nativeCrc(0x64B8,sample.data()+64)==SaveChecksum(sample),
                       "portable CRC matches the real native generator, including index255 behavior");
            }
            std::string kernelPath=argv[2];kernelPath=kernelPath.substr(0,kernelPath.find_last_of("/\\")+1)+"command.bin";
            std::ifstream kernel(kernelPath,std::ios::binary);commands.assign(std::istreambuf_iterator<char>(kernel),{});
            Expect(commands.size()==51867,"actual modified command table loaded privately");
            uint8_t jump[5]={0xE9};uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&KernelEntry)-(base+0x390AE0+5));
            std::memcpy(jump+1,&relative,4);
            Expect(Write(reinterpret_cast<void*>(base+0x390AE0),jump,5),"fixture substitutes only the command table provider after profile validation");
            const uint8_t ret=0xC3;
            Expect(Write(reinterpret_cast<void*>(base+0x39B5BE),&ret,1),"fixture returns immediately after the native maximum store");
            const auto initialized=FfxHooks::MinHookBatch::EnsureProcessInitialized();
            Expect(initialized==FfxHooks::MinHookBatch::InitializationResult::Ready,"shared SDK ready for native fixture");
            std::array<uintptr_t,5> addresses{};bool created=true;
            for(size_t i=0;i<prepared.count;++i) {
                const auto& target=prepared.hooks[i];addresses[i]=target.address;
                if(MH_CreateHook(reinterpret_cast<void*>(target.address),target.replacement,target.original)!=MH_OK)created=false;
            }
            Expect(created,"production descriptors create all five owned native hooks");
            *prepared.hooks[4].original=reinterpret_cast<void*>(&ResetProvider);
            const auto enabled=FfxHooks::MinHookBatch::EnableBatch(&FfxHooks::MinHookBatch::ProcessCoordinator(),
                FfxHooks::MinHookBatch::RuntimeBatchIo(),FfxHooks::MinHookBatch::Owner::NovaSuperDamage,addresses.data(),prepared.count);
            Expect(enabled.result==FfxHooks::MinHookBatch::BatchResult::Applied,"native target batch enabled atomically");
            ActivateRuntime();
            auto* kimahri=actors+3*0xF90;auto* auron=actors+2*0xF90;
            uint16_t id=3;std::memcpy(kimahri+0xE,&id,2);id=2;std::memcpy(auron+0xE,&id,2);
            uint32_t hp=1000;std::memcpy(kimahri+0x5D0,&hp,4);std::memcpy(auron+0x5D0,&hp,4);
            kimahri[0x5BC]=100;auron[0x5BC]=100;
            uint8_t entry[148]={};entry[0x3A]=100;
            InvokeMaximum(0,3,kimahri,entry,reinterpret_cast<void*>(base+0x39B5B7));
            Expect(kimahri[0x5BD]==200&&kimahri[0x5BC]==100,"actual native gateway raises only capacity, not current charge");
            MaximumCall call{kimahri,entry,reinterpret_cast<void*>(base+0x39B5B7)};
            HANDLE initializer=CreateThread(nullptr,0,&NativeInitializationThread,&call,0,nullptr);
            Expect(initializer!=nullptr,"separate native initialization owner fixture started");
            if(initializer){WaitForSingleObject(initializer,5000);CloseHandle(initializer);}
            Expect(kimahri[0x5BD]==200&&kimahri[0x5BC]==100,
                   "each admitted native initialization owns its thread; an earlier bootstrap thread cannot lock it out");
            InvokeMaximum(0,3,kimahri,entry,reinterpret_cast<void*>(base+0x39B5B7));
            InvokeMaximum(0,2,auron,entry,reinterpret_cast<void*>(base+0x39B5B7));
            Expect(auron[0x5BD]==100&&auron[0x5BC]==100,"Auron2 remains entirely native");
            TestNativeCommandList(base,kimahri,auron);
            std::memcpy(kimahri+0x670,reinterpret_cast<void*>(base+0xD307FC),32);
            Expect(Write(reinterpret_cast<void*>(base+0x39BC83),&ret,1)&&
                   Write(reinterpret_cast<void*>(base+0x39BF17),&ret,1),"fixture returns after each exact native presence caller");
            const auto has=[&](uint8_t slot,int16_t command) {
                return InvokeAvailability(slot,command,reinterpret_cast<void*>(base+(command==282?0x39BF0B:0x39BC79)));
            };
            for(unsigned command:{104u,105u,113u,115u,282u}) {
                uint16_t mask=0;std::memcpy(&mask,kimahri+0x690+(command/16)*2,2);
                mask|=static_cast<uint16_t>(1u<<(command&15));
                std::memcpy(kimahri+0x690+(command/16)*2,&mask,2);
            }
            kimahri[0x5BC]=20;
            Expect((has(3,104)&1)!=0,"native presence includes learned Jump at20 of200");
            Expect((has(3,105)&1)!=0,"native presence retains learned cost100 commands below their usable charge");
            Expect((has(3,282)&1)!=0,"native learned header remains present independently of its selection gate");
            // Exercise the earlier native input branch and its menu-word producer,
            // not only the child/header query reached after entering Overdrive.
            uint8_t ringJump[5]={0xE9};relative=static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(&RingBuilderBoundary)-(base+0x3ACEC0+5));
            std::memcpy(ringJump+1,&relative,4);
            Expect(Write(reinterpret_cast<void*>(base+0x3ACEC0),ringJump,5)&&
                   Write(reinterpret_cast<void*>(base+0x392BF0),&ret,1)&&
                   Write(reinterpret_cast<void*>(base+0x39B6C5),&ret,1),
                   "entry fixture stops at the renderer boundary after real native decisions");
            const auto enter=[&](int slot) {
                ringBuilds=0;ringKind=-1;
                InvokeOverdriveEntry(slot,reinterpret_cast<void*>(base+0x392BCD));
                return ringBuilds==1&&ringKind==12;
            };
            const auto refresh=[&](int slot,uint8_t* actor) {
                InvokeOverdriveRefresh(slot,actor,reinterpret_cast<void*>(base+0x39B6A9));
                uint16_t word=0;std::memcpy(&word,actor+0x6C8,2);return word;
            };
            kimahri[0x590]=0;kimahri[0x5BC]=20;
            Expect(enter(3),"left-entry native branch builds Ronso ring at20/200 with full-gauge bit clear");
            Expect(refresh(3,kimahri)==0,"native refresh removes only the partial-gauge menu lock at20/200");
            Expect(kimahri[0x590]==0&&kimahri[0x5BC]==20&&kimahri[0x5BD]==200,
                   "entry admission does not spoof full gauge, charge, or capacity");
            kimahri[0x5BC]=19;
            Expect(!enter(3)&&refresh(3,kimahri)==0x3021,"entry stays closed below the cheapest learned cost");
            auron[0x590]=0;
            TestLeftKeyRoute(base,kimahri,auron);
            Expect(!enter(2)&&refresh(2,auron)==0x3021,"other actors keep native full-gauge admission");
            const auto nativeReady=reinterpret_cast<int(__cdecl*)(int)>(base+0x39AF70);
            kimahri[0x5BC]=100;
            Expect(nativeReady(3)==0,"non-UI callers retain the real full-gauge readiness bit");
            for(uint8_t balance:{uint8_t(20),uint8_t(80),uint8_t(100),uint8_t(180),uint8_t(199),uint8_t(200)}) {
                kimahri[0x5BC]=balance;
                std::array<uint8_t,0xF90> before{};std::memcpy(before.data(),kimahri,before.size());
                Expect(enter(3),"native input constructs the Ronso ring at every affordable partial/full balance");
                Expect(std::memcmp(before.data(),kimahri,before.size())==0,
                       "menu admission is read-only over the complete actor record");
            }
            kimahri[0x5BC]=20;
            uint16_t learnedBank=0;std::memcpy(&learnedBank,reinterpret_cast<void*>(base+0xD307FC),2);
            const uint16_t withoutJump=learnedBank&~uint16_t(0x100);
            Write(reinterpret_cast<void*>(base+0xD307FC),&withoutJump,2);
            Expect(!enter(3),"an unlearned cheap Jump cannot unlock the entry for unaffordable learned skills");
            Write(reinterpret_cast<void*>(base+0xD307FC),&learnedBank,2);
            kimahri[0xDCC]=1;Expect(!enter(3),"native actor input block prevents partial entry");kimahri[0xDCC]=0;
            kimahri[0xDCE]=1;Expect(!enter(3),"native actor secondary block prevents partial entry");kimahri[0xDCE]=0;
            uint16_t curse=0x400;std::memcpy(kimahri+0x616,&curse,2);
            Expect(!enter(3),"Curse retains native Overdrive entry restrictions");
            curse=0;std::memcpy(kimahri+0x616,&curse,2);
            uint32_t dead=0;std::memcpy(kimahri+0x5D0,&dead,4);
            Expect(!enter(3),"a dead actor does not gain partial entry");std::memcpy(kimahri+0x5D0,&hp,4);
            kimahri[0x5BD]=100;Expect(!enter(3),"unowned native capacity does not borrow pool admission");kimahri[0x5BD]=200;
            uint8_t noCost=1;Write(reinterpret_cast<void*>(base+0xD2A90C),&noCost,1);
            Expect(!enter(3),"native no-cost mode forwards its real readiness instead of adding a new override");
            noCost=0;Write(reinterpret_cast<void*>(base+0xD2A90C),&noCost,1);
            ringBuilds=0;ringKind=-1;
            HANDLE foreignMenu=CreateThread(nullptr,0,&ForeignMenuThread,reinterpret_cast<void*>(base+0x392BCD),0,nullptr);
            Expect(foreignMenu!=nullptr,"foreign-thread menu fixture started");
            if(foreignMenu){WaitForSingleObject(foreignMenu,5000);CloseHandle(foreignMenu);}
            Expect(ringBuilds==0,"a foreign thread cannot use the native initialization owner's partial entry");
            auron[0x590]=4;Expect(enter(2)&&refresh(2,auron)==0,"another actor's full native gauge still opens normally");
            auron[0x590]=0;
            kimahri[0x5BC]=199;Expect((has(3,115)&1)!=0,"Nova199 remains present for grey classification");
            kimahri[0x5BC]=200;Expect((has(3,115)&1)!=0,"Nova200 presence does not depend on a new learned flag");
            Expect((has(3,113)&1)==0,"Bad Breath absent from the actual save is never granted");
            uint8_t eventJump[5]={0xE9};relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FullEvent)-(base+0x385AC0+5));
            std::memcpy(eventJump+1,&relative,4);Write(reinterpret_cast<void*>(base+0x385AC0),eventJump,5);
            const auto gain=reinterpret_cast<int(__cdecl*)(int,void*,int)>(base+0x3B15A0);
            kimahri[0x5BC]=80;gain(3,kimahri,20);
            Expect(kimahri[0x5BC]==100&&kimahri[0x5BD]==200,"native gain continues past an affordable partial gauge");
            kimahri[0x5BC]=190;gain(3,kimahri,50);
            Expect(kimahri[0x5BC]==200&&fullEvents==1,"native gain clamps at fixed200 and emits native full event");
            kimahri[0x5BC]=100;uint16_t modifier=2;std::memcpy(kimahri+0x6BE,&modifier,2);gain(3,kimahri,20);
            Expect(kimahri[0x5BC]==160,"native triple gain modifier remains intact");
            modifier=1;std::memcpy(kimahri+0x6BE,&modifier,2);kimahri[0x5BC]=100;gain(3,kimahri,20);
            Expect(kimahri[0x5BC]==140,"native double gain modifier remains intact");
            modifier=0;std::memcpy(kimahri+0x6BE,&modifier,2);uint16_t cursed=0x400;
            std::memcpy(kimahri+0x616,&cursed,2);kimahri[0x5BC]=100;gain(3,kimahri,20);
            Expect(kimahri[0x5BC]==100&&(has(3,104)&1)!=0,"native Curse blocks gain without erasing learned presence");
            cursed=0;std::memcpy(kimahri+0x616,&cursed,2);
            auron[0x5BC]=80;gain(2,auron,50);Expect(auron[0x5BC]==100&&auron[0x5BD]==100,"another character still gains only to native100");
            Write(reinterpret_cast<void*>(base+0x38F20E),&ret,1);
            auron[0x5BC]=50;kimahri[0x5BC]=150;
            InvokeTransfer(auron,kimahri,reinterpret_cast<void*>(base+0x38F1DB));
            Expect(auron[0x5BC]==0&&kimahri[0x5BC]==200,"native Entrust fills Kimahri without a max=current pin");
            auron[0x5BC]=80;kimahri[0x5BC]=180;
            InvokeTransfer(auron,kimahri,reinterpret_cast<void*>(base+0x38F1DB));
            Expect(auron[0x5BC]==0&&kimahri[0x5BC]==200,"Entrust overflow keeps native donor and clamp semantics");
            kimahri[0x5BC]=80;auron[0x5BC]=50;
            InvokeTransfer(kimahri,auron,reinterpret_cast<void*>(base+0x38F1DB));
            Expect(kimahri[0x5BC]==0&&auron[0x5BC]==100,"Entrust in the reverse direction preserves the other maximum");
            kimahri[0x5BC]=20;
            RequestStop();Expect((has(3,115)&1)!=0,"stop preserves the unmodified native presence mask");
            Expect(!enter(3)&&refresh(3,kimahri)==0x3021,"stop closes partial menu entry without actor restoration writes");
            const uint8_t nativeTest=0xA8,nativeAnd=0x83;
            Write(reinterpret_cast<void*>(base+0x39BC83),&nativeTest,1);
            Write(reinterpret_cast<void*>(base+0x39BF17),&nativeAnd,1);
            TestNativeCommandList(base,kimahri,auron,false);
            Write(reinterpret_cast<void*>(base+0x39BC83),&ret,1);
            Write(reinterpret_cast<void*>(base+0x39BF17),&ret,1);
            TestLeftKeyRoute(base,kimahri,auron,false);
            ActivateRuntime();Expect(!enter(3),"late publication cannot reopen stopped pool");
            const auto neutral=FfxHooks::MinHookBatch::NeutralizeBatch(&FfxHooks::MinHookBatch::ProcessCoordinator(),
                FfxHooks::MinHookBatch::RuntimeBatchIo(),FfxHooks::MinHookBatch::Owner::NovaSuperDamage,addresses.data(),prepared.count);
            Expect(neutral.neutralized,"fixture retirement restores the owned target batch");
            for(auto address:addresses)MH_RemoveHook(reinterpret_cast<void*>(address));
            VirtualFree(actors,0,MEM_RELEASE);
        }
    }
    DiscardUnpublishedRuntime();FreeLibrary(game);FreeLibrary(crt);
    std::printf("RonsoPoolNativeRt1: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
