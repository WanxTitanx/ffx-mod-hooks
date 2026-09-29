// Actual adapter below; native game, installation, session and Workshop are simulated.
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define __cdecl
using DWORD=std::uint32_t;
static DWORD errorValue=0;
static DWORD GetLastError(){return errorValue;}
static void SetLastError(DWORD v){errorValue=v;}
#endif
#include "SeymourGearSortCore.h"
namespace Test {
namespace S=FfxHooks::SeymourGearSort;
unsigned checks=0,failures=0,adds=0,publishes=0,swaps=0,within=0,refreshes=0,originals=0;
unsigned thread=7,ownerThread=7;
unsigned battleReads=0,revokeAt=0,writesAfterRevocation=0;
bool revoked=false,revive=false;
int config=1;
bool profile=true,signature=true,publish=true,retire=true,master=true,persist=true,io=true;
bool busyWorkshop=false,changeSession=false,stopInside=false,throwInside=false,reenter=false;
std::uint8_t battle=0;
std::uint64_t revision=4;
constexpr std::uintptr_t Base=0x400000;
S::Snapshot image;
std::array<std::uint16_t,14> counts{};
std::uint32_t adjacent=0xdeadbeef;
std::array<unsigned,200> identities{};
std::array<S::Gear,200> baselineGear{};
std::string scenario;
void Check(bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
template<class F> struct Exit {F f;~Exit(){f();}};
template<class F> Exit<F> OnExit(F f){return {f};}
int NativeCount();int NativeSwap(unsigned,unsigned);void NativeWithin(void*,int,unsigned);void NativeRefresh();
void DuringPublish();
int OriginalGroup(){++originals;SetLastError(731);return 19;}
void OriginalWithin(){++originals;SetLastError(731);}
}
#if !defined(_WIN32)
static DWORD GetCurrentThreadId(){return Test::thread;}
#endif
namespace FfxHooks {
namespace SeymourGearSort {void RequestStop()noexcept;void PresentTick();}
namespace Config {
enum class IntReadState {Missing,Valid,Invalid};
struct IntReadResult {IntReadState state;int value;};
struct BoolGateResult {bool value;};
IntReadResult ReadIntExact(const char*,int,int){return {Test::config<0?IntReadState::Invalid:IntReadState::Valid,Test::config};}
bool SetInt(const char*,int v){if(!Test::persist)return false;Test::config=v;return true;}
}
struct F8FlagSpec{};
const F8FlagSpec* FindF8Flag(const char*){static F8FlagSpec f;return &f;}
Config::BoolGateResult ResolveF8Flag(const F8FlagSpec&){return {Test::master};}
namespace RonsoPool {bool IsSaveIoReady(){return Test::io;}}
namespace SeymourSession {
struct Token {std::uint64_t revision;unsigned thread;bool Valid()const{return revision&&thread;}};
bool PublisherReady() noexcept {return Test::io;}
Token Capture() noexcept {return Test::io?Token{Test::revision,Test::ownerThread}:Token{};}
bool Current(const Token& t) noexcept {return Test::io&&t.revision==Test::revision&&t.thread==Test::ownerThread;}
}
namespace EquipmentWorkshop {
enum class RuntimeCode {Disabled,Ready,Busy};
struct Snapshot {RuntimeCode code;unsigned ownerThread;};
Snapshot Status(){return {Test::busyWorkshop?RuntimeCode::Busy:RuntimeCode::Ready,Test::ownerThread};}
bool Requested(){return true;}
}
namespace MinHookBatch {enum class Owner {SeymourGearSort};}
namespace RecoveryEvidence {
struct Proof {std::uintptr_t rva;const std::uint8_t* bytes;std::size_t size;const std::uint16_t* relocations;std::size_t relocationCount;};
}
namespace RecoveryNative {
bool Profile(std::uintptr_t b){return b==Test::Base&&Test::profile;}
bool Match(std::uintptr_t,const RecoveryEvidence::Proof& proof){return Test::signature&&!(Test::scenario=="helper-signature"&&proof.rva==0x4c94b0);}
bool Range(std::uintptr_t,std::size_t,std::uintptr_t=0,bool=false,bool=false){return true;}
bool Copy(void* output,const void* source,std::size_t size){
    const auto at=reinterpret_cast<std::uintptr_t>(source)-Test::Base;const void* data=nullptr;
    if(at==0xd2a8e0&&size==1){
        data=&Test::battle;++Test::battleReads;
        if(Test::revokeAt==Test::battleReads){
            Test::config=0;SeymourGearSort::PresentTick();
            if(Test::revive){Test::config=1;SeymourGearSort::PresentTick();}
            Test::revoked=true;
        }
    }
    if(at==0x1197730&&size<=sizeof(Test::image.rows))data=Test::image.rows.data();
    if(at==0xd30f2c&&size==sizeof(Test::image.gear))data=Test::image.gear.data();
    if(at==0x146a3a4&&size==sizeof(Test::counts))data=Test::counts.data();
    for(unsigned i=0;i<18;++i)if(at==0xd3205c+i*0x94+0x2d&&size==2)data=Test::image.equipped.data()+i*2;
    if(!data)return false;
    std::memcpy(output,data,size);SetLastError(381);return true;
}
class OwnedBatch {
public:
    bool Add(std::uintptr_t target,void*,void** original){++Test::adds;
        *original=target==Test::Base+0x4c9bc0?reinterpret_cast<void*>(&Test::OriginalWithin):reinterpret_cast<void*>(&Test::OriginalGroup);return true;}
    bool Publish(MinHookBatch::Owner,const void*){++Test::publishes;Test::DuringPublish();return Test::publish;}
    bool DiscardUnpublished(){return Test::retire;}
    bool Neutralize(){return Test::retire;}
};
}
namespace SeymourGearSort {void RequestStop()noexcept;void PresentTick();}
}
// INSERT ACTUAL SORT ADAPTER
namespace Test {
void DuringPublish(){if(scenario=="stop-during")S::RequestStop();}
int NativeCount(){return static_cast<int>(image.count);}
int NativeSwap(unsigned a,unsigned b){
    if(revoked)++writesAfterRevocation;
    ++swaps;const auto x=S::Slot(a),y=S::Slot(b);
    Check(x<200&&y<200,"bounded native saved-inventory swap");
    std::swap(image.gear[x],image.gear[y]);std::swap(identities[x],identities[y]);
    for(auto& slot:image.equipped){if(slot==x)slot=static_cast<std::uint8_t>(y);else if(slot==y)slot=static_cast<std::uint8_t>(x);}
    SetLastError(719);if(scenario=="swap-exception")throw std::runtime_error("simulated swap exception");return 1;
}
void NativeRefresh(){
    if(revoked)++writesAfterRevocation;
    ++refreshes;S::Counts result{};Check(S::Count(image,result),"count input valid");
    std::copy_n(result.begin(),14,counts.begin());
    if(scenario=="corrupt-counter")++counts[0];SetLastError(727);
}
void NativeWithin(void*,int start,unsigned count){
    if(revoked)++writesAfterRevocation;
    ++within;Check(start>=0&&static_cast<unsigned>(start)+count<=image.count,"bounded native within span");
    if(count>1)NativeSwap(S::Handle(image.rows[static_cast<unsigned>(start)]),S::Handle(image.rows[static_cast<unsigned>(start)+count-1]));
    if(scenario=="corrupt-within")image.gear[S::Slot(image.rows[static_cast<unsigned>(start)])][7]^=1;
    if(scenario=="outside-within")image.gear[199][7]^=1;
    if(scenario=="row-drift")image.rows[0]^=0x10000;
    if(changeSession)++revision;if(stopInside)S::RequestStop();
    if(scenario=="off-on"){config=0;S::PresentTick();config=1;S::PresentTick();}
    if(reenter){reenter=false;Check(S::OwnerShim()==0,"nested ordering rejected");}
    SetLastError(729);if(throwInside)throw std::runtime_error("simulated native exception");
}
void Make(){
    image={};image.generation=revision;image.thread=thread;image.count=20;image.equipped.fill(255);
    for(unsigned i=0;i<200;++i)identities[i]=i;
    for(unsigned i=0;i<20;++i){auto& g=image.gear[i];image.rows[i]=0x5000u+i;
        g[0]=static_cast<std::uint8_t>(i);g[2]=1;g[4]=i<8?static_cast<std::uint8_t>(i%2):7;
        g[5]=static_cast<std::uint8_t>((i/2)%2);g[6]=255;g[11]=4;}
    if(scenario=="duplicates")image.gear[17]=image.gear[16];baselineGear=image.gear;
}
void CheckIdentities(){for(unsigned i=0;i<200;++i)Check(image.gear[i]==baselineGear[identities[i]],"native swap preserves item identity including duplicate bytes");}
}
int main(int argc,char** argv){
    using namespace Test;if(argc!=2)return 2;scenario=argv[1];
    if(scenario.rfind("revoke-read-",0)==0)revokeAt=static_cast<unsigned>(std::stoul(scenario.substr(12)));
    if(scenario.rfind("revive-read-",0)==0){revive=true;revokeAt=static_cast<unsigned>(std::stoul(scenario.substr(12)));}
#if defined(_WIN32)
    ownerThread=thread=GetCurrentThreadId();
#endif
    Make();if(scenario=="off")config=0;if(scenario=="invalid")config=-1;
    if(scenario=="profile")profile=false;if(scenario=="signature")signature=false;
    if(scenario=="publish")publish=false;if(scenario=="stop-before")S::RequestStop();
    S::Start(Base,scenario=="validate",nullptr);
    if(scenario=="off"||scenario=="invalid"||scenario=="validate"||scenario=="profile"||scenario=="signature"||scenario=="helper-signature"||scenario=="stop-before"||scenario=="stop-during"||scenario=="publish"){
        Check(!S::g_ready.load(),"failed or closed startup not operational");Check(!swaps&&!within,"no preflight inventory writes");
    }else if(revokeAt){
        const auto grouped=S::TypeShim();
        if(grouped==1)S::WithinShim();
        Check(writesAfterRevocation==0,"revocation inside memory validation prevents every subsequent custom writer");
        Check(!S::g_callbacks.load(),"revoked callback drained");
    }else if(scenario=="menu"){
        persist=false;Check(!S::MenuAction()&&config==1,"failed config persistence retained");
        persist=true;Check(S::MenuAction()&&config==0,"toggle persists OFF");
    }else{
        Check(S::g_ready.load(),"installed after all dependencies");
        if(scenario=="field")battle=1;if(scenario=="workshop-busy")busyWorkshop=true;
        if(scenario=="wrong-thread")++ownerThread;
        if(scenario=="field"||scenario=="workshop-busy"||scenario=="wrong-thread"){
            Check(S::OwnerShim()==19&&!swaps,"inadmissible operation forwards unchanged");
        }else if(scenario=="swap-exception"){
            bool caught=false;try{S::TypeShim();}catch(const std::runtime_error&){caught=true;}
            Check(caught&&!S::g_ready.load()&&!S::g_callbacks.load(),"native swap exception closes custom mutation and drains");
            Check(GetLastError()==719,"swap exception preserves native LastError");
        }else{
            const auto grouped=S::TypeShim();
            if(scenario=="corrupt-counter")Check(grouped==0&&!S::g_ready.load(),"count readback drift closes mutation admission");
            else{
                Check(grouped==1,"eight-owner grouping completed");
                if(scenario=="owner")Check(S::OwnerShim()==1,"owner sort completed");
                if(scenario=="counters"){
                    S::Counts expected{};Check(S::Count(image,expected),"expected count");
                    Check(refreshes>0&&std::equal(counts.begin(),counts.end(),expected.begin()),"native fourteen counters refreshed after grouping");
                }
                if(scenario!="owner"&&scenario!="type"&&scenario!="counters"){
                    if(scenario=="invalid-slots")image.gear[S::Slot(image.rows[8])][11]=5;
                    if(scenario=="ungrouped")NativeSwap(S::Handle(image.rows[0]),S::Handle(image.rows[19]));
                    changeSession=scenario=="session-drift";stopInside=scenario=="stop-inside";
                    throwInside=scenario=="exception";reenter=scenario=="reentrant";
                    bool caught=false;try{S::WithinShim();}catch(const std::runtime_error&){caught=true;}
                    if(scenario=="invalid-slots"||scenario=="ungrouped")Check(!within,"invalid group rejected before any native sorter");
                    else if(scenario=="corrupt-within"||scenario=="outside-within"||scenario=="row-drift"||scenario=="session-drift"||scenario=="stop-inside"||scenario=="off-on"||scenario=="exception"){
                        Check(within==1,"failure prevents later native groups");Check(!S::g_ready.load(),"uncertain mutation closes admission");
                        if(scenario=="exception")Check(caught&&GetLastError()==729,"native exception and LastError preserved");
                    }else{Check(within>=6,"all eligible native and Seymour groups sorted");CheckIdentities();}
                }else CheckIdentities();
                Check(adjacent==0xdeadbeef,"fourteen counters never expanded into adjacent global");
                Check(!S::g_callbacks.load(),"callbacks drained after return or exception");
                if(scenario=="retire-fail"){retire=false;Check(!S::Remove(),"unretired owner retained");}
            }
        }
    }
    std::printf("SeymourSortAdapterRt1 %s: %u/%u passed (simulated endpoints)\n",scenario.c_str(),checks-failures,checks);
    std::printf("BATTLE_READS=%u WRITES_AFTER_REVOCATION=%u\n",battleReads,writesAfterRevocation);
    return failures?1:0;
}
