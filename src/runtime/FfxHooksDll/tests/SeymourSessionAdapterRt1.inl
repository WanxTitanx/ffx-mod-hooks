// Actual session adapter; configuration, profile and producer endpoints are
// simulated. Windows uses real thread IDs and LastError. No game or files.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
using DWORD=std::uint32_t;
inline thread_local DWORD testLastError=0;
inline DWORD GetLastError(){return testLastError;}
inline void SetLastError(DWORD value){testLastError=value;}
inline DWORD GetCurrentThreadId(){
    static std::atomic<DWORD> serial{1};
    thread_local const DWORD id=serial.fetch_add(1);
    return id;
}
#endif
#include "Config.h"
#include "RonsoPoolSave.h"
#include "SeymourSessionRuntime.h"
#include "NativeSaveLoadEvents.h"

namespace Test {
namespace S=FfxHooks::SeymourSession;
namespace E=FfxHooks::NativeSaveEvents;
namespace R=FfxHooks::RonsoPool;
unsigned checks=0,failures=0,profileCalls=0,pinCalls=0,starts=0,copyCalls=0;
int config=1;
bool profile=true,pinned=true,startResult=true,gateReady=false,ioReady=true;
bool stopAtStart=false,stopAtReadback=false,stopAtThreadId=false;
bool loseProducerAtReadback=false,loseGatewayAtReadback=false;
bool sourceReadable=true,readbackReadable=true,throwCopy=false;
std::string selectedKey="seymour.permanent_roster";
std::array<unsigned char,0x10000> ram{};
R::SaveImage first{},second{};
constexpr std::uintptr_t SaveRva=0xd2ca90u,BattleRva=0xd2a8e0u;
std::uintptr_t Base(){return reinterpret_cast<std::uintptr_t>(ram.data())-BattleRva;}
void* Destination(){return reinterpret_cast<void*>(Base()+SaveRva);}
void Check(bool value,const char* reason){
    ++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}
}
bool Contains(std::uintptr_t at,std::size_t size,const void* data,std::size_t capacity){
    const auto low=reinterpret_cast<std::uintptr_t>(data);
    return size&&at>=low&&at-low<=capacity&&size<=capacity-(at-low);
}
bool Readable(std::uintptr_t at,std::size_t size){
    return Contains(at,size,ram.data(),ram.size())||
        Contains(at,size,first.data(),first.size())||Contains(at,size,second.data(),second.size());
}
DWORD ThreadId(){
    const auto id=GetCurrentThreadId();
    if(stopAtThreadId){stopAtThreadId=false;S::RequestStop();}
    return id;
}
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
const E::Observer primary{Read,Write};
const auto otherObservers=[](){
    std::array<E::Observer,E::kMaximumObservers> values{};
    for(auto& value:values){value.read=Read;value.write=Write;}
    return values;
}();
}

namespace FfxHooks {
namespace Config {
IntReadResult ReadIntExact(const char* key,int,int){
    if(Test::selectedKey!=key)return {IntReadState::Valid,0};
    if(Test::config==-2)return {IntReadState::Missing,0};
    return {Test::config<0?IntReadState::Invalid:IntReadState::Valid,Test::config};
}
}
namespace RonsoPool {bool IsSaveIoReady() noexcept {return Test::ioReady;}}
bool StartNativeSaveLoadEvents(std::uintptr_t base){
    ++Test::starts;Test::Check(base==Test::Base(),"existing load publisher receives the same module");
    Test::gateReady=Test::startResult;
    if(Test::stopAtStart)SeymourSession::RequestStop();
    return Test::startResult;
}
bool NativeSaveLoadEventsReady() noexcept {return Test::gateReady;}
namespace RecoveryNative {
bool Profile(std::uintptr_t base){++Test::profileCalls;return Test::profile&&base==Test::Base();}
bool Pin(const void*){++Test::pinCalls;return Test::pinned;}
bool Range(std::uintptr_t at,std::size_t size){
    const bool readback=at==reinterpret_cast<std::uintptr_t>(Test::Destination());
    if(readback&&Test::stopAtReadback){Test::stopAtReadback=false;SeymourSession::RequestStop();}
    if(readback&&Test::loseProducerAtReadback){Test::loseProducerAtReadback=false;Test::ioReady=false;}
    if(readback&&Test::loseGatewayAtReadback){Test::loseGatewayAtReadback=false;Test::gateReady=false;}
    SetLastError(151);
    return (readback?Test::readbackReadable:Test::sourceReadable)&&Test::Readable(at,size);
}
bool Copy(void* destination,const void* source,std::size_t size){
    ++Test::copyCalls;SetLastError(152);
    if(Test::throwCopy)throw std::runtime_error("simulated copy failure");
    if(!Test::Readable(reinterpret_cast<std::uintptr_t>(source),size))return false;
    std::memcpy(destination,source,size);return true;
}
}
}

// INSERT ACTUAL SESSION ADAPTER

