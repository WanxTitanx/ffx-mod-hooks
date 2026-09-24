// Jarvis-HOOK. Curated AI arguments, owned name labels and per-enemy reward views.
// Original asset bytes and the actor's resource pointers remain unchanged.
#include "SinAiHook.h"
#include "F8RuntimeCore.h"
#include "SinMetadataCore.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <atomic>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks::SinAi {
namespace {
using OriginalFn=int(__cdecl*)(int,const char*,const void*);
using StepFn=int(__cdecl*)(int,int,float);
using RewardFn=int(__cdecl*)(std::uint32_t,std::uint32_t,const void*,std::uint32_t);
std::atomic<bool> g_accepting{false},g_ready{false};
std::atomic<StatusCode> g_status{StatusCode::Off};
std::atomic<unsigned> g_accepted{0},g_rejected{0};
alignas(8) volatile LONG64 g_generation=0;
std::uintptr_t g_base=0;
alignas(8) std::uint64_t g_original=0;
alignas(8) std::uint64_t g_originalStep=0;
alignas(8) std::uint64_t g_originalReward=0;
ContextReader g_context=nullptr;
RegistrationReporter g_reporter=nullptr;
NaturalReporter g_naturalReporter=nullptr;
Pack g_pack{};
void* g_arena=nullptr;
struct Metadata {
    std::uintptr_t actor=0,whole=0;
    std::uint64_t generation=0;
    DWORD owner=0;
    unsigned monster=0,curse=0,threat=0;
    bool nameOwned=false;
    std::array<std::uint8_t,SinMetadata::kNameSize> originalName{},markedName{};
    std::array<std::uint8_t,SinMetadata::kLootSize> reward{};
};
std::array<Metadata,8> g_metadata{};
#ifdef FFXHOOKS_HAVE_POLYHOOK
PLH::x86Detour* g_hook=nullptr;
PLH::x86Detour* g_stepHook=nullptr;
PLH::x86Detour* g_rewardHook=nullptr;
#endif
bool ReadMemory(void*,std::uintptr_t at,void* target,std::size_t count){
    if(!target || !count || at<0x10000u || at+count<at)return false;
    std::uintptr_t cursor=at;std::size_t remaining=count;
    while(remaining){
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto end=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(end<=cursor)return false;
        const auto part=(std::min)(remaining,static_cast<std::size_t>(end-cursor));cursor+=part;remaining-=part;
    }
    __try{std::memcpy(target,reinterpret_cast<const void*>(at),count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WriteName(std::uintptr_t at,const std::array<std::uint8_t,SinMetadata::kNameSize>& name){
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(reinterpret_cast<void*>(at),&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
        (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD protection=memory.Protect&0xFFu;
    if(protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return false;
    const auto start=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if(at<start || at-start>memory.RegionSize || name.size()>memory.RegionSize-(at-start))return false;
    __try{std::memcpy(reinterpret_cast<void*>(at),name.data(),name.size());return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool SameActor(const Metadata& metadata,unsigned slot){
    if(!metadata.actor || metadata.owner!=GetCurrentThreadId() || slot>=8)return false;
    std::uint8_t value[4]{};
    if(!ReadMemory(nullptr,g_base+0xD34460u,value,4) || U32(value)+slot*0xF90u!=metadata.actor)return false;
    if(!ReadMemory(nullptr,metadata.actor+0x48u,value,4) || U32(value)!=metadata.whole)return false;
    return ReadMemory(nullptr,metadata.actor+0xEu,value,2) && U16(value)==metadata.monster;
}
void RestoreNames(std::uint64_t generation){
    for(unsigned slot=0;slot<g_metadata.size();++slot){auto& metadata=g_metadata[slot];
        if(!metadata.generation || (generation && metadata.generation!=generation) || metadata.owner!=GetCurrentThreadId())continue;
        std::array<std::uint8_t,SinMetadata::kNameSize> current{};
        if(metadata.nameOwned && SameActor(metadata,slot)){
            if(!ReadMemory(nullptr,metadata.actor+SinMetadata::kNameOffset,current.data(),current.size()))continue;
            if(current==metadata.markedName && !WriteName(metadata.actor+SinMetadata::kNameOffset,metadata.originalName))continue;
        }
        metadata.generation=0;metadata.nameOwned=false;
    }
}
bool MarkName(Metadata& metadata,unsigned slot){
    if(!SameActor(metadata,slot))return false;
    std::array<std::uint8_t,SinMetadata::kNameSize> current{};
    if(!ReadMemory(nullptr,metadata.actor+SinMetadata::kNameOffset,current.data(),current.size()))return false;
    if(metadata.nameOwned){
        if(current==metadata.markedName)return true;
        if(current!=metadata.originalName)return false;
    } else {
        if(!SinMetadata::NameView(current,metadata.curse,metadata.threat,&metadata.markedName))return false;
        metadata.originalName=current;
    }
    if(!WriteName(metadata.actor+SinMetadata::kNameOffset,metadata.markedName))return false;
    metadata.nameOwned=true;return true;
}
void RememberMetadata(unsigned slot,unsigned monster,unsigned curse,unsigned threat,std::uint64_t generation){
    if(slot>=8 || !generation)return;
    for(const auto& old:g_metadata)if(old.generation && old.generation!=generation){RestoreNames(old.generation);break;}
    std::uint8_t value[4]{};
    if(!ReadMemory(nullptr,g_base+0xD34460u,value,4))return;
    const auto actor=static_cast<std::uintptr_t>(U32(value))+slot*0xF90u;
    if(!ReadMemory(nullptr,actor+0x48u,value,4))return;
    auto& metadata=g_metadata[slot];
    if(metadata.generation && metadata.generation!=generation)return;
    if(metadata.generation==generation && metadata.actor==actor && metadata.monster==monster){MarkName(metadata,slot);return;}
    metadata={};metadata.actor=actor;metadata.whole=U32(value);metadata.monster=monster;
    metadata.curse=curse;metadata.threat=threat;metadata.generation=generation;metadata.owner=GetCurrentThreadId();
    MarkName(metadata,slot);
}
const void* RewardViewFor(const void* source){
    Context context{};
    if(!source || !g_accepting || !g_context || !g_context(true,&context) || !context.enabled)return source;
    for(unsigned slot=0;slot<g_metadata.size();++slot){auto& metadata=g_metadata[slot];
        if(metadata.generation!=context.generation || !SameActor(metadata,slot))continue;
        std::uint8_t value[4]{},header[0x24]{};
        if(!ReadMemory(nullptr,metadata.actor+SinMetadata::kLootPointerOffset,value,4) || U32(value)!=reinterpret_cast<std::uintptr_t>(source))continue;
        if(!ReadMemory(nullptr,metadata.whole,header,sizeof(header)))continue;
        const auto offset=U32(header+0x14),end=U32(header+0x18),size=U32(header+0x20);
        if(offset<sizeof(header) || end<offset || end-offset<SinMetadata::kLootSize || end>size ||
            metadata.whole+offset<metadata.whole || metadata.whole+offset!=reinterpret_cast<std::uintptr_t>(source))continue;
        std::array<std::uint8_t,SinMetadata::kLootSize> original{};
        if(ReadMemory(nullptr,reinterpret_cast<std::uintptr_t>(source),original.data(),original.size()) &&
            SinMetadata::RewardView(original,metadata.threat,&metadata.reward))return metadata.reward.data();
    }
    return source;
}
bool Profile(std::uintptr_t base){
    __try {
        if(!base)return false;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || !F8Runtime::IsSupportedExecutable({pe->FileHeader.Machine,pe->OptionalHeader.Magic,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage}))return false;
        std::uint8_t code[0x4B]{};std::memcpy(code,reinterpret_cast<void*>(base+kRegisterRva),sizeof(code));
        constexpr unsigned offsets[]={0xE,0x23,0x29,0x33,0x3D};
        constexpr std::uintptr_t targets[]={0xD34468,0xD3447C,0xD34468,0xD3449C,0xD34468};
        for(unsigned i=0;i<5;++i){if(U32(code+offsets[i])!=base+targets[i])return false;std::memset(code+offsets[i],0,4);}
        constexpr std::uint8_t caller[]={0xE8,0xD4,0x38,0x01,0};
        return Hash(code,sizeof(code))==0x295F0925E21F0F41ULL && std::memcmp(reinterpret_cast<void*>(base+kRegisterCallerRva-5),caller,sizeof(caller))==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Prepare(std::uintptr_t base,const std::uint8_t* bytes,std::size_t size,ContextReader reader,RegistrationReporter reporter){
    if(g_ready || g_arena || !reader || !Profile(base)){g_status=StatusCode::Unsupported;return false;}
    Pack temporary;
    if(temporary.Load(bytes,size)!=PackCode::Ok){g_status=StatusCode::InvalidPack;return false;}
    std::size_t needed=0;for(const auto& view:temporary.views)needed+=(view.proof->size+15u)&~std::size_t(15u);
    auto* arena=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,needed,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!arena){g_status=StatusCode::InvalidPack;return false;}
    std::size_t at=0;
    for(auto& view:temporary.views){std::memcpy(arena+at,view.bytes,view.proof->size);view.bytes=arena+at;at+=(view.proof->size+15u)&~std::size_t(15u);}
    DWORD old=0;
    if(!VirtualProtect(arena,needed,PAGE_READONLY,&old)){VirtualFree(arena,0,MEM_RELEASE);g_status=StatusCode::InvalidPack;return false;}
    g_pack=temporary;g_arena=arena;g_base=base;g_context=reader;g_reporter=reporter;
    return true;
}
int Dispatch(int channel,const char* name,const void* original,std::uint32_t caller){
    const auto next=reinterpret_cast<OriginalFn>(g_original);
    if(!next)return -1;
    const void* selected=original;bool replaced=false;unsigned actorSlot=8,selectedMonster=0,selectedCurse=0,selectedThreat=0;std::uint64_t generation=0;
    if(g_accepting && channel==1 && caller==kRegisterCallerRva){
        const bool commands=CommandsReady({nullptr,ReadMemory},g_base);
        Context context{};
        if(g_context && g_context(commands,&context) && context.enabled && context.generation){
            if(static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_generation,0,0))!=context.generation){
                InterlockedExchange64(&g_generation,static_cast<LONG64>(context.generation));g_accepted=0;g_rejected=0;
            }
            if(!commands){g_status=StatusCode::DependenciesMissing;}
            else {
                char label[5]{};unsigned monster=0;
                if(ReadMemory(nullptr,reinterpret_cast<std::uintptr_t>(name),label,sizeof(label)) && MonsterName(label,&monster)){
                    const auto assignment=SinSpread::BuildAssignment(context.field,context.seed,context.visit,context.distribution,true);
                    const auto* actor=assignment.Find(monster);
                    const View* view=actor&&actor->curse?g_pack.Find(monster,actor->curse):nullptr;
                    if(view && RegistrationSlot({nullptr,ReadMemory},g_base,reinterpret_cast<std::uintptr_t>(original),monster,&actorSlot)){
                        // Fixed bounded scratch is registration-only, never a frame allocation.
                        std::array<std::uint8_t,kMaxScript> source{};
                        if(ReadMemory(nullptr,reinterpret_cast<std::uintptr_t>(original),source.data(),view->proof->originalSize) && Hash(source.data(),view->proof->originalSize)==view->proof->originalHash){selected=view->bytes;replaced=true;generation=context.generation;
                            selectedMonster=SinSpread::NativeMonsterId(static_cast<std::uint16_t>(monster));selectedCurse=actor->curse;selectedThreat=actor->threat;}
                        else {++g_rejected;g_status=StatusCode::SourceMismatch;}
                    }
                }
            }
        }
    }
    const int result=next(channel,name,selected);
    if(replaced && result==0){RememberMetadata(actorSlot,selectedMonster,selectedCurse,selectedThreat,generation);
        ++g_accepted;if(g_rejected==0)g_status=StatusCode::Installed;if(g_reporter)g_reporter(actorSlot,generation);}
    return result;
}
#ifdef FFXHOOKS_HAVE_POLYHOOK
int __cdecl RewardShim(std::uint32_t first,std::uint32_t second,const void* loot,std::uint32_t overkill){
    const auto original=reinterpret_cast<RewardFn>(g_originalReward);
    // The original consumer selects normal/overkill AP and applies existing F8
    // multipliers. Its read-only input changes; native tables and actor pointers do not.
    return original?original(first,second,RewardViewFor(loot),overkill):0;
}
int __cdecl NaturalStepShim(int field,int group,float distance){
    const auto address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto caller=address>=g_base?static_cast<std::uint32_t>(address-g_base):0u;
    const auto original=reinterpret_cast<StepFn>(g_originalStep);
    const int result=original?original(field,group,distance):0;
    if(g_accepting && g_naturalReporter && caller==SinNatural::kCallerRva && result==-1){
        std::uint8_t selected[4]{};
        if(ReadMemory(nullptr,g_base+SinNatural::kFieldRowRva,selected,sizeof(selected))){
            const SinNatural::Evidence evidence{caller,result,field,group,U16(selected),selected[2],selected[3]};
            if(SinNatural::Admitted(evidence))g_naturalReporter(evidence);
        }
    }
    return result;
}
int __cdecl RegisterShim(int channel,const char* name,const void* data){
    const auto address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto caller=address>=g_base?static_cast<std::uint32_t>(address-g_base):0u;
    return Dispatch(channel,name,data,caller);
}
#endif
}
bool Start(std::uintptr_t base,const wchar_t* path,ContextReader reader,RegistrationReporter reporter,NaturalReporter naturalReporter){
    if(g_ready)return true;
    if(!path || !reader || !reporter || !naturalReporter)return false;
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(g_hook || g_stepHook || g_rewardHook){g_status=StatusCode::Conflict;return false;}
    std::array<std::uint8_t,SinNatural::kPrefix.size()> step{};
    std::array<std::uint8_t,SinNatural::kCall.size()> caller{};
    std::array<std::uint8_t,SinMetadata::kRewardPrefix.size()> reward{};
    if(!Profile(base) || !ReadMemory(nullptr,base+SinNatural::kStepRva,step.data(),step.size()) || step!=SinNatural::kPrefix ||
        !ReadMemory(nullptr,base+SinNatural::kCallerRva-caller.size(),caller.data(),caller.size()) || caller!=SinNatural::kCall ||
        !ReadMemory(nullptr,base+SinMetadata::kRewardRva,reward.data(),reward.size()) || reward!=SinMetadata::kRewardPrefix){
        g_status=StatusCode::Unsupported;return false;
    }
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){g_status=StatusCode::MissingPack;return false;}
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(file,&length) || length.QuadPart<12 || length.QuadPart>kMaxPack){CloseHandle(file);g_status=StatusCode::InvalidPack;return false;}
    auto* bytes=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,static_cast<SIZE_T>(length.QuadPart),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));DWORD read=0;
    const bool loaded=bytes && ReadFile(file,bytes,static_cast<DWORD>(length.QuadPart),&read,nullptr) && read==length.QuadPart;
    CloseHandle(file);
    const bool prepared=loaded&&Prepare(base,bytes,read,reader,reporter);if(bytes)VirtualFree(bytes,0,MEM_RELEASE);
    if(!prepared)return false;
    g_naturalReporter=naturalReporter;
    HMODULE own=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&RegisterShim),&own)){g_status=StatusCode::Conflict;return false;}
    try {
        g_hook=new PLH::x86Detour(base+kRegisterRva,reinterpret_cast<std::uintptr_t>(&RegisterShim),&g_original);
        if(!g_hook->hook()){g_status=StatusCode::Conflict;return false;}
        g_stepHook=new PLH::x86Detour(base+SinNatural::kStepRva,reinterpret_cast<std::uintptr_t>(&NaturalStepShim),&g_originalStep);
        if(!g_stepHook->hook()){g_status=StatusCode::Conflict;return false;}
        g_rewardHook=new PLH::x86Detour(base+SinMetadata::kRewardRva,reinterpret_cast<std::uintptr_t>(&RewardShim),&g_originalReward);
        if(!g_rewardHook->hook()){g_status=StatusCode::Conflict;return false;}
    } catch(...){g_status=StatusCode::Conflict;return false;}
    // Channel contexts retain pointers into these buffers. Both the applied
    // gateway and the read-only arena remain resident until process exit.
    g_ready=true;g_accepting=true;g_status=StatusCode::Ready;return true;
