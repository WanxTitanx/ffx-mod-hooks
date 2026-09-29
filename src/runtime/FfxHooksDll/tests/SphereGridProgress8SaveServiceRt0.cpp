// Jarvis-HOOK: real publisher, checksum, queue and consumer; explicit disk/native endpoints.
#include <cstdio>
#if !__has_include("../hooks/SphereGridProgress8SaveService.h")
int main(){std::puts("FAIL: Grid8 save service is missing");return 1;}
#else
#include "../hooks/SphereGridProgress8SaveService.h"
#include "../hooks/NativeSaveCommitAdapter.h"
#include <memory>
namespace S=FfxHooks::SphereGridProgress8Save;
namespace C=FfxHooks::SphereGridProgress8Commit;
namespace G=FfxHooks::SphereGridProgress8;
namespace E=FfxHooks::NativeSaveEvents;
namespace R=FfxHooks::RonsoPool;
namespace N=FfxHooks::NativeSaveCommit;
static unsigned checks=0,failures=0;
static void Check(bool v,const char* reason){++checks;if(!v){++failures;std::printf("FAIL: %s\n",reason);}}
struct Fixture {
    S::Service service;
    C::Scope scope{11,7,13,17};
    G::Key key{};
    G::Snapshot state,saved;
    unsigned writes=0,closeCount=0,captures=0;
    bool active=true,captureOk=true,identifyOk=true,closeOk=true,readOk=true;
    unsigned interrupt=0;
    R::SaveImage image{};
    S::Persist diskResult=S::Persist::Written;
    S::Persist last=S::Persist::Rejected;
    Fixture(){
        key.path.fill(1);key.image.fill(2);key.layout.fill(3);key.contents.fill(4);
        state.nodes.resize(1003,{1,0x81});state.links.resize(1024,0x80);
        state.cursors.fill(860);state.cursors[7]=1002;R::SealSave(image);
    }
    void Interrupt(unsigned point){
        if(interrupt==point){interrupt=0;service.Invalidate();}
    }
    static bool Describe(void* p,C::Scope& scope,G::Key& key){
        auto& f=*static_cast<Fixture*>(p);scope=f.scope;key=f.key;f.Interrupt(1);return f.active;
    }
    static bool Capture(void* p,const C::Scope&,G::Snapshot& out){
        auto& f=*static_cast<Fixture*>(p);++f.captures;out=f.state;f.Interrupt(2);return f.captureOk;
    }
    static bool Identify(void* p,const wchar_t* path,const R::SaveImage& image,
                         FfxHooks::SphereGridProgress::Hash& hp,FfxHooks::SphereGridProgress::Hash& hi){
        auto& f=*static_cast<Fixture*>(p);hp=f.key.path;hi=f.key.image;
        f.Interrupt(3);return f.identifyOk&&path&&*path&&R::IsValidSave(image);
    }
    static bool Current(void* p,const C::Scope& scope,const G::Key& key){
        auto& f=*static_cast<Fixture*>(p);f.Interrupt(4);
        return f.active&&scope==f.scope&&key.layout==f.key.layout&&key.contents==f.key.contents;
    }
    static S::Persist Publish(void* p,const G::Key& key,const G::Snapshot& state,const S::Admission& admission){
        auto& f=*static_cast<Fixture*>(p);
        Check(f.closeCount==1,"companion disk publication only follows successful original close");
        f.Interrupt(5);
        if(!admission.current(admission.context,key))return S::Persist::Rejected;
        ++f.writes;f.saved=state;
        Check(!f.service.Stage(99,L"fixture",f.image),"reentrant staging never captures during disk publication");
        auto wrong=key;wrong.path[0]^=1;
        Check(!admission.current(admission.context,wrong),"disk admission checks the exact publication key");
        if(f.interrupt==6){f.service.RequestStop();f.interrupt=0;}
        return f.diskResult;
    }
    bool Configure(){return service.Configure({this,Describe,Capture,Identify,Current,Publish});}
};
static Fixture* currentFixture=nullptr;
static void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
static void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
static void Staged(std::uint64_t ticket,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(size!=R::kSaveSize)return;
    R::SaveImage image{};std::memcpy(image.data(),bytes,size);
    currentFixture->service.Stage(ticket,path,image);
}
static void Verified(std::uint64_t ticket,const wchar_t* path,const unsigned char* bytes,std::size_t size) noexcept {
    if(size!=R::kSaveSize)return;
    R::SaveImage image{};std::memcpy(image.data(),bytes,size);
    currentFixture->last=currentFixture->service.Verified(ticket,path,image);
}
static void Aborted(std::uint64_t ticket) noexcept {currentFixture->service.Abort(ticket);}
static bool Ready() noexcept {return true;}
static std::uint32_t Epoch() noexcept {return 1;}
static std::uint32_t Thread() noexcept {return 7;}
static std::uint32_t Error() noexcept {return 0;}
static void SetError(std::uint32_t) noexcept {}
static int* Number() noexcept {return nullptr;}
static bool File(void*,N::FileIdentity& id) noexcept {id={1,1};return true;}
static bool Readback(const wchar_t*,N::FileIdentity& id,R::SaveImage& out) noexcept {
    id={1,1};out=currentFixture->image;return currentFixture->readOk;
}
static int Close(void*){++currentFixture->closeCount;return currentFixture->closeOk?0:-1;}
int main(){
    const auto observer=[](){E::Observer o{Read,Write};o.writeStaged=Staged;o.writeVerified=Verified;o.writeAborted=Aborted;return o;}();
    Check(E::Subscribe(&observer),"existing shared publisher accepts the consumer");
    for(unsigned mode=0;mode<23;++mode){
        Fixture f;currentFixture=&f;Check(f.Configure(),"valid immutable endpoint binding");
        Check(!f.Configure(),"service endpoints cannot be swapped after configuration");
        auto publisher=std::make_unique<N::Runtime>();
        Check(publisher->Configure({Ready,Epoch,Thread,File,Readback,Error,SetError,Number}),"actual native commit publisher configured");
        if(mode==1)f.active=false;
        if(mode==2)f.captureOk=false;
        if(mode==3)f.identifyOk=false;
        if(mode>=4&&mode<=7)f.interrupt=mode-3;
        auto ticket=publisher->Stage(reinterpret_cast<void*>(0x1000),L"fixture",f.image);
        Check(ticket.Valid()&&f.writes==0,"buffered native save never implies companion disk success");
        const auto original=f.state;
        f.state.nodes[0].mask=0;f.state.cursors[7]=0;
        publisher->Finish(ticket,mode!=8);
        if(mode==9)f.closeOk=false;
        if(mode==10)f.readOk=false;
        if(mode==11){f.image[80]^=1;R::SealSave(f.image);}
        if(mode==12)f.service.Invalidate();
        if(mode==13)f.service.RequestStop();
        if(mode==14)++f.scope.session;
        if(mode==15)++f.scope.thread;
        if(mode==16)++f.scope.layoutGeneration;
        if(mode==17)++f.scope.request;
        if(mode==18)f.key.layout[0]^=1;
        if(mode==19)f.key.image[0]^=1;
        if(mode==20)f.interrupt=5;
        if(mode==21)f.diskResult=S::Persist::Unavailable;
        if(mode==22)f.interrupt=6;
        publisher->Close(reinterpret_cast<void*>(0x1000),Close);
        const bool published=mode==0||mode==21||mode==22;
        Check(f.writes==(published?1u:0u),"only matching admitted confirmations reach disk");
        if(published)Check(f.saved==original,"consumer publishes the frozen snapshot, not changed live progress");
        if(mode==0)Check(f.last==S::Persist::Written,"successful disk confirmation is reported");
        if(mode==21)Check(f.last==S::Persist::Unavailable,"disk failure is not reported as saved");
        if(mode==22)Check(f.last==S::Persist::Retired,"stop during disk publication does not report current-session success");
        const auto writes=f.writes;E::WriteVerified(ticket.serial,L"fixture",f.image.data(),f.image.size());
        Check(f.writes==writes,"repeated verified callback cannot republish");
    }
    E::Unsubscribe(&observer);currentFixture=nullptr;
    Check(!E::Requested(),"consumer teardown preserves registry ownership");
    std::printf("SphereGridProgress8SaveServiceRt0: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
#endif
