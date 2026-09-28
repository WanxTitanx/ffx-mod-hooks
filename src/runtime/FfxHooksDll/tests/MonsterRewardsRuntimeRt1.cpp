#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/MonsterRewardsRuntime.h"
#include "../hooks/MonsterRewardSettings.h"
#include "../shared/Config.h"
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <thread>
namespace R=FfxHooks::MonsterRewards;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
static void* entry=nullptr;
static int resultAp=0,resultGil=0;
static unsigned resultFlags=0,resultEdi=0;
__declspec(naked) static void Resume(){
    __asm {
        pushfd
        pop eax
        mov resultFlags,eax
        mov resultEdi,edi
        mov eax,[ebp-4]
        mov resultAp,eax
        mov eax,[ebp-8]
        mov resultGil,eax
        pop esi
        pop ebx
        pop edi
        mov esp,ebp
        pop ebp
        ret
    }
}
__declspec(naked) static void Invoke(unsigned,void*,void*,unsigned,int,int){
    __asm {
        push ebp
        mov ebp,esp
        sub esp,0x18
        push edi
        push ebx
        mov ebx,1
        mov edi,[ebp+0x10]
        mov eax,[ebp+0x18]
        mov [ebp-4],eax
        mov eax,[ebp+0x1C]
        mov [ebp-8],eax
        jmp dword ptr [entry]
    }
}
static bool Persist(void*,const char*,const char*){return true;}
static void Put16(unsigned char* p,unsigned value){p[0]=static_cast<unsigned char>(value);p[1]=static_cast<unsigned char>(value>>8);}
static void Put32(void* p,std::uint32_t value){std::memcpy(p,&value,4);}
static std::uintptr_t Image(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return 0;
    const auto size=file.tellg();if(size<4096)return 0;std::vector<unsigned char> bytes(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||std::size_t(dos->e_lfanew)>bytes.size()-sizeof(IMAGE_NT_HEADERS32))return 0;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto* mapped=static_cast<unsigned char*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!mapped)return 0;
    std::memcpy(mapped,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        if(std::uint64_t(s.PointerToRawData)+s.SizeOfRawData>bytes.size()||std::uint64_t(s.VirtualAddress)+s.SizeOfRawData>nt->OptionalHeader.SizeOfImage)return 0;
        std::memcpy(mapped+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);}
    return reinterpret_cast<std::uintptr_t>(mapped);
}
int main(int argc,char** argv){
    if(argc!=4)return 2;const bool off=std::strcmp(argv[2],"off")==0,invalid=std::strcmp(argv[2],"invalid")==0;
    const auto root=std::filesystem::absolute(argv[3]);std::filesystem::create_directories(root);
    const auto ini=(root/"ffx-hooks.ini").string();
    FfxHooks::Config::LoadTextForTests(off?"[cheats]\nmonster_rewards=0\n":"[f8_authority]\nmonster_rewards=1\n[cheats]\nmonster_rewards=1\nap_100x=1\nap_multiplier=25\ngil_100x=1\ngil_multiplier=7\n",ini.c_str());
    FfxHooks::Config::SetProvidersForTests({nullptr,nullptr,nullptr,Persist});
    const auto base=Image(argv[1]);Check(base!=0,"private supported executable maps without launching the game");if(!base)return 1;
    std::vector<unsigned char> actors(31*0xF90,0xA5);std::array<unsigned char,280> loot{};loot.fill(0xA5);
    Put16(loot.data(),60000);Put16(loot.data()+2,65000);Put16(loot.data()+4,65535);
    auto* actor=actors.data()+18*0xF90;Put16(actor+0xC,18);Put16(actor+0xE,0x1001);actor[0xDC8]=1;
    Put32(actor+0xF88,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(loot.data())));
    Put32(reinterpret_cast<void*>(base+0xD334CC),static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actors.data())));
    const auto originalActors=actors;const auto originalLoot=loot;
    auto* seam=reinterpret_cast<unsigned char*>(base+0x399144);std::array<unsigned char,6> before{};std::memcpy(before.data(),seam,before.size());
    if(invalid){seam[2]^=1;Check(!R::Prepare(base,false,nullptr)&&!R::Installed(),"a changed native seam cannot install");Check(seam[2]==(before[2]^1),"rejected installation does not replace foreign bytes");}
    else {
        Check(R::Prepare(base,false,nullptr),"supported profile prepares the selected mode");R::TickMainThread();
        Check(R::Count()>=347,"the native menu has the factual monster name catalog");R::Preview preview{};
        Check(R::ReadPreview(1,preview)&&preview.source==R::Source::LiveActor&&preview.base.ap==65000&&preview.base.gil==60000,"live metadata supplies unsigned native AP and Gil for the selected monster");
        if(off){Check(!R::Installed()&&std::memcmp(before.data(),seam,before.size())==0,"default-OFF mode leaves original instructions untouched");}
        else {
            Check(R::Installed(),"explicit per-monster opt-in installs one profile-gated seam");
            // Only the isolated continuation is replaced: the real detour and
            // MinHook gateway execute the native six-byte instruction span.
            auto* resume=seam+6;resume[0]=0xE9;Put32(resume+1,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Resume)-(base+0x39914A+5)));
            FlushInstructionCache(GetCurrentProcess(),seam,32);entry=seam;
            Check(R::SaveMultiplier(1,R::Kind::Ap,3)&&R::SaveMultiplier(1,R::Kind::Gil,4),"independent rates persist under the chosen monster ID");
            Invoke(18,actor,loot.data(),0,65000*25,60000*7);
            std::printf("frame AP=%d Gil=%d EDI=%u flags=%X\n",resultAp,resultGil,resultEdi,resultFlags);
            Check(resultAp==4875000&&resultGil==1680000,"actual x86 reward frame receives individual then observed global multiplication exactly once");
            Check(resultEdi==static_cast<unsigned>(resultAp)&&(resultFlags&0x40)!=0,"native overwritten instructions replay with the adjusted AP and their original flags");
            Check(R::ReadPreview(1,preview)&&preview.ap.individual==195000&&preview.ap.applied==4875000&&preview.gil.individual==240000&&preview.gil.applied==1680000,"menu preview matches the wide amounts consumed by native rewards");
            Invoke(18,actor,loot.data(),1,65535*100,60000);
            Check(resultAp==19660500&&resultGil==240000,"overkill and a disabled general Gil multiplier remain independent");
            Put16(actor+0xE,0x1156);Invoke(18,actor,loot.data(),0,65000,60000);
            Check(resultAp==65000&&resultGil==60000,"another monster does not inherit the edited species rate");Put16(actor+0xE,0x1001);
            Put16(actor+0xE,1);Invoke(18,actor,loot.data(),0,65000,60000);
            Check(resultAp==65000&&resultGil==60000,"raw player/species aliases cannot access monster-only rates");Put16(actor+0xE,0x1001);
            std::thread other([&]{Invoke(18,actor,loot.data(),0,65000,60000);});other.join();
            Check(resultAp==65000&&resultGil==60000,"a non-owner thread remains on the original reward route");
            Check(R::SaveMultiplier(1,R::Kind::Ap,1000)&&R::SaveMultiplier(1,R::Kind::Gil,1000),"maximum accepted individual rates save");
            Invoke(18,actor,loot.data(),1,65535*100,60000*100);
            Check(resultAp==static_cast<int>(R::SafeBeforeVanilla(R::Kind::Ap))&&resultGil==static_cast<int>(R::SafeBeforeVanilla(R::Kind::Gil)),"pre-vanilla bounds prevent native signed accumulator overflow");
            const auto settings=root/L"monster-rewards-v1.tsv";
            HANDLE locked=CreateFileW(settings.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(locked!=INVALID_HANDLE_VALUE,"existing settings can be locked against replacement for an I/O failure case");
            Check(!R::SaveMultiplier(1,R::Kind::Ap,2),"failed atomic replacement is reported");
            if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
            Check(R::ReadPreview(1,preview)&&preview.apMultiplier==1000,"failed persistence retains the previous multiplier");
            Check(!R::SaveMultiplier(1,R::Kind::Ap,0)&&!R::SaveMultiplier(1,R::Kind::Ap,1001)&&!R::SaveMultiplier(4096,R::Kind::Gil,2),"invalid rates and IDs are rejected before writing");
            Invoke(18,actor,loot.data(),0,65001,60000);Check(resultAp==65001&&resultGil==60000,"unknown foreign reward math is preserved instead of multiplied again");
            std::ifstream persisted(settings,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(persisted)),std::istreambuf_iterator<char>());persisted.close();R::RateTable restored{};
            Check(R::ParseSettings(bytes,restored)&&restored[1].ap==1000&&restored[1].gil==1000&&restored[342].ap==1,"real settings file reload preserves independent rates and neutral other monsters");
            const std::string foreign="ffx.monster-rewards.v1\n1\t5\t6\n";{std::ofstream otherFile(settings,std::ios::binary|std::ios::trunc);otherFile<<foreign;}
            Check(!R::SaveMultiplier(1,R::Kind::Ap,2),"another writer's settings are not overwritten by a stale UI draft");
            std::ifstream after(settings,std::ios::binary);const std::string preserved((std::istreambuf_iterator<char>(after)),std::istreambuf_iterator<char>());
            Check(preserved==foreign,"foreign settings remain byte-identical after the rejected save");
            R::RequestStop();Invoke(18,actor,loot.data(),0,65000,60000);
            Check(resultAp==65000&&resultGil==60000,"teardown closes admission while the pinned gateway remains safe");
        }
    }
    Check(loot==originalLoot&&actors==originalActors,"every monster, loot, and native WORD byte remains unchanged");
    std::printf("MonsterRewardsRuntimeRt1 %s %u/%u passed\n",argv[2],checks-failures,checks);return failures?1:0;
}
