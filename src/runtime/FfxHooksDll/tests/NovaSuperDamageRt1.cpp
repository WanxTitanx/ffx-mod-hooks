// Jarvis-HOOK: isolated x86 execution of the production Nova stub and native clamp graph.
#define NOMINMAX
#include "../hooks/NovaSuperDamageHook.cpp"
#include "../hooks/MinHookBatchCoordinator.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolStore.h"
#include <MinHook.h>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0,failures=0,logs=0;
void Expect(bool ok,const char* why) {
    ++checks;
    if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}
}
__declspec(align(16)) uint32_t xmmSeed[4]={0x12345678u,0xABCDEF01u,0x31415926u,0x76543210u};
struct Result {int32_t damage=0x12345678;uint32_t ecx=0,edx=0;float fpu=0;uint32_t xmm[4]={};};

__declspec(naked) void __cdecl Invoke(int, int, int, Result*, void*, uint32_t, int) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,80h
        push ebx
        push esi
        push edi
        mov eax,[ebp+0Ch]
        mov ebx,[ebp+10h]
        mov esi,[ebp+14h]
        mov edi,ebx
        neg edi
        mov ecx,[ebp+20h]
        mov [ebp-78h],ecx
        mov ecx,13572468h
        mov edx,24681357h
        fld1
        movups xmm0,xmmSeed
        call dword ptr [ebp+18h]
        mov [esi+4],ecx
        mov [esi+8],edx
        fstp dword ptr [esi+12]
        movups [esi+16],xmm0
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
bool GuardedInvoke(int actor,int damage,int cap,Result* out,void* code,uint32_t cmd,int remaining) {
    __try {Invoke(actor,damage,cap,out,code,cmd,remaining);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void ClobberingLogger(const char*) {
    ++logs;
    __asm { xor ecx,ecx }
    __asm { xor edx,edx }
    __asm { pxor xmm0,xmm0 }
    __asm { fninit }
}
void TestGraph(bool bypass,bool trace) {
    using namespace FfxHooks;
    constexpr uint8_t original[]={
        0x3B,0xC7,0x7D,0x04,0x8B,0xC7,0xEB,0x06,
        0x3B,0xC3,0x7E,0x02,0x8B,0xC3,0x89,0x06,0xC3
    }; // Exact VA 0x0078EDCB..0x0078EDDA; RET replaces only the later continuation.
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Expect(image!=nullptr,"fixture image allocated");
    if(!image)return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(image);
    std::memcpy(image+0x38EDCB,original,sizeof(original));
    uintptr_t patch=0,resume=0;
    uint8_t saved[kPatchLen]={};
    Expect(ResolveClampPatchSite(base,&patch,&resume,saved),"production resolver accepts exact native clamp graph");
    uint8_t* stub=nullptr;size_t length=0;
    g_logFn=ClobberingLogger;g_hitLogCount=0;logs=0;
    InterlockedExchange(&g_admission,1);
    Expect(BuildStub(resume,bypass,trace,&stub,&length),"production native stub builds");
    if(!stub){VirtualFree(image,0,MEM_RELEASE);return;}
    DWORD prior=0;
    Expect(VirtualProtect(image+0x38E000,0x1000,PAGE_EXECUTE_READ,&prior)!=FALSE,"fixture code sealed");
    void* trampoline=nullptr;
    Expect(MH_CreateHook(reinterpret_cast<void*>(patch),stub,&trampoline)==MH_OK,"real MinHook creates fixture detour");
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::NovaSuperDamage,&patch,1);
    Expect(report.result==MinHookBatch::BatchResult::Applied,"coordinated fixture detour enabled");
    struct Case {int actor;uint32_t command;int remaining,damage,cap,want;};
    const Case cases[]={
        {3,0x3073,3,150000,99999,bypass?150000:99999},
        {3,0x3073,3,246810,99999,bypass?246810:99999},
        {3,0x3073,3,1200,99999,1200},
        {3,0x3073,3,-200000,99999,-99999},
        {3,0x3073,3,-150,99999,-150},
        {4,0x3073,3,150000,99999,99999},
        {16,0x3073,3,150000,99999,99999},
        {3,0x3041,3,150000,99999,99999},
        {3,0x3073,2,150000,99999,99999},
        {3,0x3073,1,150000,99999,99999},
        {3,0x13073,3,150000,99999,99999},
        {3,0x3073,3,9999,9999,9999},
    };
    for(const auto& c:cases) {
        Result result{};
        const bool ran=GuardedInvoke(c.actor,c.damage,c.cap,&result,image+0x38EDCB,c.command,c.remaining);
        Expect(ran&&result.damage==c.want,"only Kimahri Nova HP bypasses upper cap; lower-floor and other paths remain vanilla");
        if(ran)Expect(result.ecx==0x13572468u&&result.edx==0x24681357u&&result.fpu==1.0f&&
            std::memcmp(result.xmm,xmmSeed,sizeof(xmmSeed))==0,"stub and logging preserve native register/FPU/SIMD context");
    }
    if(trace)Expect(logs>0,"diagnostics reach the logger with correct relocated targets");
    const auto off=MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::NovaSuperDamage,&patch,1);
    Expect(off.neutralized,"fixture detour disabled");
    Expect(std::memcmp(image+0x38EDCB,original,sizeof(original))==0,"OFF restores the exact native graph");
    Result vanilla{};
    Expect(GuardedInvoke(3,150000,99999,&vanilla,image+0x38EDCB,0x3073,3)&&vanilla.damage==99999,
           "disabled fixture returns to native damage cap");
    // Fixture calls have joined. Production retains published code for process lifetime.
    Expect(MH_RemoveHook(reinterpret_cast<void*>(patch))==MH_OK,"private fixture removed after quiescence");
    VirtualFree(stub,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);g_logFn=nullptr;
}
}
void TestProductionLifecycle(const char* fixture,int mode) {
    using namespace FfxHooks;
    const bool bypass=(mode&1)!=0,ronso=(mode&2)!=0,trace=(mode&4)!=0,compatibility=(mode&8)!=0;
    const bool damage=bypass||trace,io=ronso||compatibility,enabled=damage||io;
    if(compatibility) {
        wchar_t path[MAX_PATH]={};
        Expect(GetModuleFileNameW(nullptr,path,MAX_PATH)!=0,"private fixture executable path available");
        const std::wstring full(path),parent=full.substr(0,full.find_last_of(L"/\\"))+L"\\config";
        Expect(CreateDirectoryW(parent.c_str(),nullptr)!=FALSE,"private fixture config directory created");
        RonsoPool::OwnerStore store;
        Expect(store.Initialize(parent+L"\\ronso-pool-v1",true),"private compatibility store initialized");
        RonsoPool::SaveImage saved{};
        saved[RonsoPool::kSaveMaximum]=200;saved[RonsoPool::kSaveCharge]=150;RonsoPool::SealSave(saved);
        const RonsoPool::SavedOwner owner{100,150,200};
        Expect(store.Write(L"C:\\RonsoGateFixture\\ffx_000",saved,owner),"valid metadata enables compatibility discovery only");
    }
    Expect(RonsoPool::HasPersistentOwnership()==compatibility,"each gate scenario starts with isolated metadata");
    // Map the supported executable without resolving imports or running its entry
    // point/TLS initialization. This remains the standalone fixture process.
    HMODULE module=LoadLibraryExA(fixture,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Expect(module!=nullptr,"supported executable maps as data/code without starting the game");
    if(!module)return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(module);
    HMODULE crt=LoadLibraryW(L"msvcr110.dll");
    Expect(crt!=nullptr,"exact game CRT available in the combined fixture");
    if(!crt){FreeLibrary(module);return;}
    const auto nativeRead=GetProcAddress(crt,"fread"),nativeWrite=GetProcAddress(crt,"fwrite");
    DWORD importPrior=0,ignored=0;
    VirtualProtect(reinterpret_cast<void*>(base+0x70C3F4),0x38,PAGE_READWRITE,&importPrior);
    std::memcpy(reinterpret_cast<void*>(base+0x70C3F4),&nativeRead,4);
    std::memcpy(reinterpret_cast<void*>(base+0x70C428),&nativeWrite,4);
    VirtualProtect(reinterpret_cast<void*>(base+0x70C3F4),0x38,importPrior,&ignored);
    Expect(ValidateProfile(base),"production profile accepts the exact supported image");
    const uintptr_t poolTargets[]={base+0x39AD40,base+0x39B5B7,base+0x39AF70,base+0x38F750,base+0x38C750,base+0x386BC0};
    std::array<std::array<uint8_t,16>,6> poolBefore{};
    for(size_t i=0;i<poolBefore.size();++i)std::memcpy(poolBefore[i].data(),reinterpret_cast<void*>(poolTargets[i]),16);
    uint8_t* continuation=reinterpret_cast<uint8_t*>(base+RVA_FFX_BATTLE_DAMAGE_POST_WRITEBACK);
    const uint8_t original=*continuation;
    DWORD prior=0;
    Expect(VirtualProtect(continuation,1,PAGE_EXECUTE_READWRITE,&prior)!=FALSE,"private fixture continuation becomes writable");
    *continuation=0xC3;VirtualProtect(continuation,1,prior,&prior);FlushInstructionCache(GetCurrentProcess(),continuation,1);
    InterlockedExchange(&g_admission,0);
    const auto installed=InstallNovaSuperDamageHook(base,bypass,trace,ronso,ClobberingLogger);
    Expect(installed.ok==enabled&&IsNovaSuperDamageHookInstalled()==damage,
           "independent Nova and Ronso gates install exactly their requested capabilities");
    Expect(g_targetCount==(damage?1u:0u)+(ronso?5u:(compatibility?1u:0u)),
           "target ownership matches separate damage, Ronso and compatibility requests");
    for(size_t i=0;i<poolBefore.size();++i) {
        const bool changed=std::memcmp(poolBefore[i].data(),reinterpret_cast<void*>(poolTargets[i]),16)!=0;
        Expect(changed==(i==0?false:(i==5?io:ronso)),"native presence remains unpatched; Ronso owns capacity/entry/left/cost and compatibility only reset");
    }
    Expect((*reinterpret_cast<FARPROC*>(base+0x70C3F4)!=nativeRead)==io&&
           (*reinterpret_cast<FARPROC*>(base+0x70C428)!=nativeWrite)==io,
           "save imports depend on Ronso or owned metadata, never Nova alone");
    Result out{};
    Expect(GuardedInvoke(3,150000,99999,&out,reinterpret_cast<void*>(base+0x38EDCB),0x3073,3)&&
           out.damage==(bypass?150000:99999),"damage bypass depends only on Nova, independently of pool and logging");
    if(installed.ok) {
        RequestNovaSuperDamageStop();
        out={};
        Expect(!IsNovaSuperDamageHookInstalled()&&
               GuardedInvoke(3,150000,99999,&out,reinterpret_cast<void*>(base+0x38EDCB),0x3073,3)&&out.damage==99999,
               "stop immediately closes future bypass admission while forwarding vanilla");
        Expect(!InstallNovaSuperDamageHook(base,bypass,trace,ronso,ClobberingLogger).ok,
               "a late install cannot reopen stopped admission");
        Expect(RemoveNovaSuperDamageHook(ClobberingLogger),"production retirement disables only its own target");
        Expect(*reinterpret_cast<FARPROC*>(base+0x70C3F4)==nativeRead&&
               *reinterpret_cast<FARPROC*>(base+0x70C428)==nativeWrite,
               "combined retirement restores both native save imports");
        MEMORY_BASIC_INFORMATION info{};
        if(damage)Expect(VirtualQuery(g_stub,&info,sizeof(info))!=0&&info.State==MEM_COMMIT,
               "published native stub remains allocated after retirement");
        Expect(RemoveNovaSuperDamageHook(ClobberingLogger),"retirement is idempotent");
        Expect(std::memcmp(reinterpret_cast<void*>(base+0x38EDCB),kClampGraph,sizeof(kClampGraph))==0,
               "production retirement restores the exact lower/upper clamp graph");
        for(size_t i=0;i<poolBefore.size();++i)Expect(std::memcmp(poolBefore[i].data(),reinterpret_cast<void*>(poolTargets[i]),16)==0,
               "retirement restores exact native capacity, availability, entry, left input, cost and reset bytes");
        // Explicit fixture-only cleanup after all calls joined; production never frees this storage.
        while(g_targetCount)Expect(MH_RemoveHook(reinterpret_cast<void*>(g_targets[--g_targetCount]))==MH_OK,
               "quiescent test releases each retained SDK record");
        if(g_stub)VirtualFree(g_stub,0,MEM_RELEASE);g_stub=nullptr;g_created=false;
    }
    VirtualProtect(continuation,1,PAGE_EXECUTE_READWRITE,&prior);
    *continuation=original;VirtualProtect(continuation,1,prior,&prior);
    FreeLibrary(module);
    FreeLibrary(crt);
}
int main(int argc,char** argv) {
    using namespace FfxHooks;
    Expect(MinHookBatch::EnsureProcessInitialized()==MinHookBatch::InitializationResult::Ready,"shared MinHook ready");
    Expect(argc==3,"production fixture requires the exact supported executable path and isolated gate mode");
    const int mode=argc==3?std::atoi(argv[2]):-1;
    Expect(mode>=0&&mode<=15,"gate scenario fits the four explicit test flags");
    if(mode==3){TestGraph(true,false);TestGraph(true,true);TestGraph(false,true);}
    if(argc==3&&mode>=0&&mode<=15)TestProductionLifecycle(argv[1],mode);
    if(failures){std::fprintf(stderr,"NovaSuperDamageRt1: FAIL (%d/%d failed)\n",failures,checks);return 1;}
    std::printf("NovaSuperDamageRt1: PASS mode=%d (%d checks)\n",mode,checks);return 0;
}
