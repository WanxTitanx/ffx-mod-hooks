#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/OmegaLastSealNotice.h"
#include "../hooks/OmegaLastSealEvidence.h"
#include <cstdio>
#include <cstring>
namespace M=FfxHooks::OmegaLastSeal::BattleNotice;namespace O=FfxHooks::OmegaLastSeal;
static bool portuguese=false,font=false,encodingWorks=true;static unsigned encodes=0,fallthroughs=0,checks=0,failures=0;static std::uintptr_t base=0;
namespace FfxHooks::Config {const char* GetString(const char*,const char*){return portuguese?"pt-BR":"native";}}
namespace FfxHooks::TextLanguage::Native {
Snapshot Inspect() noexcept {Snapshot s;s.fontReady=font;return s;}
bool EncodeFieldLiteral(const char* text,unsigned char* out,std::size_t capacity,std::size_t& size) noexcept {
    if(!font||!encodingWorks||capacity<3)return false;
    for(unsigned i=0;i<7;++i)if(!std::strcmp(text,M::Portuguese[i])){out[0]=0x40;out[1]=static_cast<unsigned char>(0x50+i);out[2]=0;size=3;++encodes;return true;}
    return false;
}
}
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static unsigned Read(unsigned rva){unsigned value=0;std::memcpy(&value,reinterpret_cast<void*>(base+rva),4);return value;}
static const unsigned char* __cdecl NativeText(unsigned,unsigned){++fallthroughs;static const unsigned char original[]={0x55,0};return original;}
static void __cdecl ResetWindow(unsigned){}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("NOTICE_FAULT code=%08lX rva=%08lX\n",e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-base));return EXCEPTION_CONTINUE_SEARCH;}
static void ResetQueue(){*reinterpret_cast<unsigned char*>(base+0xD2C944)=0;*reinterpret_cast<unsigned char*>(base+0xD2C945)=0;}
static void Publish(unsigned i){constexpr O::Notice kinds[]={O::Notice::Exposure,O::Notice::RebuildWarning,O::Notice::Preparation,O::Notice::Preparation,O::Notice::Cancelled,O::Notice::FinalPending,O::Notice::FinalEntry};M::Publish(kinds[i],i==3||i==6);}
int main(int argc,char** argv){
    if(argc!=2)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,Fault);
    const auto pe=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!pe)return 2;base=reinterpret_cast<std::uintptr_t>(pe);
    Check(PrivatePeFixture::NormalizeRelocations(pe),"private current executable is relocated");
    const FfxHooks::EquipmentWorkshop::Evidence::Span evidence[]={O::Evidence::spans[7],O::Evidence::spans[8]};
    Check(FfxHooks::NativeUiSupport::Profile(base,evidence),"native getter and message queue signatures match");
    // The actual queue is exercised. Its display-reset call is a controlled
    // rendering boundary; no game window, timer or input subsystem is launched.
    unsigned char jump[5]={0xE9};const auto delta=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&ResetWindow)-(base+0x38FA30+5));std::memcpy(jump+1,&delta,4);
    DWORD old=0,unused=0;VirtualProtect(reinterpret_cast<void*>(base+0x38FA30),5,PAGE_EXECUTE_READWRITE,&old);std::memcpy(reinterpret_cast<void*>(base+0x38FA30),jump,5);VirtualProtect(reinterpret_cast<void*>(base+0x38FA30),5,old,&unused);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0x38FA30),5);
    Check(M::EncodeEnglish(),"all seven English messages use the real field-text encoder");
#ifdef FFXHOOKS_HAVE_POLYHOOK
    Check(M::Start(base),"the actual native battle-text getter hook installs through its owned batch");
    const auto getter=reinterpret_cast<M::Getter>(base+0x38F8F0);
#else
    M::image=base;M::ready=M::enabled=true;const auto getter=&M::Get;
#endif
    M::originals[0]=reinterpret_cast<void*>(&NativeText);
    for(unsigned i=0;i<7;++i){ResetQueue();Publish(i);Check(Read(0xD2C91C)==M::FirstId+i&&Read(0xD2C918)==20,"native queue receives the matching English notice for Omega");
        const auto* row=reinterpret_cast<unsigned char*>(base+0xD2C914);Check(row[0]==1&&row[1]==0&&row[2]==90,"notice is a timed native battle message with no confirmation wait");
        Check(getter(M::FirstId+i,0)==M::encoded[i].data(),"getter returns process-lifetime encoded English text");}
    portuguese=true;ResetQueue();Publish(0);Check(Read(0xD2C91C)==M::FirstId,"Portuguese selection falls back to English until the paired font is ready");
    font=true;for(unsigned i=0;i<7;++i){ResetQueue();Publish(i);Check(Read(0xD2C91C)==M::FirstId+7+i&&getter(M::FirstId+7+i,0)[1]==0x50+i,"Portuguese locale uses its exact approved text through the paired encoder");}
    Check(encodes==7,"each Portuguese message is encoded once without rewriting queued buffers");ResetQueue();Publish(0);Check(encodes==7,"repeated notices preserve immutable encoded storage");
    *reinterpret_cast<unsigned char*>(base+0xD2C944)=4;Publish(5);Check(M::pending==5,"a full native message queue retains the pending announcement");ResetQueue();M::Flush();Check(M::pending==7&&Read(0xD2C91C)==M::FirstId+12,"pending notice publishes after native queue capacity returns");
    const auto* text=getter(M::FirstId+7,0);M::Stop();Check(getter(M::FirstId+7,0)==text,"already queued text remains valid after logical stop");
    (void)getter(12,0);Check(fallthroughs==1,"unrelated native messages retain their original getter");
    std::printf("OmegaLastSealNoticeRt1: %u checks, %u failures; native queue with controlled render reset and paired-font provider\n",checks,failures);return failures?1:0;
}
