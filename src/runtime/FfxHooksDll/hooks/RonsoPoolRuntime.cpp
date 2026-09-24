#include "RonsoPoolRuntime.h"
#include "RonsoPoolCore.h"
#include "RonsoPoolSave.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolEvidence.h"
#include "F8RuntimeCore.h"
#include "NativeSaveEvents.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdio>

namespace FfxHooks::RonsoPool {
namespace {
constexpr uint32_t kReadImport=0x0070C3F4u,kWriteImport=0x0070C428u;
constexpr uint32_t kActorTable=0x00D334CCu,kLearnedBank=0x00D307FCu;
using ReadFn=size_t(__cdecl*)(void*,size_t,size_t,void*);
using WriteFn=size_t(__cdecl*)(const void*,size_t,size_t,void*);
using FileNoFn=int(__cdecl*)(void*);
using OsHandleFn=intptr_t(__cdecl*)(int);
using TellFn=int64_t(__cdecl*)(void*);
using AvailabilityFn=int(__cdecl*)(uint8_t,int16_t);
using MenuReadyFn=int(__cdecl*)(int);
using CostGateFn=int(__cdecl*)(int,const uint8_t*,int);
using ResetFn=int(__cdecl*)();
using CommandFn=const uint8_t*(__cdecl*)(int,int);
uintptr_t moduleBase=0;
std::atomic<uint32_t> readiness{0};
std::atomic<uint32_t> stateEpoch{1},activeActor{0},noticeCount{0};
std::atomic<bool> storageFault{false};
uint32_t sessionEpoch=1;
bool requestedGameplay=false,preparedOnce=false;
LogFn logger=nullptr;
OwnerStore store;
SaveSession session{};
std::mutex stateMutex;
ReadFn readOriginal=nullptr;
WriteFn writeOriginal=nullptr;
FileNoFn fileNo=nullptr;
OsHandleFn osHandle=nullptr;
TellFn tell=nullptr;
void* menuReadyOriginal=nullptr;
void* leftEntryOriginal=nullptr;
std::atomic<uint32_t> leftInputNotices{0};
void* costGateOriginal=nullptr;
std::atomic<uint32_t> costUiNotices{0},costCommitNotices{0};
void* resetOriginal=nullptr;
void* maxOriginal=nullptr;
uint8_t* maxStub=nullptr;
struct ImportPatch {bool owned=false,protectionPending=false;DWORD originalProtection=0;};
ImportPatch readPatch{},writePatch{};
#ifdef FFXHOOKS_TESTING
ImportProtectFn protectForFixture=nullptr;
#endif
bool ProtectImport(uintptr_t address,uint32_t protection,DWORD* prior) noexcept {
#ifdef FFXHOOKS_TESTING
    if(protectForFixture) {
        uint32_t value=0;const bool ok=protectForFixture(address,4,protection,&value);
        *prior=value;return ok;
    }
#endif
    return VirtualProtect(reinterpret_cast<void*>(address),4,protection,prior)!=FALSE;
}
std::atomic<uint32_t> actorThread{0};
uint64_t actorGeneration=0;
Owner actorOwner{};
void Log(const char* text) {if(logger)logger(text);}
void Notice(const char* text) {
    if(noticeCount.fetch_add(1)<16)Log(text);
}
bool Copy(void* target,const void* source,size_t bytes) noexcept {
    __try {std::memcpy(target,source,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool DataRange(uintptr_t address,size_t bytes,bool write=false) noexcept {
    if(address<0x10000||bytes==0||address>UINT32_MAX-bytes)return false;
    const uintptr_t end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||info.State!=MEM_COMMIT||
            (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD p=info.Protect&0xFF;
        const bool writable=p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        const bool readable=writable||p==PAGE_READONLY||p==PAGE_EXECUTE_READ;
        if(!readable||(write&&!writable))return false;
        address=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
    }
    return true;
}
uint8_t* PartyActor(uint8_t slot) noexcept {
    if(slot>=8)return nullptr;
    uintptr_t table=0;
    if(!Copy(&table,reinterpret_cast<void*>(moduleBase+kActorTable),4)||table>UINT32_MAX-8*0xF90u)return nullptr;
    auto* actor=reinterpret_cast<uint8_t*>(table+slot*0xF90u);
    uint16_t id=UINT16_MAX;
    if(!DataRange(reinterpret_cast<uintptr_t>(actor),0xF90)||!Copy(&id,actor+0xE,2))return nullptr;
    // The party-record array uses character IDs. Visible party order is a
    // separate list; matching both prevents an enemy/clone from borrowing it.
    return id==kCharacter && slot==id?actor:nullptr;
}
bool Learned(uint16_t command) noexcept {
    if(command<96 || command>351)return false;
    const unsigned index=command-96;uint16_t bits=0;
    return Copy(&bits,reinterpret_cast<void*>(moduleBase+kLearnedBank+(index/16)*2),2)&&
        (bits&(1u<<(index%16)))!=0;
}
const uint8_t* Command(uint16_t id) noexcept {
    __try {return reinterpret_cast<CommandFn>(moduleBase+0x390AE0)(id,0);}
    __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
bool Cost(uint16_t id,bool header,uint8_t* cost) noexcept {
    const auto* row=Command(id);uint8_t fields[39]={};
    if(!row || !DataRange(reinterpret_cast<uintptr_t>(row),sizeof(fields))||!Copy(fields,row,sizeof(fields)))return false;
    if(fields[25]!=kCharacter||fields[24]!=4||fields[23]!=4||
        ((fields[22]&7u)!=0)!=header)return false;
    *cost=fields[38];return true;
}
bool ReplaceMaximum(uint8_t* actor,uint8_t expected,uint8_t desired) noexcept {
    __try {
        return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile CHAR*>(actor+0x5BD),
            static_cast<CHAR>(desired),static_cast<CHAR>(expected)))==expected;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ImageRange(uintptr_t address,size_t bytes,bool executable) noexcept {
    if(!moduleBase||!address||bytes==0||address>UINT32_MAX-bytes)return false;
    const uintptr_t end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||
            info.State!=MEM_COMMIT||info.Type!=MEM_IMAGE||
            reinterpret_cast<uintptr_t>(info.AllocationBase)!=moduleBase||
            (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD p=info.Protect&0xFF;
        const bool code=p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        if(executable&&!code)return false;
        address=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
    }
    return true;
}
bool Profile() {
    uint8_t header[0x1000]={};F8Runtime::ExecutableIdentity identity{};
    if(!ImageRange(moduleBase,sizeof(header),false)||!Copy(header,reinterpret_cast<void*>(moduleBase),sizeof(header))||
        F8Runtime::ParseExecutableIdentity(header,sizeof(header),&identity)!=F8Runtime::ProfileResult::Supported||
        !F8Runtime::IsSupportedExecutable(identity)||moduleBase>UINT32_MAX-identity.sizeOfImage)return false;
    for(const auto& span:Evidence::kSpans) {
        std::vector<uint8_t> bytes(span.size);
        if(!ImageRange(moduleBase+span.rva,span.size,true)||
            !Copy(bytes.data(),reinterpret_cast<void*>(moduleBase+span.rva),bytes.size())||
            !Evidence::Matches(span,bytes.data(),bytes.size(),moduleBase))return false;
    }
    return ImageRange(moduleBase+kReadImport,4,false)&&ImageRange(moduleBase+kWriteImport,4,false);
}
std::wstring StorageDirectory() {
    HMODULE module=nullptr;wchar_t path[4096]={};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PrepareRuntime),&module))return {};
    const DWORD length=GetModuleFileNameW(module,path,4096);
    if(length==0||length>=4096)return {};
    std::wstring directory(path,length);const auto slash=directory.find_last_of(L"/\\");
    if(slash==std::wstring::npos)return {};
    return directory.substr(0,slash)+L"\\config\\ronso-pool-v1";
}
bool StreamInfo(void* stream,wchar_t* path,DWORD capacity,int64_t* position) noexcept {
    __try {
        if(!fileNo||!osHandle||!tell||!stream)return false;
        const int descriptor=fileNo(stream);if(descriptor<0)return false;
        const intptr_t native=osHandle(descriptor);
        if(native==-1)return false;
        const HANDLE handle=reinterpret_cast<HANDLE>(native);
        if(GetFileType(handle)!=FILE_TYPE_DISK)return false;
        const DWORD length=GetFinalPathNameByHandleW(handle,path,capacity,FILE_NAME_NORMALIZED);
        if(length==0||length>=capacity)return false;
        *position=tell(stream);return *position>=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uint16_t CurrentScene() noexcept {
    uint16_t scene=UINT16_MAX;
    Copy(&scene,reinterpret_cast<void*>(moduleBase+0xD2CA90),2);return scene;
}
bool IoCaller(uintptr_t address,bool write) noexcept {
    if(address<moduleBase)return false;
    const uintptr_t rva=address-moduleBase;
    return write?(rva==0x2F06C5u||rva==0x2F0A45u):rva==0x2F0228u;
}
struct IoWork {
    SaveImage input{},output{};
    std::wstring path;
    SavedOwner owner{};
    bool needsOwner=false,useOutput=false;
    uint32_t epoch=0;
};
size_t __cdecl ReadShim(void* data,size_t size,size_t count,void* stream) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(!readOriginal)return 0;
    if(readiness.load()!=1||size!=1||count!=kSaveSize||!IoCaller(caller,false))
        return readOriginal(data,size,count,stream);
    const uint32_t epoch=stateEpoch.load();wchar_t path[4096]={};int64_t offset=-1;
    const bool candidate=StreamInfo(stream,path,4096,&offset)&&offset==0;
    const size_t result=readOriginal(data,size,count,stream);
    const DWORD nativeError=GetLastError();
    try {
        if(candidate&&result==count&&readiness.load()==1&&stateEpoch.load()==epoch&&
            OwnerStore::IsSavePath(path)&&DataRange(reinterpret_cast<uintptr_t>(data),kSaveSize,true)) {
            auto work=std::make_unique<IoWork>();work->path=path;
            if(Copy(work->input.data(),data,kSaveSize)) {
                SavedOwner metadata{};const auto ownership=store.Read(work->path,work->input,&metadata);
                SaveSession next{};SaveDecision decision=SaveDecision::Invalid;
                if(ownership==OwnerRead::Found||ownership==OwnerRead::Missing)
                    decision=LoadPool(requestedGameplay,work->input,ownership==OwnerRead::Found?&metadata:nullptr,
                        CurrentScene(),&next,&work->output);
                {
                std::lock_guard<std::mutex> lock(stateMutex);
                if(readiness.load()==1&&stateEpoch.load()==epoch) {
                    session={};activeActor.store(0);actorThread.store(0);
                    sessionEpoch=stateEpoch.fetch_add(1)+1;
                    if(decision==SaveDecision::Converted) {
                        if(Copy(data,work->output.data(),kSaveSize)){session=next;storageFault.store(false);}
                        else {
                            storageFault.store(true);
                            Notice("[ffx-hooks] ERROR RonsoPool load-buffer publication failed\n");
                        }
                    } else if(decision==SaveDecision::Native) {
                        session=next;storageFault.store(false);
                    } else if(ownership==OwnerRead::Invalid||ownership==OwnerRead::Unavailable||
                              decision==SaveDecision::OwnershipConflict) {
                        storageFault.store(true);
                        Notice("[ffx-hooks] ERROR RonsoPool save ownership unavailable; native data preserved\n");
                    }
                }
                }
                // Notify only after releasing the pool mutex. Subscribers copy
                // these bounded images; they never own or rewrite the CRT buffer.
                NativeSaveEvents::ReadCompleted(path,work->input.data(),
                    static_cast<const unsigned char*>(data),kSaveSize);
            }
        }
    } catch(...) {
        storageFault.store(true);Notice("[ffx-hooks] ERROR RonsoPool load metadata failed; no retry\n");
    }
    SetLastError(nativeError);return result;
}
size_t __cdecl WriteShim(const void* data,size_t size,size_t count,void* stream) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(!writeOriginal)return 0;
    if(readiness.load()!=1||size!=1||count!=kSaveSize||!IoCaller(caller,true)||
       (storageFault.load()&&!NativeSaveEvents::Requested()))
        return writeOriginal(data,size,count,stream);
    std::unique_ptr<IoWork> work;
    try {
        wchar_t path[4096]={};int64_t offset=-1;
        if(StreamInfo(stream,path,4096,&offset)&&offset==0&&OwnerStore::IsSavePath(path)&&
            DataRange(reinterpret_cast<uintptr_t>(data),kSaveSize)) {
            auto prepared=std::make_unique<IoWork>();prepared->path=path;
            SaveSession snapshot{};
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                prepared->epoch=stateEpoch.load();
                if(sessionEpoch==prepared->epoch)snapshot=session;
            }
            if(Copy(prepared->input.data(),data,kSaveSize)) {
                const auto decision=storageFault.load()?SaveDecision::Native:
                    SavePool(requestedGameplay,snapshot,prepared->input,
                        &prepared->output,&prepared->owner,&prepared->needsOwner);
                if(decision==SaveDecision::Converted){prepared->useOutput=true;work=std::move(prepared);}
                else if(decision==SaveDecision::OwnershipConflict) {
                    storageFault.store(true);Notice("[ffx-hooks] ERROR RonsoPool save state conflict; native data preserved\n");
                }
                if(!work&&NativeSaveEvents::Requested())work=std::move(prepared);
            }
        }
    } catch(...) {Notice("[ffx-hooks] ERROR RonsoPool save preparation failed; native write retained\n");}
    if(work&&(readiness.load()!=1||stateEpoch.load()!=work->epoch))work.reset();
    const void* bytes=work&&work->useOutput?work->output.data():data;
    // Never put this call inside a retry/catch path: an exception after fwrite
    // must not append a second save payload to the native stream.
    const size_t result=writeOriginal(bytes,size,count,stream);
    const DWORD nativeError=GetLastError();
    if(work&&work->needsOwner&&result==count) {
        try {
            if(!store.Write(work->path,work->output,work->owner)) {
                storageFault.store(true);
                Notice("[ffx-hooks] ERROR RonsoPool ownership commit failed; full charge remains in native save\n");
            }
        } catch(...) {
            storageFault.store(true);
            Notice("[ffx-hooks] ERROR RonsoPool ownership commit threw; native save is not retried\n");
        }
    }
    if(work&&result==count)NativeSaveEvents::WriteCompleted(work->path.c_str(),
        static_cast<const unsigned char*>(bytes),kSaveSize);
    SetLastError(nativeError);return result;
}
Availability DecideAvailability(uint8_t slot,int16_t command,int vanilla) {
    const uint16_t raw=static_cast<uint16_t>(command),group=raw&0xF000u,id=raw&0xFFFu;
    if((group!=0&&group!=0x3000u)||((id<104||id>115)&&id!=282))return Availability::Native;
    auto* actor=PartyActor(slot);
    if(!actor||activeActor.load()!=reinterpret_cast<uintptr_t>(actor))return Availability::Native;
    uint8_t charge=0,maximum=0,noCost=0,blockedA=0,blockedB=0;
    uint16_t status=0;int32_t hp=0;
    if(!Copy(&charge,actor+0x5BC,1)||!Copy(&maximum,actor+0x5BD,1)||maximum!=200||
        !Copy(&status,actor+0x616,2)||!Copy(&hp,actor+0x5D0,4)||
        !Copy(&blockedA,actor+0xDCC,1)||!Copy(&blockedB,actor+0xDCE,1)||
        !Copy(&noCost,reinterpret_cast<void*>(moduleBase+0xD2A90C),1)||noCost)return Availability::Native;
    const bool nativeAllowed=hp>0&&(status&0x400u)==0&&blockedA==0&&blockedB==0;
    uint8_t cost=0;if(!Cost(id,id==282,&cost))return Availability::Native;
    Facts facts{true,false,true,id==282?(vanilla&3)!=0:Learned(id),nativeAllowed,3,id,charge,cost};
    Availability decision=Availability::Native;
    if(id==282) {
        std::array<Facts,12> children{};
        for(size_t i=0;i<children.size();++i) {
            const uint16_t child=static_cast<uint16_t>(104+i);uint8_t childCost=0;
            if(!Cost(child,false,&childCost))return Availability::Native;
            children[i]={true,false,true,Learned(child),nativeAllowed,3,child,charge,childCost};
        }
        decision=EvaluateHeader(facts,children);
    } else decision=Evaluate(facts).availability;
    return decision;
}
int __cdecl MenuReadyShim(int slot) {
    const auto original=reinterpret_cast<MenuReadyFn>(menuReadyOriginal);
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int vanilla=original?original(slot):0;
    // These two callers construct the input ring and the menu's blocked word.
    // Other readiness consumers include charge-clearing/gameplay paths; they
    // must retain the real full-gauge bit. No gauge or actor flag is spoofed.
    if(readiness.load()!=1||!requestedGameplay||storageFault.load()||slot!=kCharacter||
       actorThread.load()!=GetCurrentThreadId()||caller<moduleBase||
       (caller-moduleBase!=0x392BD3u&&caller-moduleBase!=0x39B6AEu))return vanilla;
    // The validated native presence reader stays unhooked: list membership
    // follows learned commands, while CostGateShim handles current usability.
    const auto availability=reinterpret_cast<AvailabilityFn>(moduleBase+0x39AD40u);
    const auto decision=DecideAvailability(static_cast<uint8_t>(slot),282,
                                           availability(static_cast<uint8_t>(slot),282));
    if(decision==Availability::Allow)return 1;
    if(decision==Availability::Block)return 0;
    return vanilla;
}
int __cdecl LeftEntryShim(int slot) {
    const auto original=reinterpret_cast<MenuReadyFn>(leftEntryOriginal);
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int vanilla=original?original(slot):0;
    if(readiness.load()!=1||!requestedGameplay||storageFault.load()||slot!=kCharacter||caller<moduleBase)
        return vanilla;
    const uint32_t callerRva=static_cast<uint32_t>(caller-moduleBase);
    bool knownCaller=false;
    for(uint32_t known:Evidence::kLeftEntryCallers)if(callerRva==known)knownCaller=true;
    if(!knownCaller)return vanilla;
    const DWORD nativeError=GetLastError(),thread=GetCurrentThreadId();
    int result=vanilla;
    const char* reason="native";
    uint8_t charge=0,maximum=0;
    auto* actor=PartyActor(static_cast<uint8_t>(slot));
    const bool sampled=actor&&Copy(&charge,actor+0x5BC,1)&&Copy(&maximum,actor+0x5BD,1);
    if((vanilla&2)==0) {
        if(!sampled||maximum!=200||activeActor.load()!=reinterpret_cast<uintptr_t>(actor)||actorThread.load()!=thread)
            reason="actor-or-thread";
        else {
            uintptr_t ring=0;std::array<uint16_t,8> headers{};
            constexpr uintptr_t rowOffset=3u*0x478u+0x28u;
            const bool row=Copy(&ring,reinterpret_cast<void*>(moduleBase+0x1F10CD8u),4)&&
                ring<=UINT32_MAX-rowOffset-sizeof(headers)&&
                DataRange(ring+rowOffset,sizeof(headers))&&
                Copy(headers.data(),reinterpret_cast<void*>(ring+rowOffset),sizeof(headers));
            bool found=false,foreign=false;
            if(row)for(uint16_t command:headers) {
                if(command==0x311A)found=true;
                else if(command!=0xFF) {
                    // Adding the costed-menu bit must not authorize another mod's
                    // paid left header. Native zero-cost neighbors remain intact.
                    uint8_t cost=0;const auto* entry=(command&0xF000u)==0x3000u?Command(command&0xFFFu):nullptr;
                    if(!entry||!DataRange(reinterpret_cast<uintptr_t>(entry),39)||!Copy(&cost,entry+38,1)||cost!=0)
                        foreign=true;
                }
            }
            uint16_t disabled=0;uint8_t headerCost=0;
            if(!row||!found||foreign)reason="left-header";
            else if(!Copy(&disabled,actor+0x690+(282/16)*2,2)||(disabled&(1u<<(282%16))))
                reason="native-disabled";
            else if(!Learned(282)||!Cost(282,true,&headerCost)||headerCost==0)reason="header-data";
            else if(DecideAvailability(static_cast<uint8_t>(slot),282,1)!=Availability::Allow)
                reason="restrictions-or-cost";
            else {result=vanilla|2;reason="affordable-ronso";}
        }
    }
    if(readiness.load()!=1)result=vanilla;
    // Only real LEFT presses are logged, never each renderer poll. +690 is a
    // disabled mask; actual learning comes from the saved command bank.
    if(logger&&(callerRva==0x49C9CBu||callerRva==0x49CEA8u)&&readiness.load()==1&&leftInputNotices.fetch_add(1)<12) {
        char line[320]={};
        std::snprintf(line,sizeof(line),"[ffx-hooks] RonsoPool left-input caller=0x%08X sampled=%u charge=%u max=%u native=%d result=%d thread=%u owner=%u reason=%s\n",
            callerRva,sampled?1u:0u,static_cast<unsigned>(charge),static_cast<unsigned>(maximum),vanilla,result,
            static_cast<unsigned>(thread),actorThread.load(),reason);
        Log(line);
    }
    SetLastError(nativeError);return result;
}
int __cdecl CostGateShim(int slot,const uint8_t* command,int extraMp) {
    const auto original=reinterpret_cast<CostGateFn>(costGateOriginal);
    if(!original)return -1;
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(readiness.load()!=1||!requestedGameplay||storageFault.load()||slot!=kCharacter||caller<moduleBase)
        return original(slot,command,extraMp);
    const uint32_t rva=static_cast<uint32_t>(caller-moduleBase);
    bool known=false;for(uint32_t site:Evidence::kCostGateCallers)if(site==rva)known=true;
    if(!known)return original(slot,command,extraMp);
    const DWORD callerError=GetLastError();
    std::array<uint8_t,96> view{};bool bypass=false;uint16_t id=UINT16_MAX;uint8_t charge=0,cost=0;
    auto* actor=PartyActor(static_cast<uint8_t>(slot));
    if(command&&actor&&activeActor.load()==reinterpret_cast<uintptr_t>(actor)&&actorThread.load()==GetCurrentThreadId()) {
        if(command==Command(282))id=282;
        else for(uint16_t candidate=104;candidate<=115;++candidate)if(command==Command(candidate))id=candidate;
        if(id!=UINT16_MAX&&Learned(id)&&Cost(id,id==282,&cost)&&
           Copy(&charge,actor+0x5BC,1)&&charge<200&&
           DecideAvailability(static_cast<uint8_t>(slot),static_cast<int16_t>(id),1)==Availability::Allow&&
           DataRange(reinterpret_cast<uintptr_t>(command),view.size())&&Copy(view.data(),command,view.size())) {
            // The native function conflates nonzero OD cost with a full gauge.
            // Its other checks/MP calculation still run on this call-local view.
            // Native confirmation retains its original command pointer and reads
            // the real OD cost into +6CD afterwards; no table or gauge is edited.
            view[38]=0;bypass=readiness.load()==1;
        }
    }
    SetLastError(callerError);
    const int result=original(slot,bypass?view.data():command,extraMp);
    const DWORD nativeError=GetLastError();
    auto& budget=rva==0x38AC65u?costCommitNotices:costUiNotices;
    if(bypass&&logger&&readiness.load()==1&&budget.fetch_add(1)<8) {
        char line[224]={};
        std::snprintf(line,sizeof(line),"[ffx-hooks] RonsoPool cost-gate caller=0x%08X command=0x%04X charge=%u realCost=%u mpResult=%d\n",
            rva,static_cast<unsigned>(0x3000u|id),static_cast<unsigned>(charge),static_cast<unsigned>(cost),result);
        Log(line);
    }
    SetLastError(nativeError);return result;
}
int __cdecl ResetShim() {
    const auto original=reinterpret_cast<ResetFn>(resetOriginal);
    if(readiness.load()==1) {
        const uint32_t epoch=stateEpoch.fetch_add(1)+1;
        activeActor.store(0);actorThread.store(0);storageFault.store(false);
        std::unique_lock<std::mutex> lock(stateMutex,std::try_to_lock);
        if(lock.owns_lock()){session={};sessionEpoch=epoch;actorOwner={};++actorGeneration;}
    }
    const int result=original?original():0;
    if(readiness.load()==1)NativeSaveEvents::ResetCompleted();
    return result;
}
void __cdecl AfterNativeMaximum(int slot,void* nativeActor) {
    if(readiness.load()!=1||!requestedGameplay||storageFault.load()||slot<0||slot>=8)return;
    auto* actor=PartyActor(static_cast<uint8_t>(slot));
    if(actor!=nativeActor||!DataRange(reinterpret_cast<uintptr_t>(actor),0xF90,true))return;
    activeActor.store(0);
    // This gateway runs inside the native initialization writer itself. A
    // bootstrap's thread ID is not authority over later native initializers.
    actorThread.store(GetCurrentThreadId());
    uint8_t original=0,charge=0;
    if(!Copy(&original,actor+0x5BD,1)||!Copy(&charge,actor+0x5BC,1)||
        original==0||original>200||charge>200)return;
    std::unique_lock<std::mutex> lock(stateMutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(readiness.load()!=1)return;
    actorOwner={++actorGeneration,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)),original,true,false};
    if(!ReplaceMaximum(actor,original,200)) {actorOwner.conflict=true;return;}
    uint8_t observed=0;
    if(!Copy(&observed,actor+0x5BD,1)||observed!=200){actorOwner.conflict=true;return;}
    const uint32_t epoch=stateEpoch.load();
    if(sessionEpoch!=epoch){session={};sessionEpoch=epoch;}
    if(!session.owned) {session.owned=true;session.originalMax=original;session.dormant=0;}
    activeActor.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)));
}
bool BuildMaximumStub() {
    std::vector<uint8_t> code(Evidence::kMaxInitialization+7,Evidence::kMaxInitialization+14);
    const auto emit=[&](std::initializer_list<uint8_t> bytes){code.insert(code.end(),bytes);};
    emit({0x9C,0x60,0x8B,0xD4,0x81,0xEC,0x20,0x02,0,0,0x83,0xE4,0xF0});
    emit({0x89,0x94,0x24,0,0x02,0,0,0x0F,0xAE,0x04,0x24});
    emit({0x56,0xFF,0x75,0x0C,0xE8});const size_t call=code.size();emit({0,0,0,0});
    emit({0x83,0xC4,0x08,0x0F,0xAE,0x0C,0x24,0x8B,0xA4,0x24,0,0x02,0,0,0x61,0x9D,0xE9});
    const size_t jump=code.size();emit({0,0,0,0});
    maxStub=static_cast<uint8_t*>(VirtualAlloc(nullptr,code.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!maxStub)return false;
    const uint32_t relativeCall=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&AfterNativeMaximum)-
        (reinterpret_cast<uintptr_t>(maxStub)+call+4));
    const uint32_t relativeJump=static_cast<uint32_t>((moduleBase+0x39B5BE)-
        (reinterpret_cast<uintptr_t>(maxStub)+jump+4));
    std::memcpy(code.data()+call,&relativeCall,4);std::memcpy(code.data()+jump,&relativeJump,4);
    std::memcpy(maxStub,code.data(),code.size());DWORD prior=0;
    if(!VirtualProtect(maxStub,code.size(),PAGE_EXECUTE_READ,&prior)||
        !FlushInstructionCache(GetCurrentProcess(),maxStub,code.size())) {
        VirtualFree(maxStub,0,MEM_RELEASE);maxStub=nullptr;return false;
    }
    return true;
}
bool PublishImport(uint32_t rva,void* expected,void* desired,ImportPatch* patch) noexcept {
    if(!ImageRange(moduleBase+rva,4,false))return false;
    auto* address=reinterpret_cast<void* volatile*>(moduleBase+rva);DWORD prior=0;
    if(!ProtectImport(moduleBase+rva,PAGE_READWRITE,&prior))return false;
    patch->originalProtection=prior;patch->protectionPending=true;
    const bool changed=InterlockedCompareExchangePointer(address,desired,expected)==expected;
    patch->owned=changed;
    DWORD ignored=0;const bool protectedAgain=ProtectImport(moduleBase+rva,prior,&ignored);
    if(protectedAgain)patch->protectionPending=false;
    return changed&&protectedAgain;
}
bool RestoreImport(uint32_t rva,void* replacement,void* original,ImportPatch* patch) noexcept {
    if(!patch->owned&&!patch->protectionPending)return true;
    if(!ImageRange(moduleBase+rva,4,false))return false;
    bool pointerRestored=!patch->owned;
    if(patch->owned) {
        DWORD previous=0;
        if(!ProtectImport(moduleBase+rva,PAGE_READWRITE,&previous))return false;
        if(!patch->protectionPending)patch->originalProtection=previous;
        patch->protectionPending=true;
        auto* address=reinterpret_cast<void* volatile*>(moduleBase+rva);
        pointerRestored=InterlockedCompareExchangePointer(address,original,replacement)==replacement;
        if(pointerRestored)patch->owned=false;
    }
    DWORD ignored=0;
    const bool pageRestored=ProtectImport(moduleBase+rva,patch->originalProtection,&ignored);
    if(pageRestored)patch->protectionPending=false;
    return pointerRestored&&pageRestored;
}
}
bool HasPersistentOwnership() noexcept {
    try {
        const auto directory=StorageDirectory();if(directory.empty())return false;
        WIN32_FIND_DATAW entry{};const auto mask=directory+L"\\*.bin";
        HANDLE find=FindFirstFileW(mask.c_str(),&entry);
        if(find==INVALID_HANDLE_VALUE)return false;
        FindClose(find);return true;
    } catch(...) {return false;}
}
bool PrepareRuntime(uintptr_t base,bool gameplay,LogFn log,PreparedRuntime* result,const wchar_t* overrideDirectory) {
    if(!result||preparedOnce||readiness.load()!=0)return false;
    *result={};moduleBase=base;logger=log;
    if(!Profile())return false;
    wchar_t legacy[8]={};
    if(GetEnvironmentVariableW(L"FFXHOOKS_RONSO_MANA_FORCE",legacy,8)>0&&legacy[0]!=L'0')return false;
    const auto directory=overrideDirectory?std::wstring(overrideDirectory):StorageDirectory();
    const bool observerRequested=NativeSaveEvents::Requested();
    if(!gameplay&&!overrideDirectory&&!HasPersistentOwnership()&&!observerRequested)return true;
    if((gameplay||observerRequested)&&!overrideDirectory) {
        const auto slash=directory.find_last_of(L"/\\");
        if(slash==std::wstring::npos)return false;
        const auto parent=directory.substr(0,slash);
        if(!CreateDirectoryW(parent.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    }
    if(!store.Initialize(directory,gameplay||observerRequested))return false;
    HMODULE crt=GetModuleHandleW(L"msvcr110.dll");if(!crt)return false;
    readOriginal=reinterpret_cast<ReadFn>(GetProcAddress(crt,"fread"));
    writeOriginal=reinterpret_cast<WriteFn>(GetProcAddress(crt,"fwrite"));
    fileNo=reinterpret_cast<FileNoFn>(GetProcAddress(crt,"_fileno"));
    osHandle=reinterpret_cast<OsHandleFn>(GetProcAddress(crt,"_get_osfhandle"));
    tell=reinterpret_cast<TellFn>(GetProcAddress(crt,"_ftelli64"));
    void* readEntry=nullptr;void* writeEntry=nullptr;
    if(!readOriginal||!writeOriginal||!fileNo||!osHandle||!tell||
        !Copy(&readEntry,reinterpret_cast<void*>(base+kReadImport),4)||
        !Copy(&writeEntry,reinterpret_cast<void*>(base+kWriteImport),4)||
        readEntry!=reinterpret_cast<void*>(readOriginal)||writeEntry!=reinterpret_cast<void*>(writeOriginal))return false;
    if(gameplay) {
        if(!BuildMaximumStub())return false;
        result->hooks[result->count++]={base+0x39B5B7,maxStub,&maxOriginal};
        result->hooks[result->count++]={base+0x39AF70,reinterpret_cast<void*>(&MenuReadyShim),&menuReadyOriginal};
        result->hooks[result->count++]={base+0x38F750,reinterpret_cast<void*>(&LeftEntryShim),&leftEntryOriginal};
        result->hooks[result->count++]={base+0x38C750,reinterpret_cast<void*>(&CostGateShim),&costGateOriginal};
    }
    result->hooks[result->count++]={base+0x386BC0,reinterpret_cast<void*>(&ResetShim),&resetOriginal};
    result->ioRequired=true;requestedGameplay=gameplay;preparedOnce=true;
    Log("[ffx-hooks] RonsoPool prepared; gameplay admission remains closed\n");
    return true;
}
bool InstallIoImports() noexcept {
    if(!preparedOnce||readiness.load()!=0)return false;
    if(!PublishImport(kReadImport,reinterpret_cast<void*>(readOriginal),reinterpret_cast<void*>(&ReadShim),&readPatch)) {
        RestoreIoImports();return false;
    }
    if(!PublishImport(kWriteImport,reinterpret_cast<void*>(writeOriginal),reinterpret_cast<void*>(&WriteShim),&writePatch)) {
        RestoreIoImports();return false;
    }
    return true;
}
bool RestoreIoImports() noexcept {
    const bool read=RestoreImport(kReadImport,reinterpret_cast<void*>(&ReadShim),reinterpret_cast<void*>(readOriginal),&readPatch);
    const bool write=RestoreImport(kWriteImport,reinterpret_cast<void*>(&WriteShim),reinterpret_cast<void*>(writeOriginal),&writePatch);
    return read&&write;
}
void ActivateRuntime() noexcept {uint32_t expected=0;readiness.compare_exchange_strong(expected,1);}
void RequestStop() noexcept {readiness.store(2);}
void DiscardUnpublishedRuntime() noexcept {
    if(readiness.load()==0&&maxStub){VirtualFree(maxStub,0,MEM_RELEASE);maxStub=nullptr;}
}
#ifdef FFXHOOKS_TESTING
void SetImportProtectionForFixture(ImportProtectFn fn) noexcept {protectForFixture=fn;}
#endif
}
