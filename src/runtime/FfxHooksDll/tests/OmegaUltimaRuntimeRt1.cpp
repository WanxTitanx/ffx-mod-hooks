// Jarvis-HOOK: isolated Windows adapter; no running game or RT2 acceptance.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/OmegaUltimaRuntime.h"
#include "../hooks/CustomMixWindowsAdapter.h"
#include "../shared/Config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
namespace O=FfxHooks::OmegaUltima;
namespace R=O::Runtime;
namespace B=O::Bridge;
namespace P=FfxHooks::ExecutableProfile;
namespace C=FfxHooks::CustomMixUltra::WindowsAdapter;
static unsigned checks=0,failures=0,draws=0,incidence=0,ordinal=0,originalCalls=0,assetCalls=0;
static std::uintptr_t base=0;
static std::uint32_t request=0;
static bool requestCurrent=true,expectedClone=false;
static std::string mode;
static std::vector<unsigned char> source,clone,foreign(128);
static unsigned expectedSlot=0;
static const unsigned char* retained=nullptr;
static O::FormationProof proof;
static O::Layout layout;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static void Log(const char* text){std::fputs(text,stdout);}
static std::vector<unsigned char> ReadFileBytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
template<class T>static void Put(std::uintptr_t address,const T& v){std::memcpy(reinterpret_cast<void*>(address),&v,sizeof(v));}
static std::uint32_t Pointer(){std::uint32_t p=0;std::memcpy(&p,reinterpret_cast<void*>(base+C::kCarrierPointerRva),4);return p;}
static bool Current(std::uint32_t id,std::int32_t) noexcept {return requestCurrent&&id==request;}
static bool NativeAsset(std::uintptr_t image,unsigned char* out,unsigned size) noexcept {
    ++assetCalls;if(image!=base||size!=clone.size())return false;
    std::memcpy(out,clone.data(),size);if(mode=="asset-fail")out[0]^=1;return true;
}
static bool Draw(void*,unsigned bound,unsigned& out) noexcept {
    ++draws;out=draws==1?incidence:ordinal;
    if(draws==1){
        if(mode=="stop-roll")R::Stop();
        if(mode=="name-drift")*reinterpret_cast<char*>(base+C::kCarrierNameRva)='n';
        if(mode=="bytes-drift")source[40]^=1;
        if(mode=="ticket-drift")requestCurrent=false;
        if(mode=="flag-drift")*reinterpret_cast<unsigned char*>(base+P::Rva<0xD2CA90>()+0x3BE)=0;
    }
    return out<bound;
}
static bool Original(void*,int* out){
    ++originalCalls;const auto* bytes=reinterpret_cast<const unsigned char*>(Pointer());
    Check(bytes!=nullptr,"native continuation receives a resource");
    if(expectedClone){
        Check(bytes!=source.data()&&O::Word(bytes+layout.slots+expectedSlot*2)==O::CloneNative,"m910 is present before the native load continuation");
        for(unsigned i=0;i<source.size();++i)if(i!=layout.slots+expectedSlot*2&&i!=layout.slots+expectedSlot*2+1)
            Check(bytes[i]==source[i],"only the chosen formation WORD is changed before loading");
        retained=bytes;
    }else Check(bytes==source.data(),"native fallback receives the original formation pointer");
    if(mode=="ownership"||mode=="exception-ownership")Put(base+C::kCarrierPointerRva,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(foreign.data())));
    if(mode=="stop-original")R::Stop();
    if(mode=="exception"||mode=="exception-ownership")RaiseException(0xE0123456,0,0,nullptr);
    *out=0;return true;
}
static bool InvokeException(int* out) {
    __try {B::Battle(base,nullptr,Original,out);}
    __except(GetExceptionCode()==0xE0123456?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
static void Prepare(const O::FormationProof& p,const std::filesystem::path& battle,unsigned flag=2){
    proof=p;char name[16]{};std::snprintf(name,sizeof(name),"omeg%02u_%02u",p.field-1076,p.index);
    source=ReadFileBytes(battle/"btl"/name/(std::string(name)+".bin"));
    Check(O::Inspect(proof,source.data(),source.size(),layout),"fixture uses the exact Editor formation");
    Put(base+C::kCarrierPointerRva,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source.data())));
    Put(base+C::kCarrierSizeRva,static_cast<std::uint16_t>(source.size()));std::memcpy(reinterpret_cast<void*>(base+C::kCarrierNameRva),name,10);
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2CA90>()+0x3BE)=static_cast<unsigned char>(flag);
    draws=originalCalls=0;requestCurrent=true;++request;
    FfxHooks::SinNatural::Evidence evidence{FfxHooks::SinNatural::kCallerRva,-1,static_cast<int>(FfxHooks::OmegaEncounter::NativeField(p.field)),0,88,0,static_cast<std::uint8_t>(p.index)};
    B::Natural(evidence,request,Current);
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=4)return 2;
    mode=argv[3];const std::filesystem::path game=argv[2],battle=game/"data/mods/ffx_ps2/ffx/master/jppc/battle";
    HMODULE pe=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!pe)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(pe),"exact private PE relocations");base=reinterpret_cast<std::uintptr_t>(pe);
    using Save=std::uintptr_t(__cdecl*)();
    Check(reinterpret_cast<Save>(base+0x385240)()==base+P::Rva<0xD2CA90>(),"actual native save getter confirms the payload base");
    auto text=ReadFileBytes(game/"modules/config/_omega-ultima-v1.json");O::Manifest manifest;
    Check(O::ParseManifest({reinterpret_cast<const char*>(text.data()),text.size()},manifest)==O::ManifestCode::Ready,"isolated enabled manifest resolves");
    clone=ReadFileBytes(battle/"mon/_m910/m910.bin");
    FfxHooks::Config::ResetForTests();FfxHooks::Config::LoadTextForTests("[spira]\n","C:\\private-omega-ultima.ini");
    Check(!R::Requested()&&!R::Start(base,game,false,Log),"default OFF installs no consumer");
    FfxHooks::Config::LoadTextForTests("[spira]\nomega_ultima=1\n","C:\\private-omega-ultima.ini");
    Check(!R::Start(base,game,true,Log)&&!B::provider.load(),"validate-only has no admission");
    Check(!R::Start(0,game,false,Log),"wrong executable identity rejected");
    Check(!R::Start(base,game/"missing",false,Log),"missing pack retains native behavior");
    R::SetProvidersForTests(NativeAsset,{nullptr,Draw});
    Check(R::Start(base,game,false,Log),"exact profile, real formation/clone bytes and manifest prepare");
    Check(!R::Start(0,game,false,Log),"idempotent startup cannot claim another executable base");
    if(mode=="normal"){
        // Invalid external notifications cannot claim the native owner thread.
        auto invalid=FfxHooks::SinNatural::Evidence{};
        std::thread foreignThread([&]{B::Natural(invalid,1,Current);});foreignThread.join();
        for(unsigned flag:{0u,1u,4u,128u}){Prepare(manifest.formations[0],battle,flag);expectedClone=false;int result=99;
            Check(B::Battle(base,nullptr,Original,&result)&&result==0&&originalCalls==1&&!draws,"locked completion flags preserve original once without RNG");}
        Prepare(manifest.formations[0],battle);incidence=10;expectedClone=false;int result=99;
        Check(B::Battle(base,nullptr,Original,&result)&&originalCalls==1&&draws==1,"incidence miss consumes only one draw and preserves the formation");
        incidence=0;
        for(const auto& p:manifest.formations){
            unsigned n=0;
            for(unsigned slot=0;slot<8;++slot){Prepare(p,battle);if(!(layout.eligible&(1u<<slot)))continue;
                ordinal=n++;expectedSlot=slot;expectedClone=true;const auto before=source;
                Check(B::Battle(base,nullptr,Original,&result)&&result==0&&originalCalls==1&&draws==2,"each eligible active slot can be selected with two draws and one original call");
                Check(Pointer()==reinterpret_cast<std::uintptr_t>(source.data())&&source==before,"loan restores its pointer and never modifies source data");
                Check(retained&&O::Word(retained+layout.slots+slot*2)==O::CloneNative,"native interior pointers may safely outlive the loan");
            }
        }
        Prepare(manifest.formations[0],battle,6);ordinal=0;expectedSlot=0;expectedClone=true;
        Check(B::Battle(base,nullptr,Original,&result),"combined completion flags remain eligible");
        expectedClone=false;const auto oldDraws=draws;
        Check(B::Battle(base,nullptr,Original,&result)&&draws==oldDraws,"one natural request is never rolled twice");
        Prepare(manifest.formations[0],battle,0);expectedClone=false;
        Check(B::Battle(base,nullptr,Original,&result)&&!draws,"loading a pre-Ultima save has no sticky unlock");
        Prepare(manifest.formations[0],battle);requestCurrent=false;expectedClone=false;
        Check(B::Battle(base,nullptr,Original,&result)&&!draws,"superseded force/compose request cannot consume natural selection");
        Prepare(manifest.formations[0],battle);expectedClone=false;
        std::thread thread([&]{int out=0;B::Battle(base,nullptr,Original,&out);});thread.join();
        Check(!draws,"foreign battle thread performs only the original continuation");
        R::Stop();expectedClone=false;draws=0;originalCalls=0;
        Check(B::Battle(base,nullptr,Original,&result)&&originalCalls==1&&!draws,"stopped consumer is inert");
    }else{
        Prepare(manifest.formations[0],battle);ordinal=0;expectedSlot=0;
        expectedClone=mode=="ownership"||mode=="exception"||mode=="exception-ownership"||mode=="stop-original";
        int result=99;
        if(mode=="exception"||mode=="exception-ownership")Check(InvokeException(&result),"native exception reaches its existing handler");
        else Check(B::Battle(base,nullptr,Original,&result)&&result==0,"original success and native zero return preserved");
        Check(originalCalls==1,"adversarial boundary invokes the original once");
        if(mode=="ownership"||mode=="exception-ownership")Check(Pointer()==reinterpret_cast<std::uintptr_t>(foreign.data()),"another pointer owner is never clobbered");
        else Check(Pointer()==reinterpret_cast<std::uintptr_t>(source.data()),"carrier pointer restored on every owned exit");
        if(mode=="stop-roll")Check(draws==1,"closing admission prevents the second draw as well as mutation");
        if(mode=="asset-fail")Check(!draws&&assetCalls==1,"native-loader mismatch rejects before any random draw");
        if(mode=="ownership"||mode=="exception-ownership"||mode=="stop-original"||mode=="stop-roll"||mode=="asset-fail"){
            mode="after-stop";Prepare(manifest.formations[0],battle);expectedClone=false;
            Check(B::Battle(base,nullptr,Original,&result)&&!draws,"closed admission cannot silently resume on a later encounter");
        }
        R::Stop();
    }
    std::printf("OmegaUltimaRuntimeRt1 %s: %u checks, %u failures\n",argv[3],checks,failures);return failures?1:0;
}
