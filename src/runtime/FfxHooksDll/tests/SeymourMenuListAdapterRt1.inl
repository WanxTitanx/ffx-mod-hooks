// Actual adapter source, with explicit simulated native/platform endpoints.
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#define __cdecl
using DWORD=unsigned;
static DWORD lastError=0;
static DWORD GetLastError(){return lastError;}
static void SetLastError(DWORD value){lastError=value;}
static DWORD GetCurrentThreadId(){return 7;}
#endif
#include "SeymourMenuListService.h"
#include "SeymourSessionRuntime.h"
#include "SeymourOverdriveControl.h"
#include "MinHookBatchCoordinator.h"
#include "RecoveryEvidence.generated.h"
#include "F8FlagCatalog.h"
#include "Config.h"
namespace FfxHooks::SeymourMenuList {void RequestStop() noexcept;void PresentTick();}
namespace Test {
namespace M=FfxHooks::SeymourMenuList;namespace S=FfxHooks::SeymourSession;
unsigned checks=0,failed=0,adds=0,publishes=0,originals=0,writes=0,reads=0,edge=0;
int config=1;
bool master=true,profile=true,signature=true,pin=true,publisher=true,create=true,publish=true,retire=true,persist=true;
bool live=true,stopPublish=false,readFailure=false,writeFailure=false,shortWrite=false,revokeWrite=false,saveWrite=false;
bool revokeNative=false,saveNative=false,revive=false,revoked=false,throwNative=false,reentrant=false,foreign=false;
std::uint64_t revision=1;unsigned owner=0;std::uint8_t battle=0;
M::Roster roster{};M::List image{},native{};
constexpr std::uintptr_t Base=0x400000,Menu=0x1841bd4,Front=0x11307e8,Reserve=0x11307eb,Player=0x113205c,Battle=0x112a8e0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",why);}}
template<class F>struct Cleanup {F f;~Cleanup(){f();}};
template<class F>Cleanup<F> OnExit(F f){return {f};}
void __cdecl Native(unsigned);void AtRange();void AfterWrite();
void* Backing(std::uintptr_t address,std::size_t size){
 if(address>=Menu&&address-Menu<=sizeof(image)&&size<=sizeof(image)-(address-Menu))return reinterpret_cast<unsigned char*>(&image)+(address-Menu);
 if(address>=Front&&address-Front<3&&size<=3-(address-Front))return roster.front.data()+(address-Front);
 if(address>=Reserve&&address-Reserve<17&&size<=17-(address-Reserve))return roster.reserve.data()+(address-Reserve);
 if(address==Battle&&size==1)return &battle;
 for(unsigned i=0;i<8;++i)if(address==Player+i*0x94u+0x2cu&&size==1)return roster.flags.data()+i;
 return nullptr;
}
}
namespace FfxHooks {
const F8FlagSpec* FindF8Flag(const char*){static const F8FlagSpec value{};return &value;}
Config::BoolGateResult ResolveF8Flag(const F8FlagSpec&){return {Test::master,Config::BoolSource::DefaultValue};}
namespace Config {
IntReadResult ReadIntExact(const char*,int,int){return {Test::config<0?IntReadState::Invalid:IntReadState::Valid,Test::config};}
bool SetInt(const char*,int value){if(!Test::persist)return false;Test::config=value;return true;}
}
namespace SeymourSession {
bool PublisherReady() noexcept{return Test::publisher;}
Token Capture(bool) noexcept{return Test::live?Token{Test::revision,Test::owner}:Token{};}
bool Current(const Token& t,bool) noexcept{return Test::live&&t.revision==Test::revision&&t.thread==Test::owner;}
}
namespace RecoveryNative {
bool Profile(std::uintptr_t b){return b==Test::Base&&Test::profile;}
bool Pin(const void*){return Test::pin;}
bool Match(std::uintptr_t,const RecoveryEvidence::Proof& p){Test::Check(p.rva==0x4a8ef0&&p.size>=20,"specific native constructor signature");return Test::signature;}
bool Range(std::uintptr_t at,std::size_t size,std::uintptr_t=0,bool code=false,bool write=false){
 if(code)return at==Test::Base+0x4a8ef0;
 Test::AtRange();SetLastError(121);
 if(Test::readFailure&&at==Test::Menu&&!write)return false;
 if(Test::writeFailure&&write)return false;
 return Test::Backing(at,size)!=nullptr;
}
bool Copy(void* out,const void* in,std::size_t size){
 SetLastError(122);
 if(void* destination=Test::Backing(reinterpret_cast<std::uintptr_t>(out),size)){
  ++Test::writes;
  if(Test::shortWrite){Test::shortWrite=false;std::memcpy(destination,in,size/2);return false;}
  std::memcpy(destination,in,size);Test::AfterWrite();return true;
 }
 if(void* source=Test::Backing(reinterpret_cast<std::uintptr_t>(in),size)){std::memcpy(out,source,size);return true;}
 return false;
}
class OwnedBatch {
public:
 bool Add(std::uintptr_t address,void*,void** original){++Test::adds;Test::Check(address==Test::Base+0x4a8ef0,"one menu constructor target");if(!Test::create)return false;*original=reinterpret_cast<void*>(&Test::Native);return true;}
 bool Publish(MinHookBatch::Owner owner,const void*){++Test::publishes;Test::Check(owner==MinHookBatch::Owner::SeymourMenuList,"independent menu owner");if(Test::stopPublish)SeymourMenuList::RequestStop();return Test::publish;}
 bool DiscardUnpublished(){return true;}
 bool Neutralize(){return Test::retire;}
};
}
}
// INSERT ACTUAL MENU ADAPTER
namespace Test {
void AtRange(){++reads;if(edge&&reads==edge){config=0;M::PresentTick();if(revive){config=1;M::PresentTick();}revoked=true;}}
void AfterWrite(){
 if(revokeWrite){revokeWrite=false;config=0;M::PresentTick();}
 if(saveWrite){saveWrite=false;++revision;image.selection=123;}
}
void __cdecl Native(unsigned mode){
 ++originals;SetLastError(77);
 if(throwNative){
#ifdef _WIN32
  RaiseException(0xe0523811,0,0,nullptr);
#else
  throw std::runtime_error("native constructor failure");
#endif
 }
 // Independent expected native result for this fixture, not the tested builder.
 if((mode&0xffff0000u)==0x10000u){image.current=image.total=image.frontline=image.selection=image.frontMask=image.currentMask=image.totalMask=0;}
 else{for(unsigned i=0;i<7;++i)image.rows[i]=static_cast<unsigned char>(i);image.current=image.total=7;image.frontline=2;image.selection=0;image.frontMask=3;image.currentMask=image.totalMask=127;}
 native=image;
 if(foreign)image.currentMask^=1;
 if(revokeNative){config=0;M::PresentTick();if(revive){config=1;M::PresentTick();}}
 if(saveNative)++revision;
 if(reentrant){reentrant=false;M::ConstructorShim(mode);}
 SetLastError(77);
}
bool CatchNative(){
#ifdef _WIN32
 __try{M::ConstructorShim(0);return false;}__except(EXCEPTION_EXECUTE_HANDLER){return true;}
#else
 try{M::ConstructorShim(0);return false;}catch(...){return true;}
#endif
}
}
int main(int argc,char** argv){
 using namespace Test;
 if(argc!=2)return 2;
 const std::string s=argv[1];owner=GetCurrentThreadId();
 roster.front={0,1,7};roster.reserve.fill(255);
 for(unsigned i=0;i<5;++i)roster.reserve[i]=static_cast<unsigned char>(i+2);
 roster.flags.fill(0x10);image.rows.fill(255);
 if(s=="off")config=0;
 if(s=="invalid")config=-1;
 if(s=="master-off")master=false;
 if(s=="profile")profile=false;
 if(s=="signature")signature=false;
 if(s=="pin")pin=false;
 if(s=="publisher")publisher=false;
 if(s=="create")create=false;
 if(s=="publish")publish=false;
 if(s=="stop-publish")stopPublish=true;
 if(s=="stop-before")M::RequestStop();
 M::Start(Base,s=="validate",nullptr);
 const bool inactive=config<=0||s=="validate"||!profile||!signature||!pin||!publisher||!create||!publish||stopPublish||s=="stop-before";
 if(inactive){
  Check(!M::g_ready.load(),"disabled, failed or stopped startup not installed");
  if(config<=0||s=="validate"||!profile||!signature||!pin||!publisher||s=="stop-before")Check(!adds,"preflight before creation");
 }else{
  Check(M::g_ready.load()&&adds==1&&publishes==1,"single complete installation");
  M::Start(Base,false,nullptr);Check(adds==1,"installation one-shot");
  if(s=="no-session")live=false;
  if(s=="wrong-thread")++owner;
  if(s=="battle")battle=1;
  if(s=="off-after"){config=0;M::PresentTick();}
  revokeNative=s=="revoke-native"||s=="revive-native";
  revive=s=="revive-native";saveNative=s=="load-native";
  throwNative=s=="original-exception";foreign=s=="foreign";
  readFailure=s=="read-failure";writeFailure=s=="write-failure";
  shortWrite=s=="short-write";revokeWrite=s=="post-write-revoke";
  saveWrite=s=="post-write-save";reentrant=s=="reentry";
  if(s.rfind("revoke-read-",0)==0||s.rfind("revive-read-",0)==0){
   edge=static_cast<unsigned>(std::stoul(s.substr(12)));
   revive=s.rfind("revive",0)==0;reads=0;
  }
  if(throwNative)Check(CatchNative(),"original exception propagates");
  else M::ConstructorShim(s=="mode-hidden"?0x10000u:0u);
  Check(originals==(s=="reentry"?2u:1u),"original once per invocation");
  Check(GetLastError()==77,"original LastError preserved");
  Check(M::g_callbacks.load()==0,"callback accounting drained");
  if(edge){
   Check(revoked,"requested boundary reached");
   Check(image==native,"revocation cannot leave an unowned extended menu");
   }else if(s=="call"||s=="menu"||s=="remove"||s=="retire-fail"||s=="disable-cleanup"||s=="wrong-thread-cleanup"||s=="changed-menu-cleanup"||s.rfind("new-save-",0)==0){
   Check(image.total==8&&image.current==8&&image.frontline==3&&image.rows[2]==7&&M::ValidList(image),"coherent eight-character list");
  }else if(s=="post-write-save"){
   Check(image.selection==123&&!M::g_ready.load(),"new save menu never overwritten by old cleanup");
  }else if(s=="foreign"){
   Check(!writes&&image.currentMask==(native.currentMask^1u),"foreign native result untouched");
  }else if(!throwNative){Check(image==native,"rejected/restored frame preserves native output");}
  if(s=="menu"){persist=false;Check(!M::MenuAction(),"persistence failure visible");}
  if(s=="disable-cleanup"){
   config=0;M::PresentTick();M::PumpTick();
   Check(image==native,"owner pump restores the exact menu after disabling");
  }
  if(s=="wrong-thread-cleanup"){
   const auto extended=image;++owner;
   Check(!M::Remove()&&image==extended,"foreign thread cannot retire owned menu data");
   --owner;Check(M::Remove()&&image==native,"owner can finish deferred restoration");
  }
   if(s=="changed-menu-cleanup"){
   image.selection=1;const auto changed=image;
   Check(!M::Remove()&&image==changed,"menu changed by a consumer is not blindly restored");
   M::ConstructorShim(0);
    Check(M::Remove()&&image==native,"successful native reconstruction retires old ownership");
   }
   if(s.rfind("new-save-",0)==0){
    const auto beforeWrites=writes;
    image.selection=93;image.rows[7]=0xe1;
    const auto nextImage=image;
    if(s=="new-save-pending-cleanup"){
     live=false;
     Check(!M::Remove()&&image==nextImage&&writes==beforeWrites,"unconfirmed load defers cleanup without writes");
     live=true;
    }
    ++revision;
    if(s=="new-save-wrong-thread-cleanup"){
     ++owner;
     Check(!M::Remove()&&image==nextImage&&writes==beforeWrites,"foreign thread cannot retire the old lease");
     --owner;
    }
    Check(M::Remove(),"confirmed new save retires obsolete menu ownership");
    Check(image==nextImage&&writes==beforeWrites,"old menu baseline never overwrites the new save view");
   }
  if(s=="remove"||s=="retire-fail"){
   retire=s=="remove";
   Check(M::Remove()==retire&&!M::g_ready.load(),"retirement reflects ownership");
   Check(image==native,"owned menu data restored before detour retirement");
   M::ConstructorShim(0);Check(image==native,"retained shim forwards after stop");
  }
  if(s=="call")std::printf("MENU_READS=%u\n",reads);
 }
 std::printf("SeymourMenuListAdapterRt1 %s: %u/%u passed (actual adapter; simulated endpoints)\n",s.c_str(),checks-failed,checks);
 return failed?1:0;
}
