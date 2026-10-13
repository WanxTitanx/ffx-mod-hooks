// Jarvis-HOOK: actual Pump entry, native layer walker and cached-help renderer.
// Device/font endpoints are observed stubs; native selection and traversal run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include "../shared/ExecutableProfile.h"
#include "NativeHelpPeFixture.inc"
#if __has_include("../hooks/NativeMenuHelpGuard.h")
#include "../hooks/NativeMenuHelpGuard.h"
#define HAS_HELP_GUARD 1
#endif

namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
constexpr bool steam=FfxHooks::ExecutableProfile::Steam20261001;
constexpr std::uint32_t drawRva=steam?0x4A9350:0x4A9300,walkRva=steam?0x4A9690:0x4A9640;
constexpr std::uint32_t setterRva=steam?0x4AAEB0:0x4AAE60,messageRva=steam?0x1441C00:0x1441BC0;
constexpr std::uint32_t poolRva=steam?0x1440900:0x14408C0;
std::atomic<unsigned> panels{0},titles{0},texts{0},customDraws{0};
const unsigned char helpText[]="Swap equipment.";
bool injectFault=false,foreignThread=false,nestedFrame=false;
unsigned observedArgument=0;
unsigned char* image=nullptr;
std::uintptr_t g_base=0;
std::uint64_t g_nativeMenuPumpTramp=0;
volatile LONG g_forceSubsystem=0;
namespace NativeMenu {struct Menu{int obj=0;};}
NativeMenu::Menu g_nativeMenu,g_arenaPlusMenu,g_sinMenu,g_f7Menu;
bool equipment=false,compose=false,maechen=false,drain=false;
namespace EquipmentMenu {[[maybe_unused]] bool Active(){return equipment;}}
[[maybe_unused]] bool ArenaPlusComposePick_IsActive(){return compose;}
namespace FfxHooks {
[[maybe_unused]] bool Maechen_MenuOwned(){return maechen;}
#ifdef HAS_HELP_GUARD
namespace NativeMenuHelp=::FfxHooks::NativeMenuHelp;
#endif
}
[[maybe_unused]] bool NativeMenuHubCloseDrainPending(){return drain;}
void F7MainMenuMouseTick(int){}
#include "NativeHelpOwner.inc"
#include "NativeHelpPumpEntry.inc"

int __cdecl Quiet(){return 0;}
float __cdecl Scale(float input){return input;}
int __cdecl Panel(){++panels;return 0;}
int __cdecl Title(){++titles;return 0;}
int __cdecl Text(int,const unsigned char* text){if(text==helpText)++texts;return 0;}
int __cdecl Custom(int){++customDraws;return 0;}
void Jump(std::uint32_t rva,void* target){
 image[rva]=0xE9;const auto rel=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-g_base-rva-5);
 std::memcpy(image+rva+1,&rel,4);
}
void StubCallAt(std::uint32_t rva,void* target){
 Check(image[rva]==0xE8||image[rva]==0xE9,"fixture endpoint is a native direct call or tail jump");
 std::int32_t displacement=0;std::memcpy(&displacement,image+rva+1,4);
 Jump(rva+5+displacement,target);
}
void DrawHelp(){reinterpret_cast<int(__cdecl*)(int)>(image+drawRva)(static_cast<int>(g_base+poolRva));}
int __cdecl OriginalPump(unsigned argument){
 observedArgument=argument;
 if(nestedFrame){
  nestedFrame=false;const int owner=g_f7Menu.obj;g_f7Menu.obj=0;
  NativeHelpTestPump(argument+1);g_f7Menu.obj=owner;
 }
 if(foreignThread){std::thread other(DrawHelp);other.join();}
 if(injectFault)RaiseException(0xE0424242,0,0,nullptr);
 const auto walk=reinterpret_cast<void(__cdecl*)(int)>(image+walkRva);
 walk(2);walk(6);return 0x217;
}
bool CatchPumpFault(){
 __try{NativeHelpTestPump(91);}
 __except(EXCEPTION_EXECUTE_HANDLER){return true;}
 return false;
}
void ClearOwners(){g_nativeMenu={};g_arenaPlusMenu={};g_sinMenu={};g_f7Menu={};equipment=compose=maechen=drain=false;}
void ResetCounts(){panels=0;titles=0;texts=0;customDraws=0;}
bool HelpVisible(){return panels==1&&titles==1&&texts==1;}
bool HelpHidden(){return panels==0&&titles==0&&texts==0;}
}

