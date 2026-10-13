// Real GridTeach/Vanguard native entries; save-I/O readiness is an RT1 stub.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/SharedCommandAvailability.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/GridTeachHook.h"
#include "../hooks/GridLearnedRuntime.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/AeroEquipmentBridge.h"
#include "../shared/ffx_addresses.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
namespace FfxHooks::RonsoPool {bool IsSaveIoReady() noexcept{return true;}}
namespace S=FfxHooks::SharedCommandAvailability;
namespace V=FfxHooks::Vanguard;
namespace L=FfxHooks::GridLearned::Runtime;
namespace C=FfxHooks::Config;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
static void Log(const char* text){std::fputs(text,stdout);}
static bool aeroEnabled=false;
static bool AeroValefor() noexcept {return aeroEnabled;}
static unsigned AeroSos(unsigned,const void*) noexcept {return 0;}
static const FfxHooks::AeroEquipmentBridge::Provider aeroProvider{AeroValefor,AeroSos};
int main(int argc,char** argv){
    if(argc!=4)return 2;SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"exact private PE relocates");
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    std::array<unsigned char,31*0xF90> actors{};const auto pointer=reinterpret_cast<std::uintptr_t>(actors.data());
    std::memcpy(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&pointer,4);
    for(unsigned i=0;i<31;++i){actors[i*0xF90+0xC]=actors[i*0xF90+0xE]=static_cast<unsigned char>(i);actors[i*0xF90+0xDC8]=1;}
    std::ifstream input(argv[3],std::ios::binary);std::vector<unsigned char> command((std::istreambuf_iterator<char>(input)),{});
    if(command.size()<20+374*96)return 2;const auto bank=reinterpret_cast<std::uintptr_t>(command.data());
    std::memcpy(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD2A92C>()),&bank,4);
    // Aero367 aliases disabled-command WORD0 if forwarded to the 352-bit native bank.
    actors[0x691]=0x80;const auto native=reinterpret_cast<S::Function>(base+S::Rva);
    Check(native(0,367)==1,"native extended ID aliases an unrelated flag without the shadow guard");
    C::ResetForTests();C::LoadTextForTests("[vanguard]\nelement_opposite_weakness=1\nfollow_up=1\n[f8_authority]\nvanguard_element_opposite_weakness=1\nvanguard_follow_up=1\n","C:\\private-command.ini");
    const bool first=std::strcmp(argv[2],"grid-first")==0;
    if(first)Check(FfxHooks::InstallGridTeachHook(base,Log).ok,"GridTeach installs first");
    Check(V::Start(base,false,Log),"actual Vanguard accepts shared availability and Follow Up witnesses");
    if(!first)Check(FfxHooks::InstallGridTeachHook(base,Log).ok,"GridTeach accepts the actual Vanguard-owned shared entry");
    if(failures)return 1;
    FfxHooks::NativeSaveEvents::ResetCompleted();
    Check(L::Ready()&&L::Set(5,367,true)==FfxHooks::GridLearned::Change::Changed,"isolated session learns Aero only for Lulu");
    const auto before=actors;
    Check(native(5,367)==1&&native(5,0x316F)==1,"raw and encoded Aero IDs use the same learned shadow");
    Check(native(0,367)==0&&native(0,0x316F)==0,"unlearned extended commands cannot borrow native disabled flags");
    Check(native(8,367)==0&&native(8,0x316F)==0,"Valefor requires its separate authorized compatibility rule");
    Check(actors==before,"availability queries never write actor or learned bitmaps");
    Check(FfxHooks::AeroEquipmentBridge::Register(&aeroProvider),"isolated Aero provider registers");aeroEnabled=true;
    Check(native(8,367)==1&&native(8,0x316F)==1,"Valefor gains only raw and encoded Aero through the shared entry");
    Check(native(9,367)==0&&native(8,368)==0&&native(0,367)==0,"compatibility does not grant other owners or commands");
    auto* aero=command.data()+20+96*367;const auto aeroRow=std::vector<unsigned char>(aero,aero+96);
    aero[0x2D]=1;Check(native(8,367)==0,"a replaced Aero command cannot borrow the exception");aero[0x2D]=64;
    std::array<unsigned char,31*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE> rings{};
    for(unsigned i=0;i<rings.size();i+=2)rings[i]=0xFF;
    const auto rp=reinterpret_cast<std::uintptr_t>(rings.data());
    std::memcpy(reinterpret_cast<void*>(base+RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR),&rp,4);
    auto* ply=reinterpret_cast<unsigned char*>(base+FfxHooks::ExecutableProfile::Rva<0xD3205C>()+8*0x94);
    for(unsigned id=65;id<=68;++id)ply[0x3E+id/8]|=static_cast<unsigned char>(1u<<(id%8));
    const auto saved=std::vector<unsigned char>(ply,ply+0x94);
    auto* actor=actors.data()+8*0xF90;actor[0x691]=0x80;std::array<unsigned char,32> disabled{};
    std::memcpy(disabled.data(),actor+0x690,disabled.size());
    const auto build=reinterpret_cast<int(__cdecl*)(int,int)>(base+FfxHooks::ExecutableProfile::Rva<0x39BB70>());
    build(8,static_cast<int>(reinterpret_cast<std::uintptr_t>(actor)));
    const auto* black=reinterpret_cast<const unsigned short*>(rings.data()+8*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE+120);
    std::printf("MENU_NATIVE has65=%d has66=%d has67=%d has68=%d has367=%d disabled=%02X\n",native(8,65),native(8,66),native(8,67),native(8,68),native(8,367),actor[0x691]);
    for(unsigned off=0;off<352;off+=2){const auto word=*reinterpret_cast<const unsigned short*>(rings.data()+8*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE+off);if(word!=0xFFFF&&word!=0xFF)std::printf("RING off=%u word=%04X\n",off,unsigned(word));}

    for(unsigned id:{65u,66u,67u,68u,367u}){unsigned count=0;for(unsigned i=0;i<24;++i)if((unsigned(black[i])&0xFFFu)==id)++count;
        Check(count==1,"native Valefor menu preserves each basic spell and adds Aero exactly once");}
    Check(!std::memcmp(saved.data(),ply,saved.size())&&!std::memcmp(disabled.data(),actor+0x690,disabled.size()),"menu compatibility preserves saves and adjacent disabled flags");
    Check(!std::memcmp(aeroRow.data(),aero,96),"Aero owner byte and command payload are never rewritten");
    aeroEnabled=false;build(8,static_cast<int>(reinterpret_cast<std::uintptr_t>(actor)));
    bool found=false;for(unsigned i=0;i<24;++i)found=found||((black[i]&0xFFF)==367);
    Check(!found&&native(8,367)==0,"closing compatibility removes its menu entry on native rebuild");
    FfxHooks::AeroEquipmentBridge::Unregister(&aeroProvider);
    V::RequestStop();Check(native(5,367)==1,"Vanguard teardown preserves the installed learned consumer");
    FfxHooks::RequestGridTeachStop();Check(!FfxHooks::IsGridTeachHookInstalled()&&native(0,367)==1,"final teardown restores the exact original native route");
    std::printf("SHARED_GRID_VANGUARD %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
