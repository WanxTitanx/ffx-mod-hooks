// Jarvis-HOOK: actual consumer/registry/store/hashes; explicit session/profile endpoints.
#include <cstdio>
#if !__has_include("../hooks/SphereGridProgress8Runtime.h")
int main(){std::puts("FAIL: Grid8 runtime subscriber is missing");return 1;}
#else
#include "../hooks/SphereGridProgress8Runtime.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/GridLearnedStore.h"
#include "../hooks/SeymourSessionRuntime.h"
#include "../hooks/F8FlagCatalog.h"
#include <fstream>
#include <filesystem>
#include <thread>
namespace P=FfxHooks::SphereGridProgress8Runtime;
namespace G=FfxHooks::SphereGridProgress8;
namespace E=FfxHooks::NativeSaveEvents;
namespace R=FfxHooks::RonsoPool;
namespace SS=FfxHooks::SeymourSession;
static unsigned checks=0,failures=0;
static int configured=1;
static bool master=true,producer=true,profile=true,pinned=true,sourceReady=true,corrupt=false,stopCapture=false;
static SS::Token session{};
static std::uint64_t generation=1;
static G::Snapshot nativeState;
static G::Key layoutKey;
static void Check(bool value,const char* reason){++checks;if(!value){++failures;std::printf("FAIL: %s\n",reason);}}
namespace FfxHooks::Config {
IntReadResult ReadIntExact(const char* key,int,int){
    return {configured<0?IntReadState::Invalid:IntReadState::Valid,
        std::strcmp(key,"seymour.grid8_persistence")==0?configured:0};
}
bool SetInt(const char*,int value){configured=value;return true;}
}
namespace FfxHooks {
const F8FlagSpec* FindF8Flag(const char*){static const F8FlagSpec flag{};return &flag;}
Config::BoolGateResult ResolveF8Flag(const F8FlagSpec&){return {master,Config::BoolSource::DefaultValue};}
namespace RonsoPool {bool IsVerifiedSaveIoReady() noexcept {return producer;}}
namespace SeymourSession {
Token Capture(bool) noexcept {return GetCurrentThreadId()==session.thread?session:Token{};}
bool Current(const Token& token,bool) noexcept {return token.Valid()&&token==session&&GetCurrentThreadId()==token.thread;}
}
}
static bool Profile(std::uintptr_t){return profile;}
static bool Pin(const void*){return pinned;}
static bool Describe(void*,P::Layout& out) noexcept {
    out={generation,layoutKey.layout,layoutKey.contents};return sourceReady;
}
static bool Capture(void*,const P::Layout&,G::Snapshot& out) noexcept {
    try {out=nativeState;if(corrupt)out.cursors[7]=5000;if(stopCapture)P::RequestStop();return true;}
    catch(...){return false;}
}
static void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {}
static void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
static bool LeafCount(const std::wstring& directory,unsigned expected){
    unsigned count=0;
    if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory))
        if(entry.path().extension()==L".sgp2")++count;
    return count==expected;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const std::wstring root=argv[1],mode=argv[2],directory=root+L"\\records",path=root+L"\\ffx_000";
    if(std::filesystem::exists(root))return 3;
    std::filesystem::create_directory(root);
    session={11,GetCurrentThreadId()};
    layoutKey.layout.fill(3);layoutKey.contents.fill(4);
    nativeState.nodes.resize(1003,{1,0x81});nativeState.links.resize(1024,0x80);
    nativeState.cursors.fill(860);nativeState.cursors[7]=1002;
    const P::Source source{nullptr,Describe,Capture};
    if(mode!=L"source-absent")Check(P::RegisterSource(&source),"one process-lifetime native source registers");
    Check(P::SetGatesForTests(Profile,Pin),"test-only profile/pin endpoints configured before startup");
    if(mode==L"off")configured=0;
    if(mode==L"invalid")configured=-1;
    if(mode==L"master-off")master=false;
    if(mode==L"profile")profile=false;
    if(mode==L"pin")pinned=false;
    if(mode==L"unready")producer=false;
    if(mode==L"source-unready")sourceReady=false;
    if(mode==L"corrupt")corrupt=true;
    if(mode==L"stop-capture")stopCapture=true;
    const E::Observer primary{Read,Write};Check(E::Subscribe(&primary),"existing save owner registered");
    P::PrimeSaveIo(0x400000,mode==L"validate",directory.c_str());
    R::SaveImage image{};R::SealSave(image);const auto before=image;
    SetLastError(123);E::WriteStaged(1,path.c_str(),image.data(),image.size());
    Check(GetLastError()==123,"staging preserves LastError");
    Check(LeafCount(directory,0),"staging creates no progress record");
    const auto frozen=nativeState;nativeState.cursors[7]=0;nativeState.nodes[0].mask=0;
    if(mode==L"abort")E::WriteAborted(1);
    if(mode==L"reset")E::ResetCompleted();
    if(mode==L"load")for(const auto* observer:E::CaptureObservers())if(observer&&observer->loadStarting)
        observer->loadStarting(2,reinterpret_cast<void*>(0x400000+0xd2ca90),nullptr);
    if(mode==L"revoked"){configured=0;P::PresentTick();configured=1;P::PresentTick();}
    if(mode==L"stop")P::RequestStop();
    if(mode==L"session")++session.revision;
    if(mode==L"layout")++generation;
    if(mode==L"image"){image[77]^=1;R::SealSave(image);}
    SetLastError(124);
    if(mode==L"thread"){
        std::thread other([&]{E::WriteVerified(1,path.c_str(),image.data(),image.size());});other.join();
    }else E::WriteVerified(1,path.c_str(),image.data(),image.size());
    Check(GetLastError()==124,"confirmation preserves caller LastError");
    const bool expected=mode==L"normal";
    Check(LeafCount(directory,expected?1u:0u),"only complete current native state reaches disk");
    if(expected){
        G::Key key=layoutKey;std::wstring canonical;
        Check(FfxHooks::GridLearned::CanonicalSavePath(path.c_str(),canonical)&&
            FfxHooks::GridLearned::Fingerprint(canonical.data(),canonical.size()*sizeof(wchar_t),key.path)&&
            FfxHooks::GridLearned::Fingerprint(image.data(),image.size(),key.image),"actual canonical path and SHA-256 identities");
        for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".sgp2"){
            std::ifstream file(entry.path(),std::ios::binary);G::Bytes bytes((std::istreambuf_iterator<char>(file)),{});
            G::Snapshot decoded;Check(G::Decode(bytes,key,decoded)&&decoded==frozen,"real record contains the frozen eight-character snapshot");
        }
        const auto status=P::GetStatus();Check(status.written==1&&status.subscribed,"actual subscriber reports confirmed disk success");
    }
    E::WriteVerified(1,path.c_str(),image.data(),image.size());
    Check(LeafCount(directory,expected?1u:0u),"replay creates no additional record");
    Check(mode==L"image"||image==before,"native serialized image is never changed by the consumer");
    Check(P::Remove()&&E::Subscribed(&primary),"consumer removal preserves the other save owner");
    E::Unsubscribe(&primary);Check(!E::Requested(),"no leaked observer slot");
    std::printf("SphereGridProgress8RuntimeRt1: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
#endif