int main(int argc,char** argv){
 if(argc!=2)return 2;
 image=MapPe(Read(argv[1]));if(!image)return 2;
 g_base=reinterpret_cast<std::uintptr_t>(image);
 g_nativeMenuPumpTramp=reinterpret_cast<std::uintptr_t>(&OriginalPump);
 // Leave all 437 renderer bytes and the complete pool traversal unmodified.
 for(const unsigned offset:{0u,17u,22u,55u,82u,425u,431u})StubCallAt(drawRva+offset,reinterpret_cast<void*>(&Quiet));
 for(const unsigned offset:{161u,179u,197u,215u,282u,300u,318u,336u,388u,406u})StubCallAt(drawRva+offset,reinterpret_cast<void*>(&Scale));
 StubCallAt(drawRva+225,reinterpret_cast<void*>(&Panel));
 StubCallAt(drawRva+346,reinterpret_cast<void*>(&Title));
 StubCallAt(drawRva+417,reinterpret_cast<void*>(&Text));
 for(const unsigned offset:{22u,27u,46u,54u,62u,67u})StubCallAt(walkRva+offset,reinterpret_cast<void*>(&Quiet));
 auto* help=image+poolRva;auto* custom=help+152;
 const auto helpFn=static_cast<std::uint32_t>(g_base+drawRva),customFn=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Custom));
 std::memcpy(help+16,&helpFn,4);help[62]=6;help[64]=1;
 std::memcpy(custom+16,&customFn,4);custom[62]=2;custom[64]=1;
 // Real setter records the last option. Closing V does not erase that cache.
 reinterpret_cast<void(__cdecl*)(const unsigned char*)>(image+setterRva)(helpText);
 FlushInstructionCache(GetCurrentProcess(),image,0x700000);
#ifdef HAS_HELP_GUARD
 Check(::FfxHooks::NativeUiSupport::Profile(g_base,::FfxHooks::NativeMenuHelp::Evidence::renderer),
       "the complete relocated native renderer matches its recorded executable");
 bool everyByteGated=true;
 for(unsigned offset=0;offset<437;++offset){
  image[drawRva+offset]^=1;
  everyByteGated=everyByteGated&&!::FfxHooks::NativeUiSupport::Profile(g_base,::FfxHooks::NativeMenuHelp::Evidence::renderer);
  image[drawRva+offset]^=1;
 }
 Check(everyByteGated,"mutation of any renderer byte is rejected without installing a hook");
 Check(!::FfxHooks::NativeMenuHelp::Start(0),"unknown image cannot arm help suppression");
 const auto byte=image[drawRva+40];image[drawRva+40]^=1;
 Check(!::FfxHooks::NativeMenuHelp::Start(g_base),"foreign renderer bytes reject the guard before mutation");
 image[drawRva+40]=byte;
 Check(::FfxHooks::NativeMenuHelp::Start(g_base),"exact profile arms the actual help guard");
#endif
 ResetCounts();Check(NativeHelpTestPump(91)==0x217&&observedArgument==91&&HelpVisible()&&customDraws==1,
     "without a custom owner native help and the pool walker retain their behavior");
 for(int owner=0;owner<8;++owner){
  ClearOwners();switch(owner){case 0:g_nativeMenu.obj=1;break;case 1:g_f7Menu.obj=1;break;
   case 2:g_arenaPlusMenu.obj=1;break;case 3:g_sinMenu.obj=1;break;case 4:equipment=true;break;
   case 5:compose=true;break;case 6:maechen=true;break;default:drain=true;break;}
  ResetCounts();NativeHelpTestPump(91);
  Check(HelpHidden()&&customDraws==1,"cached native help stays out of the active custom menu or its close drain");
  const unsigned char* stored=nullptr;std::memcpy(&stored,image+messageRva,4);
  Check(stored==helpText,"custom presentation never clears or replaces the last native help message");
  ResetCounts();DrawHelp();Check(HelpVisible(),"help calls outside our Pump scope remain native");
  ClearOwners();ResetCounts();NativeHelpTestPump(91);
  Check(HelpVisible(),"closing a custom owner restores the native help on the next normal frame");
 }
 g_f7Menu.obj=1;foreignThread=true;ResetCounts();NativeHelpTestPump(91);foreignThread=false;
 Check(HelpVisible()&&customDraws==1,"another thread keeps its native help while our Pump suppresses only its own draw");
 nestedFrame=true;ResetCounts();NativeHelpTestPump(91);
 Check(HelpVisible()&&customDraws==2,"a nested normal frame restores outer suppression after drawing its own native help");
 injectFault=true;Check(CatchPumpFault(),"fixture raises a native exception inside the real Pump entry");injectFault=false;
 ResetCounts();DrawHelp();Check(HelpVisible(),"a native exception cannot leak suppression beyond the Pump scope");
#ifdef HAS_HELP_GUARD
 ::FfxHooks::NativeMenuHelp::Stop();ResetCounts();NativeHelpTestPump(91);
 Check(HelpVisible(),"logical stop makes the retained trampoline pass through even with an old owner");
#endif
 std::printf("NativeMenuHelp RT1: %u checks, %u failures\n",checks,failures);
 return failures?1:0;
}
