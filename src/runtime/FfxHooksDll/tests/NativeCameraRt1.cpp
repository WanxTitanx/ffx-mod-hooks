#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../hooks/NativePortsHook.h"
#include "../hooks/F8FlagCatalog.h"
#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"
#include "MusicPeFixture.inc"
namespace P=FfxHooks::NativePorts;
namespace C=FfxHooks::Config;
static int checks,failures;
static HWND testForeground=nullptr;
static HWND Foreground(){return testForeground;}
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static bool Persist(void*,const char*,const char*){return true;}
static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    LRESULT result=0;if(P::HandleWindowMessage(window,message,wParam,lParam,false,&result))return result;
    return DefWindowProcW(window,message,wParam,lParam);
}
static void Pump(bool gameplay=true){
    const DWORD until=GetTickCount()+140;MSG message{};
    do {P::Tick(gameplay);while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(5);}
    while(static_cast<LONG>(GetTickCount()-until)<0);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* image=MapPe(Read(argv[1]));if(!image)return 2;
    C::ResetForTests();C::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
    C::LoadTextForTests("[camera]\nfree_look=0\nfreeze_scene=0\n", "native-camera-test.ini");
    Check(P::Start(reinterpret_cast<uintptr_t>(image),nullptr),"exact native PE profile starts owned-byte adapter");
    WNDCLASSW wc{};wc.lpfnWndProc=Procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"JarvisNativeCameraRt1";
    RegisterClassW(&wc);HWND window=CreateWindowW(wc.lpszClassName,L"Jarvis native camera RT1",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,480,320,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 2;P::BindWindow(window);SetForegroundWindow(window);
    // Windows OpenSSH has no interactive foreground desktop. Only the test
    // build can provide an owned-window focus identity; production keeps Win32.
    testForeground=window;P::SetForegroundReaderForTests(Foreground);
    auto& camera=image[RVA_FFX_DEBUG_FLAGS+4];auto& freeze=image[0x00EFBB63u];auto& battle=image[RVA_FFX_BATTLE_ACTIVE_FLAG];
    camera=freeze=0;battle=1;*reinterpret_cast<uint32_t*>(image+RVA_FFX_MENU_SUBSYSTEM_ACTIVE_FLAG)=0;
    for(unsigned i=0;i<32;++i)if(i!=4)image[RVA_FFX_DEBUG_FLAGS+i]=static_cast<unsigned char>(0x80u+i);
    for(unsigned i=0;i<12;++i)image[RVA_FFX_NATIVE_SPEED_BOOSTER+i]=static_cast<unsigned char>(0x50u+i);
    std::array<unsigned char,32> debugBefore{};std::memcpy(debugBefore.data(),image+RVA_FFX_DEBUG_FLAGS,debugBefore.size());
    std::array<unsigned char,12> speedBefore{};std::memcpy(speedBefore.data(),image+RVA_FFX_NATIVE_SPEED_BOOSTER,speedBefore.size());
    C::SetBool("camera.free_look",true);Pump();
    const auto cameraStatus=FfxHooks::GetF8RuntimeStatus(*FfxHooks::FindF8Flag("camera.free_look"));
    std::printf("CAMERA_FIXTURE foreground=%p window=%p profile=%u applied=%d native=%u readAddress=%08X expected=%08X\n",
        GetForegroundWindow(),window,static_cast<unsigned>(cameraStatus.availability),cameraStatus.appliedValue?1:0,camera,
        *reinterpret_cast<uint32_t*>(image+0x00390C7Bu),static_cast<unsigned>(reinterpret_cast<uintptr_t>(image)+RVA_FFX_DEBUG_FLAGS+4));
    Check(camera==1,"requested free camera reaches the exact native debug byte in battle");
    auto* reader=static_cast<unsigned char*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!reader)return 2;
    std::memcpy(reader,image+0x00390C78u,7);reader[7]=0xC3;FlushInstructionCache(GetCurrentProcess(),reader,8);
    Check(reinterpret_cast<int(__cdecl*)()>(reader)()==1,"exact relocated native camera reader sees the enabled control");
    Pump(false);Check(camera==0,"opening native menus restores free-camera ownership");
    Pump();Check(camera==1,"camera resumes when its permitted battle context returns");
    testForeground=nullptr;Pump();Check(camera==0,"losing foreground releases the owned camera byte");
    testForeground=window;Pump();Check(camera==1,"regaining foreground uses the same scoped native owner");
    C::SetBool("camera.free_look",false);Pump();Check(camera==0,"OFF restores camera byte");
    C::SetBool("camera.freeze_scene",true);Pump();Check(freeze==0,"field freeze never writes during a battle");
    battle=0;std::array<unsigned char,64> player{};*reinterpret_cast<uintptr_t*>(image+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR)=reinterpret_cast<uintptr_t>(player.data());
    Pump();Check(freeze==1,"field freeze applies only with a loaded field actor");
    Pump(false);Check(freeze==0,"menus release field freeze before editing settings");
    C::SetBool("camera.freeze_scene",false);Pump();Check(freeze==0,"OFF restores field scene progression");
    for(unsigned i=0;i<debugBefore.size();++i)if(i!=4)Check(debugBefore[i]==image[RVA_FFX_DEBUG_FLAGS+i],"existing Cheat bytes remain unchanged");
    Check(std::memcmp(speedBefore.data(),image+RVA_FFX_NATIVE_SPEED_BOOSTER,speedBefore.size())==0,"SpeedHack native state and availability are untouched");
    camera=2;C::SetBool("camera.free_look",true);battle=1;Pump();
    Check(camera==2,"non-boolean foreign camera value is rejected without a write");
    C::SetBool("camera.free_look",false);camera=0;Pump();P::Stop();
    Check(camera==0 && freeze==0,"normal teardown leaves native state restored");
    P::SetForegroundReaderForTests(nullptr);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);VirtualFree(reader,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);C::ResetForTests();
    std::printf("NativeCameraRt1: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