namespace Test {
E::LoadDispatch Begin(const R::SaveImage* source,void* destination=nullptr){
    SetLastError(70);
    auto frame=E::BeginLoad(destination?destination:Destination(),source?source->data():nullptr);
    Check(GetLastError()==70,"load-start callback preserves LastError");
    return frame;
}
void NativeCopy(const R::SaveImage& source){
    // Explicit native endpoint simulation: the production observer never writes.
    std::memcpy(Destination(),source.data()+64,source.size()-64);
}
void End(E::LoadDispatch& frame,bool completed=true){
    SetLastError(71);E::EndLoad(frame,completed);
    Check(GetLastError()==71,"load-complete callback preserves LastError");
}
S::Token Load(const R::SaveImage& source){
    auto frame=Begin(&source);Check(frame.cookie!=0,"active load dispatch captured");
    NativeCopy(source);End(frame);return S::Capture();
}
void NoSession(const char* reason){Check(!S::Capture().Valid()&&!S::Capture(true).Valid(),reason);}
void Setup(){
    for(std::size_t i=64;i<first.size();++i)first[i]=static_cast<unsigned char>(i*17u+3u);
    R::SealSave(first);second=first;second[123]^=1;R::SealSave(second);
    Check(R::IsValidSave(first)&&R::IsValidSave(second),"actual save checksum fixtures valid");
}
}

