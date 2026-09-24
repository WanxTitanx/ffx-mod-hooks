#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "../hooks/NativeGamepadHook.h"
#include "../shared/Config.h"
namespace G=FfxHooks::NativeGamepad;
namespace C=FfxHooks::Config;
static int checks,failures;
static DWORD connectedController=0;
static bool allControllers=false;
static bool Persist(void*,const char*,const char*){return true;}
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
static DWORD WINAPI Physical(DWORD index,XINPUT_STATE* state){
    if(index!=connectedController && !allControllers)return ERROR_DEVICE_NOT_CONNECTED;
    *state={};state->dwPacketNumber=77;state->Gamepad.wButtons=G::A;
    state->Gamepad.bLeftTrigger=75;state->Gamepad.sThumbLX=500;state->Gamepad.sThumbRY=-12;return ERROR_SUCCESS;
}
static DWORD WINAPI Foreign(DWORD,XINPUT_STATE*){return ERROR_DEVICE_NOT_CONNECTED;}
int main(){
    C::ResetForTests();C::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
    Check(C::LoadTextForTests("[gamepad]\ncontroller=-1\nbutton_map=0123456789\n","gamepad-test.ini"),"isolated config initializes");
    HMODULE module=LoadLibraryExW(L"xinput9_1_0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check(module!=nullptr,"Windows controller API is available");
    if(!module)return 2;
    auto original=GetProcAddress(module,"XInputGetState");
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x0237D000u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* pe=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);pe->Signature=IMAGE_NT_SIGNATURE;
    pe->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;pe->OptionalHeader.Magic=0x10B;
    pe->FileHeader.TimeDateStamp=0x55D2F3CC;pe->OptionalHeader.SizeOfImage=0x0237D000;
    auto* slot=reinterpret_cast<void**>(image+0x70C718);*slot=reinterpret_cast<void*>(original);
    Check(!G::Start(0),"wrong executable profile cannot install");
    Check(G::Start(reinterpret_cast<uintptr_t>(image)),"supported import identity is admitted");
    Check(*slot==reinterpret_cast<void*>(original) && !G::MappingApplied(),"default identity installs no writer");
    G::SetStateReaderForTests(Physical);
    const auto physical=G::Poll();
    Check(physical.connected && physical.controller==0 && physical.buttons==(G::A|G::LeftTrigger),"shortcut provider reads physical buttons and trigger threshold");
    auto map=G::IdentityMap();G::SwapDestination(map,0,1);
    Check(G::SaveMapping(map) && G::MappingApplied(),"opt-in remapping is saved and applied");
    const void* owned=*slot;XINPUT_STATE remapped{};
    auto get=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(*slot);
    Check(get(0,&remapped)==ERROR_SUCCESS && remapped.Gamepad.wButtons==G::B,"game import receives the requested mapping");
    Check(remapped.Gamepad.bLeftTrigger==75 && remapped.Gamepad.sThumbLX==500 && remapped.Gamepad.sThumbRY==-12,"analog input remains byte-exact");
    Check(remapped.dwPacketNumber!=77,"mapping revision invalidates a cached physical packet");
    Check(G::Poll().buttons==physical.buttons,"shortcut capture remains physical after remapping");
    Check(G::SaveMapping(G::IdentityMap()) && *slot==reinterpret_cast<void*>(original),"identity restores the exact original import");
    Check(C::SetInt("gamepad.menu_f8",G::A|G::LeftTrigger),"private fixture assigns an explicit controller shortcut");G::RefreshMapping();
    get=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(*slot);get(0,&remapped);
    Check(remapped.Gamepad.wButtons==0 && remapped.Gamepad.bLeftTrigger==0 && G::Poll().buttons==physical.buttons,
        "hotkey combinations reach the shortcut owner while being consumed before underlying game actions");
    allControllers=true;get(1,&remapped);
    Check(remapped.Gamepad.wButtons==G::A && remapped.Gamepad.bLeftTrigger==75,
        "automatic shortcut ownership does not swallow the second controller's normal input");
    allControllers=false;
    C::SetInt("gamepad.menu_f8",0);G::RefreshMapping();
    Check(*slot==reinterpret_cast<void*>(original),"clearing the last chord releases an otherwise unnecessary input hook");
    Check(C::SetInt("gamepad.controller",1),"a specific controller can be selected");G::RefreshMapping();
    Check(G::MappingApplied(),"explicit controller selection admits routing even with identity mapping");
    connectedController=1;get=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(const_cast<void*>(owned));
    Check(get(0,&remapped)==ERROR_SUCCESS && remapped.Gamepad.wButtons==G::A && G::Poll().controller==1,
        "game reads and shortcut reads use the same explicitly selected controller");
    connectedController=0;
    Check(get(0,&remapped)==ERROR_DEVICE_NOT_CONNECTED && !G::Poll().connected,
        "disconnecting the selected controller does not silently switch to another pad");
    C::SetInt("gamepad.controller",-1);G::RefreshMapping();
    Check(*slot==reinterpret_cast<void*>(original) && G::Poll().controller==0,
        "returning to automatic restores the original import when no other feature needs it");
    Check(G::SaveMapping(map) && *slot==owned,"mapping can be re-enabled");
    *slot=reinterpret_cast<void*>(&Foreign);G::Stop();
    Check(*slot==reinterpret_cast<void*>(&Foreign) && G::MappingConflict(),"foreign import changes survive teardown");
    *slot=const_cast<void*>(owned);G::Stop();
    Check(*slot==reinterpret_cast<void*>(original) && !G::MappingApplied(),"owned import can finish restoration after a conflict");
    G::SetStateReaderForTests(nullptr);C::ResetForTests();VirtualFree(image,0,MEM_RELEASE);FreeLibrary(module);
    std::printf("NativeGamepadWindowsRt1: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
