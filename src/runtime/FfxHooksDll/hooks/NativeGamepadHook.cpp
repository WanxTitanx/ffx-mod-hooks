// Jarvis-HOOK. Native XInput actions/remapping; no external controller plugin.
#include "NativeGamepadHook.h"
#include "NativeBindingsCore.h"
#include "F8RuntimeCore.h"
#include "../shared/Config.h"
#include <atomic>
#include <cstring>

namespace FfxHooks::NativeGamepad {
namespace {
using GetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
constexpr std::uintptr_t kStateImportRva=0x0070C718u;
std::atomic<bool> g_stop{true},g_owned{false},g_conflict{false};
GetStateFn g_original=nullptr;
void* volatile* g_import=nullptr;
alignas(8) volatile LONG64 g_map=0x9876543210LL;
std::atomic<DWORD> g_revision{0};
std::atomic<int> g_controller{-1},g_shortcutController{-1};
std::array<std::atomic<std::uint32_t>,static_cast<unsigned>(NativeBindings::Action::Count)> g_shortcuts{};
#ifdef FFXHOOKS_TESTING
GetStateFn g_testReader=nullptr;
#endif
std::uint64_t Pack(const ButtonMap& map){std::uint64_t result=0;for(unsigned i=0;i<map.size();++i)result|=std::uint64_t(map[i])<<(i*4);return result;}
ButtonMap Unpack(std::uint64_t value){ButtonMap map{};for(unsigned i=0;i<map.size();++i)map[i]=static_cast<unsigned char>((value>>(i*4))&15u);return map;}
ButtonMap ConfiguredMap(){
    ButtonMap map=IdentityMap();
    const char* value=Config::GetString("gamepad.button_map","0123456789");
    if(!value || std::strlen(value)!=map.size())return map;
    for(unsigned i=0;i<map.size();++i){if(value[i]<'0'||value[i]>'9')return IdentityMap();map[i]=static_cast<unsigned char>(value[i]-'0');}
    return ValidMap(map)?map:IdentityMap();
}
DWORD ReadOriginal(DWORD controller,XINPUT_STATE* state){
#ifdef FFXHOOKS_TESTING
    if(g_testReader)return g_testReader(controller,state);
#endif
    return g_original?g_original(controller,state):ERROR_DEVICE_NOT_CONNECTED;
}
DWORD WINAPI RemappedGetState(DWORD controller,XINPUT_STATE* state){
    const int configured=g_controller.load();
    const DWORD physicalController=!g_stop && configured>=0?static_cast<DWORD>(configured):controller;
    const DWORD result=ReadOriginal(physicalController,state);
    if(result==ERROR_SUCCESS && state && !g_stop){
        const auto physical=Buttons(state->Gamepad.wButtons,state->Gamepad.bLeftTrigger,state->Gamepad.bRightTrigger);
        // Only the shortcut reader's controller owns its chord. Other pads must
        // not lose ordinary buttons for an action they cannot trigger.
        for(const auto& shortcut:g_shortcuts)if(g_shortcutController.load()==static_cast<int>(physicalController) && shortcut.load()!=0 && shortcut.load()==physical){
            state->Gamepad.wButtons=0;
            if(physical&LeftTrigger)state->Gamepad.bLeftTrigger=0;
            if(physical&RightTrigger)state->Gamepad.bRightTrigger=0;
            break;
        }
        const auto map=Unpack(static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_map,0,0)));
        state->Gamepad.wButtons=Remap(state->Gamepad.wButtons,map);
        // Mapping edits must invalidate the game's cached packet even while the
        // physical controller is held still. Analog axes and triggers are intact.
        state->dwPacketNumber+=g_revision.load();
    }
    return result;
}
bool ExchangeImport(void* before,void* after){
    if(!g_import)return false;
    DWORD old=0;
    if(!VirtualProtect(const_cast<void**>(g_import),sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool changed=InterlockedCompareExchangePointer(g_import,after,before)==before;
    DWORD ignored=0;VirtualProtect(const_cast<void**>(g_import),sizeof(void*),old,&ignored);
    return changed;
}
bool Profile(std::uintptr_t base){
    __try {
        const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(!base || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
        const auto* pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        return pe->Signature==IMAGE_NT_SIGNATURE && F8Runtime::IsSupportedExecutable({pe->FileHeader.Machine,pe->OptionalHeader.Magic,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage});
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool Start(std::uintptr_t base){
    if(!g_stop)return true;
    if(!Profile(base))return false;
    HMODULE xinput=GetModuleHandleW(L"xinput9_1_0.dll");
    auto original=reinterpret_cast<GetStateFn>(xinput?GetProcAddress(xinput,"XInputGetState"):nullptr);
    if(!original)return false;
    auto* slot=reinterpret_cast<void* volatile*>(base+kStateImportRva);
    __try {if(*slot!=reinterpret_cast<void*>(original))return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    g_import=slot;g_original=original;g_stop=false;g_conflict=false;
    RefreshMapping();return true;
}
void RequestStop(){g_stop=true;}
void Stop(){
    RequestStop();
    if(g_owned && ExchangeImport(reinterpret_cast<void*>(&RemappedGetState),reinterpret_cast<void*>(g_original)))g_owned=false;
    else if(g_owned)g_conflict=true;
}
Snapshot Poll(){
    Snapshot result{};result.available=!g_stop && g_original;
    if(!result.available)return result;
    const int configured=g_controller.load();
    const unsigned first=configured>=0 && configured<=3?static_cast<unsigned>(configured):0u;
    const unsigned last=configured>=0 && configured<=3?first:3u;
    for(unsigned index=first;index<=last;++index){
        XINPUT_STATE state{};
        if(ReadOriginal(index,&state)==ERROR_SUCCESS){g_shortcutController=static_cast<int>(index);result.connected=true;result.controller=index;result.buttons=Buttons(state.Gamepad.wButtons,state.Gamepad.bLeftTrigger,state.Gamepad.bRightTrigger);return result;}
    }
    g_shortcutController=-1;
    return result;
}
ButtonMap Mapping(){return ConfiguredMap();}
bool SaveMapping(const ButtonMap& map){
    if(!ValidMap(map))return false;
    char value[11]{};for(unsigned i=0;i<map.size();++i)value[i]=static_cast<char>('0'+map[i]);
    if(!Config::SetString("gamepad.button_map",value))return false;
    RefreshMapping();return true;
}
bool MappingApplied(){return g_owned && !g_stop && !g_conflict;}
bool MappingConflict(){return g_conflict;}
void RefreshMapping(){
    if(g_stop || !g_original)return;
    const int requested=Config::GetInt("gamepad.controller",-1);
    const int controller=requested>=0 && requested<=3?requested:-1;
    if(g_controller.exchange(controller)!=controller){g_shortcutController=-1;++g_revision;}
    bool shortcuts=false;
    for(unsigned i=0;i<g_shortcuts.size();++i){
        char key[64]{};std::snprintf(key,sizeof(key),"gamepad.%s",NativeBindings::Key(static_cast<NativeBindings::Action>(i))+9);
        const int configured=Config::GetInt(key,0);const auto mask=configured>=0 && ValidCombo(static_cast<std::uint32_t>(configured))?static_cast<std::uint32_t>(configured):0u;
        if(g_shortcuts[i].exchange(mask)!=mask)++g_revision;
        shortcuts=shortcuts||mask!=0;
    }
    const auto map=ConfiguredMap();const auto packed=static_cast<LONG64>(Pack(map));
    if(InterlockedExchange64(&g_map,packed)!=packed)++g_revision;
    const bool required=Changed(map)||shortcuts||controller>=0;
    if(g_owned && *g_import!=reinterpret_cast<void*>(&RemappedGetState)){g_conflict=true;return;}
    if(required && !g_owned){
        HMODULE own=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&RemappedGetState),&own))return;
        g_owned=ExchangeImport(reinterpret_cast<void*>(g_original),reinterpret_cast<void*>(&RemappedGetState));g_conflict=!g_owned;
    } else if(!required && g_owned){
        if(ExchangeImport(reinterpret_cast<void*>(&RemappedGetState),reinterpret_cast<void*>(g_original))){g_owned=false;g_conflict=false;}else g_conflict=true;
    }
}
#ifdef FFXHOOKS_TESTING
void SetStateReaderForTests(StateReader reader){g_testReader=reader;}
#endif
}
