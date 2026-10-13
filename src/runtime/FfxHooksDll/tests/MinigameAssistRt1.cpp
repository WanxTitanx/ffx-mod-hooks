#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/MinigameAssist.h"
#include "../hooks/MinigameAssistEvidence.generated.h"
#include "../shared/ffx_addresses.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
using namespace FfxHooks::MinigameAssist;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
void Log(const char* text){std::printf("%s",text);}
using Fetch=std::uint32_t(__cdecl*)(void*,void*);
struct Request {Fetch fetch=nullptr;void* actor=nullptr;void* frame=nullptr;std::uint32_t result=0;};
DWORD WINAPI Foreign(void* pointer){auto& r=*static_cast<Request*>(pointer);r.result=r.fetch(r.actor,r.frame);return 0;}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const auto pe=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(pe&&PrivatePeFixture::NormalizeRelocations(pe),"private x86 executable maps without running the game");
    if(!pe||failures)return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(pe);
    Check(Profile(base),"decoder code matches the exact executable profile");
    Check(!Start(base,0,false,Log)&&!Active(),"default OFF installs no decoder hook");
    Check(!Start(base,3,true,Log)&&!Active(),"validate-only never installs a hook");
    DWORD old=0,ignored=0;auto* entry=reinterpret_cast<unsigned char*>(base+Evidence::fetchRva);
    Check(VirtualProtect(entry,1,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"private entry witness can be changed for negative admission");
    const auto initial=*entry;*entry^=1;Check(!Profile(base),"changed native decoder fails signature admission");*entry=initial;
    Check(VirtualProtect(entry,1,old,&ignored)!=FALSE&&Profile(base),"original decoder entry restored exactly");
    Check(Start(base,3,false,Log)&&Active(),"verified decoder installs with both assisted options");
    if(failures)return 1;
    const auto fetch=reinterpret_cast<Fetch>(base+Evidence::fetchRva);
    *reinterpret_cast<unsigned char*>(base+RVA_FFX_BATTLE_ACTIVE_FLAG)=0;
    *reinterpret_cast<std::uint32_t*>(base+RVA_FFX_SCENE_TRANSITION_PENDING)=0;
    std::array<std::uint32_t,8> actor{},frame{};
    for(unsigned index=0;index<kScriptCount;++index){
        const auto& script=Scripts()[index];
        const auto path=std::string(argv[2])+"/"+script.field+".atel.bin";
        std::ifstream stream(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),{});
        Check(bytes.size()>=script.codeOffset+script.codeBytes,"original private field script fixture is complete");
        if(bytes.size()<script.codeOffset+script.codeBytes)continue;
        const auto originalBytes=bytes;
        actor[0]=reinterpret_cast<std::uint32_t>(bytes.data()+0x38);actor[1]=reinterpret_cast<std::uint32_t>(bytes.data());
        *reinterpret_cast<std::uint16_t*>(base+RVA_FFX_SCENE_STATE_OBJECT)=script.room;
        for(const auto& rule:Rules())if(rule.script==index){
            frame[6]=reinterpret_cast<std::uint32_t>(bytes.data()+script.codeOffset+rule.offset);
            Check(fetch(actor.data(),frame.data())==(0x2E000000u|rule.replacement),"actual native decoder returns the assisted literal at the approved script site");
        }
        Check(bytes==originalBytes,"all assists leave the original ATEL bytecode and data untouched");
        const Rule* first=nullptr;for(const auto& rule:Rules())if(rule.script==index){first=&rule;break;}
        if(!first)continue;
        frame[6]=reinterpret_cast<std::uint32_t>(bytes.data()+script.codeOffset+first->offset);
        const auto originalDecoded=0x2E000000u|first->original;
        bytes[script.codeOffset+script.codeBytes-1]^=1;
        Check(fetch(actor.data(),frame.data())==originalDecoded&&Status()==Code::ScriptMismatch,"a changed instruction elsewhere rejects the entire script even at a matching override site");
        std::memcpy(bytes.data(),originalBytes.data(),bytes.size());
        Check(fetch(actor.data(),frame.data())==(0x2E000000u|first->replacement),"restoring code at the same address works without a stale verification cache");
        *reinterpret_cast<std::uint16_t*>(base+RVA_FFX_SCENE_STATE_OBJECT)=23;
        Check(fetch(actor.data(),frame.data())==originalDecoded,"title/menu scenes retain native instructions even with a known script pointer");
        *reinterpret_cast<std::uint16_t*>(base+RVA_FFX_SCENE_STATE_OBJECT)=script.room;
        *reinterpret_cast<unsigned char*>(base+RVA_FFX_BATTLE_ACTIVE_FLAG)=1;
        Check(fetch(actor.data(),frame.data())==originalDecoded,"battle execution cannot receive field assistance");
        *reinterpret_cast<unsigned char*>(base+RVA_FFX_BATTLE_ACTIVE_FLAG)=0;
        Request request{fetch,actor.data(),frame.data(),0};auto thread=CreateThread(nullptr,0,Foreign,&request,0,nullptr);
        if(thread){WaitForSingleObject(thread,10000);CloseHandle(thread);}
        Check(thread&&request.result==originalDecoded,"foreign execution threads cannot inherit field-owner assistance");
    }
    Stop();Check(!Active()&&Status()==Code::Stopped,"logical teardown closes admission without blocking or changing scripts");
    std::array<unsigned char,3> literal{{0xAE,3,0}};frame[6]=reinterpret_cast<std::uint32_t>(literal.data());
    Check(fetch(actor.data(),frame.data())==0x2E000003u,"stopped pinned hook delegates to the actual original decoder");
    std::printf("MinigameAssistRt1: %u/%u checks passed\n",checks-failures,checks);return failures?1:0;
}
