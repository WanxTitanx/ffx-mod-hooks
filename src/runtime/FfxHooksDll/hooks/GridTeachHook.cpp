#include "GridTeachHook.h"
#include "KimahriLancetDualGrantHook.h"
#include "GridLearnedRuntime.h"
#include "NulElementCommands.h"
#include "ModFeatureCatalog.h"
#include "RecoveryNative.h"
#include "../shared/ffx_addresses.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace FfxHooks {
namespace {
namespace Learning=GridLearned::Runtime;
using GrantFn=int(__cdecl*)(int,int,int);
using BuildFn=int(__cdecl*)(int,int);
using HasFn=int(__cdecl*)(std::uint8_t,std::uint16_t);
using CommandFn=std::uint8_t*(__cdecl*)(std::int16_t,int);
std::uintptr_t module=0;
CommandFn commandEntry=nullptr;
void* grantOriginal=nullptr;
void* buildOriginal=nullptr;
void* hasOriginal=nullptr;
RecoveryNative::OwnedBatch batch;
std::array<void*,8> limitGateways{},limitOriginals{};
std::atomic<unsigned> admission{0};
std::atomic<bool> attempted{false},installed{false};
GridTeachLogFn logger=nullptr;
bool nulSpellMenus=false;
thread_local unsigned dualGrantDepth=0;
SRWLOCK lifecycle=SRWLOCK_INIT;
struct LifecycleLock {
    bool held=TryAcquireSRWLockExclusive(&lifecycle)!=FALSE;
    ~LifecycleLock(){if(held)ReleaseSRWLockExclusive(&lifecycle);}
};
bool Admitted() noexcept {return admission.load(std::memory_order_acquire)==1;}
void Log(const char* line){if(logger)logger(line);}
int Normalize(int command) noexcept {
    if(command==0||(command&0xfffff000)==0x3000)return command;
    const auto id=command&0xfff;
    return id>=1&&id<=0x3ff?0x3000|id:command;
}
bool EntryAllows(unsigned character,unsigned command) noexcept {
    if(character>=7||command>GridLearned::kLastCommand||!commandEntry)return false;
    std::uint8_t* row=nullptr;std::uint8_t* zero=nullptr;
    __try {row=commandEntry(static_cast<std::int16_t>(command),0);zero=commandEntry(0,0);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    std::uint8_t owner=0;
    // The native resolver falls back to row zero for a missing command. Such
    // fallback is not evidence that an extended ability exists.
    if(!row||(command!=0&&row==zero)||!RecoveryNative::Range(reinterpret_cast<std::uintptr_t>(row),92)||
       !RecoveryNative::Copy(&owner,row+25,1))return false;
    return owner==0xff||owner==character;
}
bool AuthoredNul(unsigned command) noexcept {
    const auto* definition=NulElements::Find(0x3000u|command);if(!definition||!commandEntry)return false;
    std::array<std::uint8_t,96> row{};
    __try {const auto* source=commandEntry(static_cast<std::int16_t>(command),0);
        return source&&RecoveryNative::Copy(row.data(),source,row.size())&&NulElements::Canonical(*definition,row.data(),row.size());
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NulLearned(unsigned character) noexcept {
    if(!nulSpellMenus||character!=FFX_CHARACTER_YUNA)return false;
    for(const auto& command:NulElements::Commands)
        if(Learning::Has(character,command.id)&&EntryAllows(character,command.id)&&AuthoredNul(command.id))return true;
    return false;
}
bool InRange(const void* returnAddress,std::uintptr_t low,std::uintptr_t high) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(returnAddress);
    return address>=module+low&&address<module+high;
}
bool FromGrid(const void* direct) noexcept {
    if(InRange(direct,RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_LO,RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_HI))return true;
    // The OS walker replaces the old arbitrary EBP-chain dereferences. It is
    // bounded and observes provenance only; the risky replay function is not hooked.
    void* frames[16]{};
    const USHORT count=CaptureStackBackTrace(0,16,frames,nullptr);
    for(USHORT i=0;i<count;++i)
        if(InRange(frames[i],RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_LO,RVA_FFX_SPHERE_GRID_NODE_ACTIVATE_HI))return true;
    return false;
}
void RonsoGridBit(unsigned character,unsigned id,bool on) noexcept {
    if(character!=FFX_CHARACTER_KIMAHRI||id<FFX_CMD_RONSO_RAGE_ID_MIN||id>FFX_CMD_RONSO_RAGE_ID_MAX)return;
    const auto address=module+RVA_FFX_KIMAHRI_RONSO_UNLOCK;
    if(!RecoveryNative::Range(address,2,module,false,true))return;
    const auto mask=static_cast<std::uint16_t>(1u<<(id-FFX_CMD_RONSO_RAGE_ID_MIN));
    __try {
        auto* location=reinterpret_cast<volatile SHORT*>(address);
        SHORT previous=*location;
        for(unsigned attempt=0;attempt<4;++attempt){
            const auto next=static_cast<SHORT>(on?static_cast<std::uint16_t>(previous)|mask:
                static_cast<std::uint16_t>(previous)&~mask);
            const SHORT actual=_InterlockedCompareExchange16(location,next,previous);
            if(actual==previous)return;previous=actual;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

int __cdecl GrantShim(int character,int command,int on){
    const auto original=reinterpret_cast<GrantFn>(grantOriginal);
    if(!Admitted()||command==0)return original(character,command,on);
    const int normalized=Normalize(command),id=normalized&0xfff;
    const bool encoded=(normalized&0xfffff000)==0x3000;
    const bool fromGrid=encoded&&FromGrid(_ReturnAddress());
    const bool dual=dualGrantDepth&&character==FFX_CHARACTER_KIMAHRI&&
        (id==FFX_CMD_BLUE_MAGIC_MENU_ID||(id>=FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST&&
         id<=FFX_CMD_KIMAHRI_BLUE_MAGE_FIRST+(FFX_CMD_RONSO_RAGE_ID_MAX-FFX_CMD_RONSO_RAGE_ID_MIN)));
    if(encoded&&id>=320){
        if(character<0||!EntryAllows(static_cast<unsigned>(character),static_cast<unsigned>(id))||!Learning::Ready())return 0;
        if(!fromGrid&&!dual)return Learning::Has(static_cast<unsigned>(character),static_cast<unsigned>(id))?1:0;
        const auto change=Learning::Set(static_cast<unsigned>(character),static_cast<unsigned>(id),on!=0);
        return change==GridLearned::Change::Changed||change==GridLearned::Change::Unchanged?1:0;
    }
    if(fromGrid&&character>=0&&id>=96&&!EntryAllows(static_cast<unsigned>(character),static_cast<unsigned>(id)))return 0;
    const int result=original(character,fromGrid?normalized:command,on);
    if(encoded&&result&&fromGrid&&character>=0&&id>=96&&Learning::Ready()){
        Learning::Set(static_cast<unsigned>(character),static_cast<unsigned>(id),on!=0);
        RonsoGridBit(static_cast<unsigned>(character),static_cast<unsigned>(id),on!=0);
    }
    if(encoded&&on&&result&&!fromGrid&&character==FFX_CHARACTER_KIMAHRI&&
       id>=FFX_CMD_RONSO_RAGE_ID_MIN&&id<=FFX_CMD_RONSO_RAGE_ID_MAX&&
       IsKimahriLancetDualGrantHookInstalled()&&Learning::Ready()){
        struct Scope {Scope(){++dualGrantDepth;}~Scope(){--dualGrantDepth;}} scope;
        KimahriLancetDualGrantOnRonsoLearn(character,id,result,_ReturnAddress(),&GrantShim);
    }
    return result;
}
int __cdecl HasShim(std::uint8_t character,std::uint16_t command){
    const auto original=reinterpret_cast<HasFn>(hasOriginal);
    if(!Admitted())return original(character,command);
    const unsigned id=command&0xfff;
    if((command&0xf000)==0x3000&&id>=320){
        if(AuthoredNul(id)&&!nulSpellMenus)return 0;
        if(id==FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID&&NulLearned(character)&&EntryAllows(character,id))return 1;
        return EntryAllows(character,id)&&Learning::Has(character,id)?1:0;
    }
    const int vanilla=original(character,command);
    if(vanilla&1)return vanilla;
    return id>=96&&EntryAllows(character,id)&&Learning::Has(character,id)?1:vanilla;
}
// Save reset at RVA 0x386BC0 belongs to RonsoPool's existing save-I/O owner.
// Learning observes its ResetCompleted event; it neither installs a second
// entry detour nor suppresses the native reset based on a historical caller label.
bool Empty(std::uint16_t code) noexcept {return code==0||code==0xff||code==0xffff;}
void Append(std::uint16_t* ring,unsigned count,unsigned id){
    for(unsigned i=0;i<count;++i)if(!Empty(ring[i])&&(ring[i]&0xfff)==id)return;
    for(unsigned i=0;i<count;++i)if(Empty(ring[i])){ring[i]=static_cast<std::uint16_t>(0x3000|id);return;}
}
bool Misplaced(unsigned character,unsigned offset,unsigned id){
    if(id>=FFX_CMD_RONSO_RAGE_ID_MIN&&id<=FFX_CMD_RONSO_RAGE_ID_MAX)
        return character!=FFX_CHARACTER_KIMAHRI||offset!=FFX_BATTLE_COMMAND_RING_OD_OFFSET;
    if(id==FFX_CMD_BLUE_MAGIC_MENU_ID)return character!=FFX_CHARACTER_KIMAHRI||offset!=0;
    if(id>=FFX_CMD_BLUE_MAGIC_CHILD_MIN&&id<=FFX_CMD_BLUE_MAGIC_CHILD_MAX)
        return character!=FFX_CHARACTER_KIMAHRI||offset!=FFX_BATTLE_COMMAND_RING_SPECIAL_OFFSET;
    if(id==FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID)return character!=FFX_CHARACTER_YUNA||offset!=0;
    if(id>=FFX_CMD_YUNA_WM_PLUS_CHILD_MIN&&id<=FFX_CMD_YUNA_WM_PLUS_CHILD_MAX)
        return character!=FFX_CHARACTER_YUNA||offset!=FFX_BATTLE_COMMAND_RING_OD_OFFSET;
    if(NulElements::Find(0x3000u|id))return character!=FFX_CHARACTER_YUNA||offset!=FFX_BATTLE_COMMAND_RING_OD_OFFSET;
    return id>=320&&!EntryAllows(character,id);
}
void UpdateExtendedMenu(unsigned character){
    if(character>=7||!Learning::Ready())return;
    std::uintptr_t table=0;
    if(!RecoveryNative::Copy(&table,reinterpret_cast<void*>(module+RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR),4)||!table)return;
    const auto tree=table+std::uintptr_t(character)*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE;
    constexpr struct Segment {unsigned offset,count;} segments[]={
        {0,FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT},{40,8},{56,8},{72,24},{120,24},{168,32},{232,32},
        {FFX_BATTLE_COMMAND_RING_OD_OFFSET,FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT}};
    for(const auto& segment:segments)
        if(!RecoveryNative::Range(tree+segment.offset,segment.count*2,0,false,true))return;
    __try {
        for(const auto& segment:segments){
            auto* ring=reinterpret_cast<std::uint16_t*>(tree+segment.offset);
            for(unsigned i=0;i<segment.count;++i)if(!Empty(ring[i])&&Misplaced(character,segment.offset,ring[i]&0xfff))ring[i]=0xff;
        }
        auto* main=reinterpret_cast<std::uint16_t*>(tree);
        if(character==FFX_CHARACTER_KIMAHRI){
            if(Learning::Has(character,FFX_CMD_BLUE_MAGIC_MENU_ID)&&EntryAllows(character,FFX_CMD_BLUE_MAGIC_MENU_ID))Append(main,FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT,FFX_CMD_BLUE_MAGIC_MENU_ID);
            auto* special=reinterpret_cast<std::uint16_t*>(tree+FFX_BATTLE_COMMAND_RING_SPECIAL_OFFSET);
            for(unsigned id=FFX_CMD_BLUE_MAGIC_CHILD_MIN;id<=FFX_CMD_BLUE_MAGIC_CHILD_MAX;++id)
                if(Learning::Has(character,id)&&EntryAllows(character,id))Append(special,FFX_BATTLE_COMMAND_RING_SPECIAL_SLOT_COUNT,id);
        }else if(character==FFX_CHARACTER_YUNA){
            if(Learning::Has(character,FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID)&&EntryAllows(character,FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID))Append(main,FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT,FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID);
            auto* special=reinterpret_cast<std::uint16_t*>(tree+FFX_BATTLE_COMMAND_RING_OD_OFFSET);
            for(unsigned id=FFX_CMD_YUNA_WM_PLUS_CHILD_MIN;id<=FFX_CMD_YUNA_WM_PLUS_CHILD_MAX;++id)
                if(Learning::Has(character,id)&&EntryAllows(character,id))Append(special,FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT,id);
            bool hasNul=false;
            for(const auto& command:NulElements::Commands)if(nulSpellMenus&&Learning::Has(character,command.id)&&EntryAllows(character,command.id)&&AuthoredNul(command.id)){
                Append(special,FFX_BATTLE_COMMAND_RING_OD_SLOT_COUNT,command.id);hasNul=true;
            }
            if(hasNul&&EntryAllows(character,FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID))
                Append(main,FFX_BATTLE_COMMAND_RING_DEFAULT_COUNT,FFX_CMD_WHITE_MAGIC_PLUS_MENU_ID);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
int __cdecl BuildShim(int character,int actor){
    const int result=reinterpret_cast<BuildFn>(buildOriginal)(character,actor);
    if(Admitted()&&character>=0)UpdateExtendedMenu(static_cast<unsigned>(character));
    return result;
}
void Discard() noexcept {
    if(!batch.DiscardUnpublished())return;
    for(auto& gateway:limitGateways)if(gateway){VirtualFree(gateway,0,MEM_RELEASE);gateway=nullptr;}
}
bool Validate(std::uintptr_t base){
    using namespace RecoveryEvidence;
    if(!RecoveryNative::Profile(base))return false;
    const Proof proofs[]={GridGrant,GridBuildMenu,GridHasCommand,GridCommandEntry};
    for(const auto& proof:proofs)if(!RecoveryNative::Match(base,proof))return false;
    for(const auto& limit:menuLimits){
        const Proof proof{limit.rva,limit.bytes,limit.size,nullptr,0};
        if(limit.size<5||limit.size>6||limit.immediate+4!=limit.size||!RecoveryNative::Match(base,proof))return false;
    }
    return true;
}
} // namespace

bool StartGridTeachSaveTracking(std::uintptr_t base,GridTeachLogFn log){return Coexistence::runtime.SavePipelineAllowed()&&GridLearned::Runtime::Start(base,log);}
bool StartNativeSaveLoadEvents(std::uintptr_t module){return Coexistence::runtime.SavePipelineAllowed()&&GridLearned::Runtime::StartLoadEvents(module);}
bool NativeSaveLoadEventsReady() noexcept {return GridLearned::Runtime::loadEventsReady.load(std::memory_order_acquire);}
void RequestNativeSaveLoadEventsStop() noexcept {GridLearned::Runtime::RequestLoadEventsStop();}
bool RemoveNativeSaveLoadEvents() noexcept {return GridLearned::Runtime::StopLoadEvents();}
bool IsGridTeachLearningReady(){return IsGridTeachHookInstalled()&&GridLearned::Runtime::Ready();}
void RequestGridTeachStop() noexcept {
    admission.store(2,std::memory_order_release);installed.store(false,std::memory_order_release);
    Learning::RequestStop();
}

GridTeachInstallResult InstallGridTeachHook(std::uintptr_t base,GridTeachLogFn log){
    GridTeachInstallResult result{};
    if(RecoveryNative::EnvironmentEnabled("FFXHOOKS_VALIDATE_ONLY"))return result;
    LifecycleLock lock;if(!lock.held)return result;
    if(installed.load()){result.ok=true;result.menuBoundPatched=true;result.menuBoundPatchVa=base+RecoveryEvidence::menuLimits[0].rva;return result;}
    if(attempted.exchange(true)||admission.load()==2)return result;
    module=base;logger=log;nulSpellMenus=ModFeatures::Enabled(ModFeatures::Feature::NulSpells);
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if(!Validate(base)||!StartGridTeachSaveTracking(base,log)||!Learning::PublisherReady()||
       MinHookBatch::EnsureProcessInitialized()!=MinHookBatch::InitializationResult::Ready){
        Log("[ffx-hooks] GridTeach unavailable: signature, profile or save producer not ready\n");return result;
    }
    commandEntry=reinterpret_cast<CommandFn>(base+RecoveryEvidence::GridCommandEntry.rva);
    const struct Target {std::uint32_t rva;void* replacement;void** original;} targets[]={
        {RecoveryEvidence::GridGrant.rva,reinterpret_cast<void*>(&GrantShim),&grantOriginal},
        {RecoveryEvidence::GridBuildMenu.rva,reinterpret_cast<void*>(&BuildShim),&buildOriginal},
        {RecoveryEvidence::GridHasCommand.rva,reinterpret_cast<void*>(&HasShim),&hasOriginal}};
    for(const auto& target:targets)if(!batch.Add(base+target.rva,target.replacement,target.original)){Discard();return result;}
    std::size_t index=0;
    for(const auto& limit:RecoveryEvidence::menuLimits){
        auto* code=static_cast<std::uint8_t*>(RecoveryNative::AllocateCode(limit.size+5));
        if(!code){Discard();return result;}limitGateways[index]=code;
        std::memcpy(code,limit.bytes,limit.size);
        const std::uint32_t extended=FFX_BATTLE_MENU_COMMAND_ID_LIMIT_EXTENDED;
        std::memcpy(code+limit.immediate,&extended,4);code[limit.size]=0xe9;
        const auto relative=static_cast<std::uint32_t>(base+limit.rva+limit.size-reinterpret_cast<std::uintptr_t>(code)-limit.size-5);
        std::memcpy(code+limit.size+1,&relative,4);
        if(!RecoveryNative::SealCode(code,limit.size+5)||!batch.Add(base+limit.rva,code,&limitOriginals[index])){Discard();return result;}
        ++index;
    }
    if(!batch.Publish(MinHookBatch::Owner::GridTeachRecovery,reinterpret_cast<const void*>(&InstallGridTeachHook))){RequestGridTeachStop();Discard();return result;}
    unsigned closed=0;
    if(!admission.compare_exchange_strong(closed,1)){batch.Neutralize();return result;}
    installed=true;result.ok=true;result.menuBoundPatched=true;result.menuBoundPatchVa=base+RecoveryEvidence::menuLimits[0].rva;
    Log("[ffx-hooks] GridTeach ready: complete owned batch; save/character-bound extended commands; no global learned-bank writer\n");
#else
    Log("[ffx-hooks] GridTeach unavailable without shared MinHook\n");
#endif
    return result;
}
bool RemoveGridTeachHook(GridTeachLogFn log){
    RequestGridTeachStop();LifecycleLock lock;if(!lock.held)return false;
    const bool native=batch.Neutralize();
    const bool saves=GridLearned::Runtime::Stop();
    if(!batch.RetainsCode())Discard();
    if(log)log(native&&saves?"[ffx-hooks] GridTeach disabled; published callbacks retained until process exit\n":
        "[ffx-hooks] GridTeach stopped with retained native ownership; restart required\n");
    return native&&saves;
}
bool IsGridTeachHookInstalled(){return installed.load(std::memory_order_acquire)&&Admitted();}
} // namespace FfxHooks