int main(int argc,char** argv){
    using namespace Test;
    if(argc!=2)return 2;
    const std::string scenario=argv[1];
    Setup();
    if(scenario=="off")config=0;
    if(scenario=="invalid")config=-1;
    if(scenario=="missing")config=-2;
    if(scenario=="sort-key")selectedKey="seymour.gear_sorting";
    if(scenario=="grid-key")selectedKey="seymour.grid8";
    if(scenario=="profile")profile=false;
    if(scenario=="pin")pinned=false;
    if(scenario=="gateway")startResult=false;
    if(scenario=="stop-before")S::RequestStop();
    if(scenario=="stop-during-start")stopAtStart=true;
    if(scenario=="io-start-late")ioReady=false;
    if(scenario=="primary")Check(E::Subscribe(&primary),"Workshop primary registered");
    if(scenario=="full")for(const auto& observer:otherObservers)
        Check(E::SubscribeAdditional(&observer),"existing observer slot retained");
    S::PrimeSaveIo(Base(),scenario=="validate");
    const bool inactive=config<=0||scenario=="validate"||scenario=="profile"||scenario=="pin"||
        scenario=="gateway"||scenario=="full"||scenario=="stop-before"||scenario=="stop-during-start";
    if(inactive){
        Check(!S::PublisherReady(),"failed or disabled admission is not ready");NoSession("no active session after failed admission");
        if(scenario=="gateway"||scenario=="stop-during-start")Check(starts==1&&!E::Requested(),"failed publication removes only its observer");
        else Check(starts==0,"preflight failure never starts a publisher");
        if(config<=0||scenario=="validate"||scenario=="stop-before")Check(profileCalls==0&&pinCalls==0,"OFF/validate/stop have no installation work");
    }else{
        if(scenario=="io-start-late"){
            Check(!S::PublisherReady(),"requested infrastructure is not operational before the producer");ioReady=true;
        }
        Check(S::PublisherReady(),"both existing producers ready");
        S::PrimeSaveIo(Base(),false);Check(starts==1,"idempotent subscription and publisher start");
        NoSession("installation alone does not admit a save");
        const auto token=Load(first);Check(token.Valid()&&token.thread==GetCurrentThreadId(),"completed exact payload owns the current thread");
        if(scenario=="load"||scenario=="sort-key"||scenario=="grid-key"||scenario=="io-start-late"){
            const auto next=Load(second);Check(next.Valid()&&next.revision!=token.revision&&!S::Current(token),"each actual load has a new revision");
        }else if(scenario=="preview"){
            E::ReadStarting(first.data());E::ReadCompleted(L"preview",first.data(),first.data(),first.size());
            auto frame=Begin(&second,ram.data());End(frame);
            Check(S::Current(token),"file preview and different load destination do not change the active save");
        }else if(scenario=="null-source"){
            auto frame=Begin(nullptr);NoSession("null source retires the prior session before native execution");End(frame,false);
            NoSession("failed null-source load cannot revive a session");
        }else if(scenario=="wrong-thread"){
            auto frame=Begin(&second);NativeCopy(second);
            std::thread worker([&]{End(frame);});worker.join();NoSession("completion on another thread is rejected");
        }else if(scenario=="wrong-capture-thread"){
            bool accepted=true;std::thread worker([&]{accepted=S::Capture().Valid()||S::Current(token);});worker.join();
            Check(!accepted&&S::Current(token),"read-only query on another thread cannot capture or revoke the owner");
        }else if(scenario=="nested"){
            auto outer=Begin(&first),inner=Begin(&second);NativeCopy(second);End(inner);NoSession("inner copy cannot admit through an outer load");
            NativeCopy(first);End(outer);NoSession("older outer copy cannot admit a newer revision");
            Check(Load(second).Valid(),"a fresh complete load recovers after nested rejection");
        }else if(scenario=="reset-pending"){
            auto frame=Begin(&second);E::ResetCompleted();NativeCopy(second);End(frame);NoSession("reset during a pending load invalidates both claims");
        }else if(scenario=="reset"){
            SetLastError(72);E::ResetCompleted();Check(GetLastError()==72,"new-game callback preserves LastError");
            const auto fresh=S::Capture();Check(fresh.Valid()&&fresh.revision!=token.revision&&!S::Current(token),"new-game reset owns a fresh transient session");
        }else if(scenario=="stop-during-reset"){
            stopAtThreadId=true;E::ResetCompleted();NoSession("stop between reset admission and publication cannot admit cleanup for a new session");
        }else if(scenario=="stop-during-readback"){
            auto frame=Begin(&second);NativeCopy(second);stopAtReadback=true;End(frame);
            NoSession("stop inside readback cannot publish a new cleanup session");
        }else if(scenario=="stop-pending"){
            auto frame=Begin(&second);NativeCopy(second);S::RequestStop();End(frame);NoSession("stop rejects a pending load completion");
        }else if(scenario=="stop-cleanup"){
            S::RequestStop();Check(!S::Capture().Valid()&&S::Current(token,true),"stop preserves only already-owned cleanup");
            Check(S::Remove()&&!E::Requested()&&!S::Current(token,true),"normal removal retires cleanup identity before future loads become unobserved");
        }else if(scenario=="stop-during-capture"){
            stopAtThreadId=true;Check(!S::Capture().Valid(),"stop inside thread lookup cannot return a new action token");
            Check(S::Current(token,true),"capture revocation preserves cleanup for the already-owned session");
        }else if(scenario=="producer-during-readback"||scenario=="gateway-during-readback"){
            auto frame=Begin(&second);NativeCopy(second);
            loseProducerAtReadback=scenario=="producer-during-readback";
            loseGatewayAtReadback=scenario=="gateway-during-readback";
            End(frame);NoSession("dependency loss during native readback rejects admission");
            ioReady=gateReady=true;NoSession("restoring a producer does not validate the rejected load retroactively");
        }else if(scenario=="producer-reset"||scenario=="gateway-reset"){
            if(scenario=="producer-reset")ioReady=false;else gateReady=false;
            E::ResetCompleted();ioReady=gateReady=true;
            NoSession("a reset observed without its publishers cannot expose a new session later");
        }else if(scenario=="captured-after-remove"){
            auto frame=Begin(&second);Check(S::Remove(),"subscriber removal is nonblocking");NativeCopy(second);End(frame);
            NoSession("captured completion remains safe after unsubscribe and never readmits");
        }else if(scenario=="producer-lost"){
            ioReady=false;Check(!S::PublisherReady()&&!S::Capture().Valid(),"lost producer closes admission");
            ioReady=true;Check(S::Current(token),"temporary producer readiness does not invent a new session");
        }else if(scenario=="gateway-lost"){
            gateReady=false;Check(!S::PublisherReady()&&!S::Capture().Valid(),"lost load gateways close admission");
        }else if(scenario=="replay"){
            auto frame=Begin(&first);auto replay=frame;NativeCopy(first);End(frame);
            const auto latest=Load(second);End(replay);Check(S::Current(latest),"replayed completion cannot revoke a later valid load");
        }else if(scenario=="crc-cleared"){
            for(unsigned i=0;i<4;++i)second[25844+i]=0;
            Check(Load(second).Valid(),"native four-byte CRC clear is validated without editing its source");
        }else if(scenario=="primary"){
            Check(E::Subscribed(&primary),"Seymour never replaces the registered Workshop observer");
            Check(S::Remove()&&E::Subscribed(&primary),"Seymour removal preserves Workshop registration");
        }else if(scenario=="capacity"){
            std::array<E::LoadDispatch,17> frames{};for(auto& frame:frames)frame=Begin(&first);
            for(auto& frame:frames)End(frame,false);
            NoSession("exhausted pending capacity fails closed");
            (void)Load(second);NoSession("capacity poison is not silently reset by another load");
        }else{
            if(scenario=="source-fault")sourceReadable=false;
            if(scenario=="copy-exception")throwCopy=true;
            if(scenario=="invalid-crc")second[99]^=1;
            auto frame=Begin(&second);NativeCopy(second);
            if(scenario=="readback-fault")readbackReadable=false;
            if(scenario=="corrupt-first")static_cast<unsigned char*>(Destination())[0]^=1;
            if(scenario=="corrupt-last")static_cast<unsigned char*>(Destination())[R::kSaveSize-65]^=1;
            const bool known=scenario=="source-fault"||scenario=="copy-exception"||scenario=="invalid-crc"||
                scenario=="readback-fault"||scenario=="corrupt-first"||scenario=="corrupt-last"||scenario=="incomplete";
            Check(known,"scenario is explicitly covered");End(frame,scenario!="incomplete");NoSession("failed source, copy or completion is not an active save");
        }
    }
    std::printf("SeymourSessionAdapterRt1 %s: %u/%u passed (actual adapter; simulated endpoints)\n",scenario.c_str(),checks-failures,checks);
    return failures?1:0;
}