#else
    (void)base;g_status=StatusCode::Unsupported;return false;
#endif
}
void RequestStop(){g_accepting=false;}
void RefreshLabels(std::uint64_t generation){if(g_accepting)for(unsigned slot=0;slot<g_metadata.size();++slot)
    if(g_metadata[slot].generation==generation)MarkName(g_metadata[slot],slot);}
void EndEncounter(std::uint64_t generation){RestoreNames(generation);}
bool Ready(){return g_ready && g_accepting;}
bool DependenciesReady(){return Ready()&&CommandsReady({nullptr,ReadMemory},g_base);}
Status CurrentStatus(){return {g_status.load(),g_accepted.load(),g_rejected.load(),static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_generation,0,0))};}
const char* Detail(){switch(g_status.load()){
case StatusCode::Ready:return "Curse scripts ready for natural encounters";
case StatusCode::Installed:return "Curse scripts registered for this encounter";
case StatusCode::MissingPack:return "Curse profile pack is missing";
case StatusCode::InvalidPack:return "Curse profile pack could not be verified";
case StatusCode::Unsupported:return "Curse runtime is unsupported in this build";
case StatusCode::Conflict:return "Curse registration hook is unavailable";
case StatusCode::DependenciesMissing:return "Spira Reforge command profiles are not loaded";
case StatusCode::SourceMismatch:return "An enemy AI differs from the verified profile";
default:return "Save and restart to enable curse scripts";
}}
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t base,const std::uint8_t* bytes,std::size_t size,ContextReader reader,RegistrationReporter reporter,RegisterFn original){
    if(!Prepare(base,bytes,size,reader,reporter))return false;g_original=reinterpret_cast<std::uintptr_t>(original);g_ready=true;g_accepting=true;g_status=StatusCode::Ready;return true;
}
int RegisterForTests(int channel,const char* name,const void* original,std::uint32_t caller){return Dispatch(channel,name,original,caller);}
const void* RewardViewForTests(const void* source){return RewardViewFor(source);}
#endif
}
