#include "EquipmentWorkshopNativeUi.h"
#include "EquipmentWorkshopRuntime.h"
#include "EquipmentWorkshopPresentationCore.h"
#include "NativeUiHookSupport.h"
#include "NativePresentationEvidence.h"
#include <atomic>
#include <intrin.h>

namespace FfxHooks::EquipmentWorkshop::NativeUi {
static std::atomic<EquipmentLayout> equipmentLayout{nullptr};
void SetEquipmentLayout(EquipmentLayout layout) noexcept {equipmentLayout.store(layout);}
static std::atomic<StatusObserver> statusObserver{nullptr};
void SetStatusObserver(StatusObserver observer) noexcept {statusObserver.store(observer);}
namespace {
enum Hook {Equipment,Customize,Battle,AbilityRow,AbilityDefinition,Shared,Inventory,StatusPage,StatusList,Frame,Count};
constexpr std::uint32_t rvas[Count]={0x4D02B0,0x4D63C0,0x4F34C0,0x4F4F10,0x3909C0,0x4D8A70,0x4BCFE0,0x4D2760,0x4D2DE0,0x4F5F70};
void* originals[Count]{};
std::uintptr_t module=0;
std::atomic<bool> active{false},attempted{false};
std::atomic<bool> installed{false};
static_assert(std::atomic<bool>::is_always_lock_free,"detach admission must be lock-free");
struct NamedRow {unsigned char row[108]{},text[192]{};};
struct Scope {
    Hook kind=Equipment;unsigned depth=0,cursor=0,measureCursor=0,drawCursor=0,nameSlot=10;
    bool admitted=false;workshop::Piece piece{};Presentation::GearView gear{};NamedRow names[10]{};
    unsigned statusWords[10]{},statusRanks[10]{},statusCount=0;
    float originY=0;bool hasOrigin=false;
};
thread_local Scope* scope=nullptr;
using EquipmentFn=int(__cdecl*)();
using CustomizeFn=int(__cdecl*)(unsigned);
using BattleFn=int(__cdecl*)(unsigned,float,float);
using RowFn=int(__cdecl*)(unsigned,float,float,unsigned);
using SharedFn=int(__cdecl*)(float,float,const unsigned char*);
using StatusFn=int(__cdecl*)(void*);
using ListFn=int(__cdecl*)(int,int,int,int,unsigned*);
using FrameFn=int(__cdecl*)(float,float,float,float,int);
using CharacterGearFn=unsigned(__cdecl*)(int);
using GetGearFn=const unsigned char*(__cdecl*)(unsigned,const unsigned char**);
using DefinitionFn=const unsigned char*(__cdecl*)(unsigned,const unsigned char**);
using ScaleFn=float(__cdecl*)(float);
float X(float value){return reinterpret_cast<ScaleFn>(module+0x244990)(value);}
float Y(float value){if(scope&&scope->kind==Equipment){if(auto layout=equipmentLayout.load())value=layout(value);}return reinterpret_cast<ScaleFn>(module+0x2449D0)(value);}
bool Enabled(){return active.load(std::memory_order_acquire)&&scope&&scope->depth<4;}
const unsigned char* GearAdapter(std::uintptr_t caller,const unsigned char* native){
    if(!Enabled()||!native)return native;
    const auto expected=scope->kind==Equipment?0x4D02D2u:scope->kind==Customize?0x4D63DDu:scope->kind==Battle?0x4F34D3u:scope->kind==Inventory?0x4BD130u:0u;
    if(caller!=expected||scope->admitted)return native;
    if(!ReadPresentation(native,scope->piece)||!Presentation::BuildGearView(scope->piece,scope->gear))return native;
    scope->admitted=true;return scope->gear.bytes;
}
const unsigned char* NameView(unsigned word,const unsigned char* native,const unsigned char** base){
    if(!Enabled()||!scope->admitted||scope->nameSlot>=10||!native||!base)return native;
    const unsigned slot=scope->nameSlot;
    const unsigned rank=scope->kind==StatusPage?scope->statusRanks[slot]:workshop::AbilityRank(scope->piece,slot);
    const unsigned expected=scope->kind==StatusPage?scope->statusWords[slot]:workshop::Ability(scope->piece,slot);
    if(!rank||expected!=(word&0xFFFF))return native;
    auto& view=scope->names[slot];const unsigned char* textBase=nullptr;
    if(!NativeUiSupport::Copy(view.row,native,108)||!NativeUiSupport::Copy(&textBase,base,sizeof(textBase))||!textBase)return native;
    const unsigned offset=view.row[0]|(unsigned(view.row[1])<<8);
    unsigned char source[181]{};std::size_t length=0;
    for(;length<sizeof(source);++length){if(!NativeUiSupport::Copy(source+length,textBase+offset+length,1))return native;if(!source[length])break;}
    if(length==sizeof(source)||!Presentation::AppendRank(source,length,rank,view.text,sizeof(view.text)))return native;
    view.row[0]=view.row[1]=0;*base=view.text;return view.row;
}
const unsigned char* __cdecl DefinitionShim(unsigned word,const unsigned char** base){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    const auto* native=reinterpret_cast<DefinitionFn>(originals[AbilityDefinition])(word,base);
    if(!Enabled()||!scope->admitted)return native;
    const auto saved=scope->nameSlot;
    if(scope->kind==Battle){
        if(caller==0x4F3526)scope->nameSlot=Presentation::NextSlot(scope->piece,static_cast<std::uint16_t>(word),scope->measureCursor);
        else if(caller==0x4F370B)scope->nameSlot=Presentation::NextSlot(scope->piece,static_cast<std::uint16_t>(word),scope->drawCursor);
        else return native;
    }else if(caller!=0x4F4F23)return native;
    const auto* result=NameView(word,native,base);scope->nameSlot=saved;return result;
}
int __cdecl RowShim(unsigned word,float x,float y,unsigned style){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(!Enabled()||!scope->admitted||!((scope->kind==Equipment&&caller==0x4D03C4)||(scope->kind==Customize&&caller==0x4D64F5)||
       (scope->kind==Shared&&caller==0x4D8B46)||(scope->kind==Inventory&&caller==0x4BD1C5)||(scope->kind==StatusPage&&caller==0x4D2AA2)))
        return reinterpret_cast<RowFn>(originals[AbilityRow])(word,x,y,style);
    const auto saved=scope->nameSlot;int result=0;
    if(scope->kind==StatusPage){
        scope->nameSlot=10;
        while(scope->cursor<scope->statusCount){const unsigned slot=scope->cursor++;
            if(scope->statusWords[slot]==word){scope->nameSlot=slot;break;}}
    }else scope->nameSlot=Presentation::NextSlot(scope->piece,static_cast<std::uint16_t>(word),scope->cursor);
    if((scope->kind==Shared||scope->kind==Inventory)&&scope->piece.fifthUnlocked&&scope->hasOrigin){
        const float offset=scope->kind==Shared?Y(2.f):0.f;
        y=scope->originY+(y-scope->originY+offset)*.8f-offset;
    }
    __try{result=reinterpret_cast<RowFn>(originals[AbilityRow])(word,x,y,style);}
    __finally{scope->nameSlot=saved;}
    return result;
}
void FifthLabel(){
    if(!Enabled()||!scope->admitted||!scope->piece.fifthUnlocked||scope->piece.fifth==workshop::Empty)return;
    // Native backgrounds already use the private capacity. Only these two
    // known label loops are fixed at four; the battle loop handles all five.
    const float y=scope->kind==Equipment?599.f+4*79.f:735.f+4*68.f-2.f;
    scope->nameSlot=4;
    reinterpret_cast<RowFn>(originals[AbilityRow])(scope->piece.fifth,X(970.f),Y(y),0);
    scope->nameSlot=10;
}
int __cdecl EquipmentShim(){
    if(!active.load())return reinterpret_cast<EquipmentFn>(originals[Equipment])();
    Scope current{};current.kind=Equipment;auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    __try{result=reinterpret_cast<EquipmentFn>(originals[Equipment])();FifthLabel();}
    __finally{scope=previous;}
    return result;
}
int __cdecl CustomizeShim(unsigned selected){
    if(!active.load())return reinterpret_cast<CustomizeFn>(originals[Customize])(selected);
    Scope current{};current.kind=Customize;auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    __try{result=reinterpret_cast<CustomizeFn>(originals[Customize])(selected);FifthLabel();}
    __finally{scope=previous;}
    return result;
}
int __cdecl BattleShim(unsigned gear,float x,float y){
    if(!active.load())return reinterpret_cast<BattleFn>(originals[Battle])(gear,x,y);
    Scope current{};current.kind=Battle;auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    __try{result=reinterpret_cast<BattleFn>(originals[Battle])(gear,x,y);}
    __finally{scope=previous;}
    return result;
}
#include "EquipmentWorkshopExtendedUi.inl"
}
bool Start(std::uintptr_t base,bool enabled,bool validateOnly,void(*log)(const char*)){
    if(!enabled||validateOnly)return false;
    if(attempted.load())return active.load();
    if(!NativeUiSupport::Profile(base,NativePresentationEvidence::equipment)){
        if(log)log("[ffx-hooks] Workshop native UI rejected: executable or drawing signatures differ\n");return false;}
    attempted=true;module=base;
    void* replacements[Count]={reinterpret_cast<void*>(&EquipmentShim),reinterpret_cast<void*>(&CustomizeShim),reinterpret_cast<void*>(&BattleShim),reinterpret_cast<void*>(&RowShim),reinterpret_cast<void*>(&DefinitionShim),reinterpret_cast<void*>(&SharedShim),reinterpret_cast<void*>(&InventoryShim),reinterpret_cast<void*>(&StatusShim),reinterpret_cast<void*>(&StatusListShim),reinterpret_cast<void*>(&FrameShim)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::EquipmentWorkshopUi,reinterpret_cast<const void*>(&Start)))return false;
    SetPresentationAdapter(GearAdapter);installed=true;active=true;
    if(log)log("[ffx-hooks] Workshop native UI: private fifth-row and ranked-name views enabled\n");return true;
}
void Stop() noexcept {active=false;}
bool Active() noexcept {return active.load();}
bool StatusBridgeInstalled(std::uintptr_t base) noexcept {return installed.load()&&module==base;}
#ifdef FFXHOOKS_TESTING
void FrameEnvironmentForTests(void* frame){if(active.load()&&frame)originals[Frame]=frame;}
#endif
}
