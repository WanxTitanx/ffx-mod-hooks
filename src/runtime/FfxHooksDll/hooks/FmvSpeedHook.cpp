// Jarvis-HOOK. The exact movie-frame consumer and its own FMOD channel share a
// bounded rate. Scene/battle clocks and every other sound channel stay native.
#include "FmvSpeedHook.h"
#include "F8RuntimeCore.h"
#include "../shared/ffx_addresses.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstring>
#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif
namespace FfxHooks::FmvSpeed {
namespace {
constexpr std::uintptr_t kFrameRva=0x006284A0u,kMovieRootRva=0x008DED2Cu;
using OriginalFn=bool(__thiscall*)(void*,void*,void*,void*);
using GetFrequency=int(__stdcall*)(void*,float*);
using SetFrequency=int(__stdcall*)(void*,float);
using GetSound=int(__stdcall*)(void*,void**);
std::uintptr_t g_base=0;
alignas(8) std::uint64_t g_original=0;
std::atomic<bool> g_ready{false},g_accepting{false};
std::atomic<unsigned> g_wanted{1},g_applied{1};
std::atomic<unsigned> g_epoch{0};
std::atomic<Code> g_status{Code::Off};
GetFrequency g_getFrequency=nullptr;SetFrequency g_setFrequency=nullptr;GetSound g_getSound=nullptr;
SRWLOCK g_audioLock=SRWLOCK_INIT;
struct AudioOwner {void* channel=nullptr;void* sound=nullptr;float baseline=0,last=0;bool owned=false;};
AudioOwner g_audio{};
void* g_conflictChannel=nullptr;void* g_conflictSound=nullptr;
#ifdef FFXHOOKS_HAVE_POLYHOOK
PLH::x86Detour* g_hook=nullptr;
#endif
struct Movie {bool valid=false,neutral=false;void* core=nullptr;void* channel=nullptr;unsigned queued=0;};
Movie ReadMovie(){
    Movie m{};
    if(!g_base)return m;
    __try {
        const auto parent=*reinterpret_cast<const std::uintptr_t*>(g_base+kMovieRootRva);
        if(parent<0x10000u || parent>0x7FFFF000u || !*reinterpret_cast<const std::uint8_t*>(parent+0x6D0u))return m;
        const auto core=parent+0x38u;
        if(*reinterpret_cast<const std::uint32_t*>(core+0xE4u)!=1)return m;
        m.core=reinterpret_cast<void*>(core);m.channel=*reinterpret_cast<void*const*>(core+0x2A0u);
        m.neutral=*reinterpret_cast<const std::uint32_t*>(g_base+RVA_FFX_NATIVE_SPEED_BOOSTER)==0;
        if(*reinterpret_cast<const std::uint8_t*>(core+0x58u))m.queued=*reinterpret_cast<const std::uint32_t*>(core+0x78u)-*reinterpret_cast<const std::uint32_t*>(core+0x74u);
        m.valid=m.queued<=1024;return m;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
bool Near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<0.5f;}
bool ReadAudio(void* channel,void** sound,float* frequency){
    if(!channel || !g_getFrequency || !g_getSound)return false;
    __try {return g_getSound(channel,sound)==0 && *sound && g_getFrequency(channel,frequency)==0 && std::isfinite(*frequency) && *frequency>0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool SetAudio(void* channel,float value){
    if(!channel || !g_setFrequency || !std::isfinite(value) || value<=0 || value>1536000.0f)return false;
    __try{return g_setFrequency(channel,value)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreAudio(){
    if(!g_audio.owned)return true;
    void* sound=nullptr;float current=0;
    if(!ReadAudio(g_audio.channel,&sound,&current) || sound!=g_audio.sound){g_audio={};return true;}
    if(!Near(current,g_audio.last)){g_conflictChannel=g_audio.channel;g_conflictSound=g_audio.sound;g_audio={};g_status=Code::Conflict;return false;}
    if(!SetAudio(g_audio.channel,g_audio.baseline)){g_status=Code::RestorePending;return false;}
    void* checked=nullptr;float frequency=0;
    if(!ReadAudio(g_audio.channel,&checked,&frequency) || checked!=sound || !Near(frequency,g_audio.baseline)){g_status=Code::RestorePending;return false;}
    g_audio={};if(g_status==Code::RestorePending)g_status=Code::Ready;return true;
}
bool RateAudio(void* channel,unsigned factor){
    if(factor==1)return RestoreAudio();
    if(!channel || factor<2 || factor>8)return false;
    void* sound=nullptr;float current=0;
    if(!ReadAudio(channel,&sound,&current))return false;
    if(channel==g_conflictChannel && sound==g_conflictSound){g_status=Code::Conflict;return false;}
    if(g_status==Code::RestorePending)return false;
    if(g_conflictChannel){g_conflictChannel=nullptr;g_conflictSound=nullptr;g_status=Code::Ready;}
    if(g_audio.owned && (g_audio.channel!=channel || g_audio.sound!=sound))if(!RestoreAudio())return false;
    if(g_audio.owned && !Near(current,g_audio.last)){g_conflictChannel=g_audio.channel;g_conflictSound=g_audio.sound;g_status=Code::Conflict;g_audio={};return false;}
    const float baseline=g_audio.owned?g_audio.baseline:current;
    const float target=baseline*static_cast<float>(factor);
    if(!SetAudio(channel,target))return false;
    g_audio={channel,sound,baseline,target,true};
    void* checked=nullptr;float frequency=0;
    if(!ReadAudio(channel,&checked,&frequency) || checked!=sound || !Near(frequency,target)){g_status=Code::RestorePending;return false;}
    if(g_status==Code::AudioUnavailable)g_status=Code::Ready;
    return true;
}
bool Profile(std::uintptr_t base){
    __try {
        if(!base)return false;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || !F8Runtime::IsSupportedExecutable({pe->FileHeader.Machine,pe->OptionalHeader.Magic,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage}))return false;
        std::uint8_t code[0x51]{};std::memcpy(code,reinterpret_cast<void*>(base+kFrameRva),sizeof(code));
        constexpr unsigned offsets[]={6,0x15};constexpr std::uintptr_t targets[]={0x6DEA98,0x8613D8};
        for(unsigned i=0;i<2;++i){std::uint32_t value=0;std::memcpy(&value,code+offsets[i],4);if(value!=base+targets[i])return false;std::memset(code+offsets[i],0,4);}
        std::uint64_t hash=14695981039346656037ULL;for(const auto byte:code)hash=(hash^byte)*1099511628211ULL;
        const std::uint8_t worker[]={0xE8,0x36,0xD5,0x34,0},direct[]={0xE8,0xAB,0x0B,0x35,0};
        return hash==0xC5248B6A135E2A6AULL && std::memcmp(reinterpret_cast<void*>(base+0x2DAF65),worker,5)==0 && std::memcmp(reinterpret_cast<void*>(base+0x2D78F0),direct,5)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct FrameCall {void* self;void* one;void* two;void* three;Movie movie;};
bool Next(void* raw){auto& f=*static_cast<FrameCall*>(raw);return reinterpret_cast<OriginalFn>(g_original)(f.self,f.one,f.two,f.three);}
bool Rate(void* raw,unsigned factor){auto& f=*static_cast<FrameCall*>(raw);const bool ok=RateAudio(f.movie.channel,factor);if(!ok && g_status!=Code::Conflict && g_status!=Code::RestorePending)g_status=Code::AudioUnavailable;return ok;}
void Restore(void*){RestoreAudio();}
bool Admitted(void* raw){auto& f=*static_cast<FrameCall*>(raw);const auto now=ReadMovie();return g_accepting && g_wanted>1 && now.valid && now.neutral && now.core==f.self && now.channel==f.movie.channel;}
bool Dispatch(void* self,void* a,void* b,void* c,std::uint32_t caller){
    const auto next=reinterpret_cast<OriginalFn>(g_original);if(!next)return false;
    const auto movie=ReadMovie();
    if(!g_accepting || !movie.valid || movie.core!=self || (caller!=0x002DAF6Au&&caller!=0x002D78F5u))return next(self,a,b,c);
    if(!TryAcquireSRWLockExclusive(&g_audioLock))return next(self,a,b,c);
    FrameCall call{self,a,b,c,movie};
    const unsigned wanted=g_wanted.load();
    const auto result=Fetch({&call,Next,Rate,Restore,Admitted},wanted,movie.queued,movie.neutral && g_accepting);
    g_applied=result.rate;
    if(g_status!=Code::Conflict&&g_status!=Code::RestorePending&&g_status!=Code::AudioUnavailable)
        g_status=result.produced>1?(result.produced<wanted?Code::Limited:Code::Applied):Code::Ready;
    ReleaseSRWLockExclusive(&g_audioLock);
    // A focus-loss thread that missed this lock published its neutral request
    // before our release. Complete restoration even if Present stops afterward.
    if(!g_accepting || g_wanted<=1)Neutralize();
    return result.frame;
}
#ifdef FFXHOOKS_HAVE_POLYHOOK
bool __fastcall FrameShim(void* self,void*,void* a,void* b,void* c){const auto address=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return Dispatch(self,a,b,c,address>=g_base?static_cast<std::uint32_t>(address-g_base):0);}
#endif
}
bool Start(std::uintptr_t base){
    if(g_ready)return true;
    if(!Profile(base)){g_status=Code::Unsupported;return false;}
    HMODULE audio=GetModuleHandleW(L"fmodex.dll");
    g_getFrequency=reinterpret_cast<GetFrequency>(audio?GetProcAddress(audio,"FMOD_Channel_GetFrequency"):nullptr);
    g_setFrequency=reinterpret_cast<SetFrequency>(audio?GetProcAddress(audio,"FMOD_Channel_SetFrequency"):nullptr);
    g_getSound=reinterpret_cast<GetSound>(audio?GetProcAddress(audio,"FMOD_Channel_GetCurrentSound"):nullptr);
    if(!g_getFrequency||!g_setFrequency||!g_getSound){g_status=Code::AudioUnavailable;return false;}
#ifdef FFXHOOKS_HAVE_POLYHOOK
    g_base=base;HMODULE own=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&FrameShim),&own))return false;
    try{g_hook=new PLH::x86Detour(base+kFrameRva,reinterpret_cast<std::uintptr_t>(&FrameShim),&g_original);if(!g_hook->hook()){g_status=Code::Conflict;return false;}}
    catch(...){g_status=Code::Conflict;return false;}
    g_ready=true;g_accepting=true;g_status=Code::Ready;return true;
#else
    (void)base;g_status=Code::Unsupported;return false;
#endif
}
bool Playing(){return g_ready&&g_accepting&&ReadMovie().valid;}
bool Ready(){return g_ready&&g_accepting;}
void Neutralize(){++g_epoch;g_wanted=1;g_applied=1;if(TryAcquireSRWLockExclusive(&g_audioLock)){RestoreAudio();if(!ReadMovie().valid){g_conflictChannel=nullptr;g_conflictSound=nullptr;if(g_status==Code::Conflict)g_status=Code::Ready;}ReleaseSRWLockExclusive(&g_audioLock);}}
unsigned PublicationEpoch(){return g_epoch.load();}
void SetDesired(unsigned factor,bool admitted,unsigned expectedEpoch){
    if(expectedEpoch==0xFFFFFFFFu)expectedEpoch=g_epoch.load();
    if(expectedEpoch!=g_epoch.load())return;
    if(!admitted || !g_accepting || (factor!=2&&factor!=4&&factor!=8) || !ReadMovie().valid){Neutralize();return;}
    g_wanted=factor;
    if(expectedEpoch!=g_epoch.load() || !g_accepting)Neutralize();
}
void RequestStop(){g_accepting=false;++g_epoch;g_wanted=1;}
Status CurrentStatus(){return {g_status.load(),g_wanted.load(),g_applied.load(),Playing()};}
const char* Detail(){switch(g_status.load()){
case Code::Ready:return "FMV speed ready";case Code::Applied:return "FMV picture and audio accelerated";case Code::Limited:return "FMV speed limited by playback capacity";
case Code::AudioUnavailable:return "FMV audio rate unavailable; native playback retained";case Code::Conflict:return "FMV rate conflicts with another owner";
case Code::RestorePending:return "FMV audio restoration pending";case Code::Unsupported:return "FMV runtime unsupported";default:return "Restart to enable FMV acceleration";
}}
#ifdef FFXHOOKS_TESTING
bool PrepareForTests(std::uintptr_t base,FrameFn original,AudioIo audio){
    if(!Profile(base))return false;g_base=base;g_original=reinterpret_cast<std::uintptr_t>(original);
    g_getFrequency=audio.getFrequency;g_setFrequency=audio.setFrequency;g_getSound=audio.getSound;
    g_ready=true;g_accepting=true;g_status=Code::Ready;return true;
}
bool FetchForTests(void* self,void* a,void* b,void* c,std::uint32_t caller){return Dispatch(self,a,b,c,caller);}
#endif
}
