#include "NovaSuperDamageHook.h"
#include "F8RuntimeCore.h"
#include "MinHookBatchCoordinator.h"
#include "RonsoPoolRuntime.h"
#include "NativeSaveEvents.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <vector>

namespace FfxHooks {
namespace {

// Keep the native lower-floor branch's destination at WRITEBACK intact.
constexpr size_t kPatchLen=6;
constexpr uint8_t kClampGraph[]={
    0x3B,0xC7,0x7D,0x04,0x8B,0xC7,0xEB,0x06,
    0x3B,0xC3,0x7E,0x02,0x8B,0xC3,0x89,0x06
};
constexpr uint8_t kFramePrefix[]={0x55,0x8B,0xEC,0x81,0xEC,0xB4,0,0,0};
constexpr uint8_t kHpLoopSetup[]={0xC7,0x45,0x88,0x03,0,0,0,0x8B,0x04,0x10};
static uint8_t* g_stub=nullptr;
static size_t g_stubLen=0;
static uintptr_t g_patchVa=0,g_resumeVa=0;
static void* g_original=nullptr;
static bool g_created=false,g_applyAttempted=false;
static std::atomic<bool> g_damageCreated{false};
static std::array<uintptr_t,6> g_targets{};
static size_t g_targetCount=0;
static volatile LONG g_installed=0,g_attempted=0;
// 2 is absorbing: a late installer cannot reopen admission after stop.
static volatile LONG g_admission=0;
static std::atomic<NovaSuperDamageLogFn> g_logFn{nullptr};
static volatile LONG g_hitLogCount=0;
static SRWLOCK g_lifecycle=SRWLOCK_INIT;

struct LifecycleScope {
    bool held=TryAcquireSRWLockExclusive(&g_lifecycle)!=FALSE;
    ~LifecycleScope(){if(held)ReleaseSRWLockExclusive(&g_lifecycle);}
};
static void HookLog(const char* fmt,...) {
    const auto log=g_logFn.load(std::memory_order_acquire);
    if(!log)return;
    char text[512]={};va_list args;va_start(args,fmt);
    vsnprintf(text,sizeof(text),fmt,args);va_end(args);log(text);
}
static bool GuardedCopy(void* out,const void* address,size_t length) noexcept {
    __try {std::memcpy(out,address,length);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool MappedImageRange(uintptr_t address,size_t length,uintptr_t base,bool code) {
    if(!address||!length||address>UINT32_MAX-length)return false;
    const uintptr_t end=address+length;
    while(address<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&mbi,sizeof(mbi))||
           mbi.State!=MEM_COMMIT||mbi.Type!=MEM_IMAGE||
           reinterpret_cast<uintptr_t>(mbi.AllocationBase)!=base||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD p=mbi.Protect&0xFFu;
        const bool executable=p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        if(!(executable||p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY)||(code&&!executable))return false;
        address=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
    }
    return true;
}
static bool ValidateProfile(uintptr_t base) {
    uint8_t header[0x1000]={};F8Runtime::ExecutableIdentity identity{};
    if(!MappedImageRange(base,sizeof(header),base,false)||
       !GuardedCopy(header,reinterpret_cast<void*>(base),sizeof(header))||
       F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
       !F8Runtime::IsSupportedExecutable(identity)||base>UINT32_MAX-identity.sizeOfImage)return false;
    const struct {uint32_t rva;const uint8_t* bytes;size_t size;} proofs[]={
        {RVA_FFX_BATTLE_COMPUTE_HIT_DAMAGE,kFramePrefix,sizeof(kFramePrefix)},
        {0x0038EDC1u,kHpLoopSetup,sizeof(kHpLoopSetup)},
        {0x0038EDCBu,kClampGraph,sizeof(kClampGraph)}
    };
    for(const auto& p:proofs) {
        uint8_t actual[32]={};
        if(F8Runtime::ValidateImageRange(p.rva,p.size,identity.sizeOfImage)!=F8Runtime::ProfileResult::Supported||
           !MappedImageRange(base+p.rva,p.size,base,true)||
           !GuardedCopy(actual,reinterpret_cast<void*>(base+p.rva),p.size)||
           std::memcmp(actual,p.bytes,p.size)!=0)return false;
    }
    return true;
}

extern "C" void __cdecl NovaSuperDamage_LogPrecClamp(int32_t command,int32_t damage,int32_t cap,int32_t bypassed) {
    if(InterlockedCompareExchange(&g_admission,0,0)!=1)return;
    const auto log=g_logFn.load(std::memory_order_acquire);
    if(!log||InterlockedIncrement(&g_hitLogCount)>128)return;
    char line[192]={};
    std::snprintf(line,sizeof(line),"[ffx-hooks] NovaClamp cmd=0x%04X damage_pre=%d cap=%d bypass=%d",
                  static_cast<unsigned>(command),damage,cap,bypassed);log(line);
}
static void PatchRel32(std::vector<uint8_t>& bytes,size_t offset,int32_t value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
static void PatchU32(std::vector<uint8_t>& bytes,size_t offset,uint32_t value) {
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
static bool BuildStub(uintptr_t resumeVa,bool bypass,bool logHits,uint8_t** outStub,size_t* outLen) {
    if(!resumeVa||!outStub||!outLen||(!bypass&&!logHits))return false;
    *outStub=nullptr;*outLen=0;
    std::vector<uint8_t> bytes;
    struct AbsoluteFixup {size_t displacement;uintptr_t destination;};
    std::vector<AbsoluteFixup> absolute;
    std::vector<size_t> toVanilla;
    const auto emit=[&](std::initializer_list<uint8_t> input){bytes.insert(bytes.end(),input);};
    const auto jump=[&](uint8_t opcode) {
        emit({0x0F,opcode});const size_t at=bytes.size();emit({0,0,0,0});return at;
    };
    const auto resume=[&]() {
        emit({0xE9});const size_t at=bytes.size();emit({0,0,0,0});absolute.push_back({at,resumeVa});
    };
    const auto log=[&](uint8_t bypassed) {
        // A C logger may clobber every volatile register and x87/SSE state.
        // Save all of it on a bounded, 16-byte-aligned private stack block.
        emit({0x9C,0x60,0x8B,0xD4}); // pushfd; pushad; mov edx,esp
        emit({0x81,0xEC,0x20,0x02,0,0,0x83,0xE4,0xF0});
        emit({0x89,0x94,0x24,0x00,0x02,0,0,0x0F,0xAE,0x04,0x24});
        emit({0x6A,bypassed,0x53,0x50,0xFF,0x75,0x1C,0xE8});
        const size_t at=bytes.size();emit({0,0,0,0});
        absolute.push_back({at,reinterpret_cast<uintptr_t>(&NovaSuperDamage_LogPrecClamp)});
        emit({0x83,0xC4,0x10,0x0F,0xAE,0x0C,0x24});
        emit({0x8B,0xA4,0x24,0x00,0x02,0,0,0x61,0x9D});
    };

    emit({0x83,0x3D});const size_t admission=bytes.size();emit({0,0,0,0,0x01});
    PatchU32(bytes,admission,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_admission)));
    const size_t stopped=jump(0x85);
    emit({0x81,0x7D,0x1C,0x73,0x30,0,0});toVanilla.push_back(jump(0x85)); // encoded Nova
    emit({0x83,0x7D,0x08,0x03});toVanilla.push_back(jump(0x85)); // Kimahri actor slot
    emit({0x83,0x7D,0x88,0x03});toVanilla.push_back(jump(0x85)); // first (HP) component
    if(logHits)log(bypass?1u:0u);
    if(bypass)resume();
    emit({0xE9});const size_t novaClamp=bytes.size();emit({0,0,0,0});
    const size_t vanilla=bytes.size();
    if(logHits&&bypass)log(0);
    const size_t clamp=bytes.size();
    emit({0x3B,0xC3,0x7E,0x02,0x8B,0xC3}); // exact vanilla upper clamp
    resume(); // native MOV [ESI],EAX and every incoming edge remain in place
    for(size_t at:toVanilla)PatchRel32(bytes,at,static_cast<int32_t>(vanilla-(at+4)));
    PatchRel32(bytes,stopped,static_cast<int32_t>(clamp-(stopped+4)));
    PatchRel32(bytes,novaClamp,static_cast<int32_t>(clamp-(novaClamp+4)));

    auto* stub=static_cast<uint8_t*>(VirtualAlloc(nullptr,bytes.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!stub)return false;
    for(const auto& fixup:absolute)PatchRel32(bytes,fixup.displacement,
        static_cast<int32_t>(fixup.destination-(reinterpret_cast<uintptr_t>(stub)+fixup.displacement+4)));
    std::memcpy(stub,bytes.data(),bytes.size());DWORD prior=0;
    if(!VirtualProtect(stub,bytes.size(),PAGE_EXECUTE_READ,&prior)||
       !FlushInstructionCache(GetCurrentProcess(),stub,bytes.size())) {
        VirtualFree(stub,0,MEM_RELEASE);return false;
    }
    *outStub=stub;*outLen=bytes.size();return true;
}
static bool ResolveClampPatchSite(uintptr_t base,uintptr_t* patch,uintptr_t* resume,uint8_t saved[kPatchLen]) {
    if(!base||!patch||!resume||!saved||base>UINT32_MAX-0x0038EDDBu)return false;
    uint8_t actual[sizeof(kClampGraph)]={};
    if(!GuardedCopy(actual,reinterpret_cast<void*>(base+0x0038EDCBu),sizeof(actual))||
       std::memcmp(actual,kClampGraph,sizeof(actual))!=0)return false;
    *patch=base+RVA_FFX_BATTLE_DAMAGE_CAP_CLAMP_CMP;
    *resume=base+RVA_FFX_BATTLE_DAMAGE_WRITEBACK;
    std::memcpy(saved,actual+8,kPatchLen);return true;
}
#ifdef FFXHOOKS_HAVE_POLYHOOK
static bool DiscardUnpublished() {
    if(g_applyAttempted)return false;
    const bool imports=RonsoPool::RestoreIoImports();
    while(g_targetCount) {
        if(MH_RemoveHook(reinterpret_cast<void*>(g_targets[g_targetCount-1]))!=MH_OK)return false;
        --g_targetCount;
    }
    g_created=false;g_damageCreated=false;
    if(g_stub){VirtualFree(g_stub,0,MEM_RELEASE);g_stub=nullptr;}
    RonsoPool::DiscardUnpublishedRuntime();return imports;
}
static bool Neutralize() {
    bool targets=true;
    if(g_targetCount) {
        const auto report=MinHookBatch::NeutralizeBatch(&MinHookBatch::ProcessCoordinator(),
            MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::NovaSuperDamage,g_targets.data(),g_targetCount);
        targets=report.neutralized;
    }
    const bool imports=RonsoPool::RestoreIoImports();
    InterlockedExchange(&g_installed,0);
    return targets&&imports;
}
static bool CreateOwned(uintptr_t address,void* replacement,void** original) {
    if(g_targetCount==g_targets.size()||MH_CreateHook(reinterpret_cast<void*>(address),replacement,original)!=MH_OK)
        return false;
    g_targets[g_targetCount++]=address;g_created=true;
    return *original!=nullptr;
}
#endif
} // namespace

void RequestNovaSuperDamageStop() {
    InterlockedExchange(&g_admission,2);
    RonsoPool::RequestStop();
}
NovaSuperDamageInstallResult InstallNovaSuperDamageHook(
    uintptr_t base,bool bypass,bool logHits,bool ronsoMana,NovaSuperDamageLogFn log) {
    NovaSuperDamageInstallResult result{false,0};
    const bool damage=bypass||logHits;
    const bool poolRequested=ronsoMana||RonsoPool::HasPersistentOwnership()||NativeSaveEvents::Requested();
    if(!damage&&!poolRequested)return result;
    LifecycleScope lock;
    if(!lock.held)return result;
    if(InterlockedCompareExchange(&g_installed,0,0)!=0) {
        result.ok=InterlockedCompareExchange(&g_admission,0,0)==1;
        result.stub=reinterpret_cast<uintptr_t>(g_stub);return result;
    }
    if(InterlockedCompareExchange(&g_attempted,1,0)!=0||InterlockedCompareExchange(&g_admission,0,0)==2)return result;
    g_logFn.store(log,std::memory_order_release);g_hitLogCount=0;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    uint8_t saved[kPatchLen]={};
    if(damage&&(!ValidateProfile(base)||!ResolveClampPatchSite(base,&g_patchVa,&g_resumeVa,saved))) {
        HookLog("[ffx-hooks] NovaClamp rejected unsupported profile or changed native control flow\n");return result;
    }
    if(MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready)return result;
    RonsoPool::PreparedRuntime pool{};
    try {
        if(damage&&!BuildStub(g_resumeVa,bypass,logHits,&g_stub,&g_stubLen))return result;
        if(poolRequested&&!RonsoPool::PrepareRuntime(base,ronsoMana,log,&pool)){DiscardUnpublished();return result;}
    } catch(...) {DiscardUnpublished();return result;}
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCSTR>(&InstallNovaSuperDamageHook),&pinned)) {
        DiscardUnpublished();return result;
    }
    if(damage) {
        if(!CreateOwned(g_patchVa,g_stub,&g_original)){DiscardUnpublished();return result;}
        g_damageCreated=true;
    }
    for(size_t i=0;i<pool.count;++i) {
        const auto& target=pool.hooks[i];
        if(!CreateOwned(target.address,target.replacement,target.original)){DiscardUnpublished();return result;}
    }
    if((damage&&!ValidateProfile(base))||InterlockedCompareExchange(&g_admission,0,0)==2||
        (pool.ioRequired&&!RonsoPool::InstallIoImports())) {
        (void)DiscardUnpublished();return result;
    }
    if(InterlockedCompareExchange(&g_admission,1,0)!=0) {
        (void)DiscardUnpublished();return result;
    }
    const auto report=MinHookBatch::EnableBatch(&MinHookBatch::ProcessCoordinator(),
        MinHookBatch::RuntimeBatchIo(),MinHookBatch::Owner::NovaSuperDamage,g_targets.data(),g_targetCount);
    g_applyAttempted=report.applyAttempted;
    if(report.result!=MinHookBatch::BatchResult::Applied) {
        RequestNovaSuperDamageStop();
        if(!g_applyAttempted)(void)DiscardUnpublished();
        HookLog("[ffx-hooks] NovaClamp enable failed; published storage retained=%d\n",g_applyAttempted?1:0);
        return result;
    }
    if(InterlockedCompareExchange(&g_admission,0,0)!=1){Neutralize();return result;}
    if(pool.ioRequired)RonsoPool::ActivateRuntime();
    if(InterlockedCompareExchange(&g_admission,0,0)!=1){Neutralize();return result;}
    InterlockedExchange(&g_installed,1);result.ok=true;result.stub=reinterpret_cast<uintptr_t>(g_stub);
    if(damage)HookLog("[ffx-hooks] NovaClamp installed patch@0x%08X resume@0x%08X cmd=0x3073 actor=3 component=HP bypass=%d log=%d\n",
        static_cast<unsigned>(g_patchVa),static_cast<unsigned>(g_resumeVa),bypass?1:0,logHits?1:0);
    HookLog("[ffx-hooks] RonsoPool mode=%s capacity-override=%u native-per-command-cost=1 save-compatibility=%d partial-entry=%d left-input=%d\n",
        ronsoMana?"ACTIVE":(pool.ioRequired?"COMPATIBILITY":"OFF"),ronsoMana?200u:0u,pool.ioRequired?1:0,ronsoMana?1:0,ronsoMana?1:0);
#else
    (void)base;
    HookLog("[ffx-hooks] NovaClamp unavailable without the shared MinHook coordinator\n");
#endif
    return result;
}
bool RemoveNovaSuperDamageHook(NovaSuperDamageLogFn log) {
    RequestNovaSuperDamageStop();
    LifecycleScope lock;if(!lock.held)return false;
    if(!g_created)return true;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    const bool restored=Neutralize();
    if(log)log(restored?"[ffx-hooks] NovaClamp disabled; published stub/trampoline retained until process exit\n":
                       "[ffx-hooks] NovaClamp retirement incomplete; inert admission and storage retained\n");
    // Threads can be paused inside machine code before any C++ callback accounting.
    // Even a successful disable cannot prove that published code is freeable.
    return restored;
#else
    (void)log;return false;
#endif
}
bool IsNovaSuperDamageHookInstalled() {
    return g_damageCreated&&InterlockedCompareExchange(&g_installed,0,0)!=0&&InterlockedCompareExchange(&g_admission,0,0)==1;
}
} // namespace FfxHooks
