#include "MinigameAssist.h"
#include "NativeUiHookSupport.h"
#include "MinigameAssistEvidence.generated.h"
#include "../shared/ffx_addresses.h"
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace FfxHooks::MinigameAssist {
namespace {
static_assert(sizeof(void*)==4,"Minigame ATEL adapter requires x86");
static_assert(std::atomic<bool>::is_always_lock_free&&std::atomic<Code>::is_always_lock_free,
              "Minigame detach must use lock-free admission stores");
using Fetch=std::uint32_t(__cdecl*)(void*,void*);
std::uintptr_t moduleBase=0;
void* original[1]{};
unsigned configured=0;
std::atomic<bool> enabled{false};
std::atomic<DWORD> ownerThread{0};
std::atomic<Code> status{Code::Off};
std::atomic<unsigned> loggedMismatch{0};
bool attempted=false;
BCRYPT_ALG_HANDLE algorithm=nullptr;
void(*logger)(const char*)=nullptr;
Feedback feedback=nullptr;
std::array<std::atomic<Code>,2> groupStatus{{Code::Off,Code::Off}};
void Publish(unsigned group,Code value) noexcept {
    const unsigned index=group==Butterflies?0:1;
    if(groupStatus[index].exchange(value)!=value&&feedback)feedback(group,value);
}

template<class T> bool Read(std::uintptr_t address,T& value) noexcept {
    return NativeUiSupport::Copy(&value,reinterpret_cast<const void*>(address),sizeof(value));
}
bool Digest(const unsigned char* bytes,unsigned size,std::array<unsigned char,32>& out) noexcept {
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(!algorithm||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return false;
    const bool ok=BCryptHashData(hash,const_cast<PUCHAR>(bytes),size,0)>=0&&BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0)>=0;
    BCryptDestroyHash(hash);return ok;
}
std::uint32_t Adjust(void* actor,void* frame,std::uint32_t decoded) noexcept {
    if(!enabled.load()||(decoded>>24)!=0x2E)return decoded;
    const DWORD thread=GetCurrentThreadId(),owner=ownerThread.load();
    if(owner&&owner!=thread)return decoded;
    std::uint16_t room=0;unsigned char battle=1;std::uint32_t transition=1;
    if(!Read(moduleBase+RVA_FFX_SCENE_STATE_OBJECT,room)||
       !Read(moduleBase+RVA_FFX_BATTLE_ACTIVE_FLAG,battle)||battle||
       !Read(moduleBase+RVA_FFX_SCENE_TRANSITION_PENDING,transition)||transition)return decoded;
    unsigned scriptIndex=0;
    for(;scriptIndex<kScriptCount;++scriptIndex)if(Scripts()[scriptIndex].room==room&&(Scripts()[scriptIndex].option&configured))break;
    if(scriptIndex==kScriptCount)return decoded;
    const auto& script=Scripts()[scriptIndex];
    std::uint32_t blob=0,pc=0,codeOffset=0,codeBytes=0;
    // The real interpreter resolves pools at actor[1] + worker header offsets;
    // the execution frame's +24 is its current instruction pointer.
    if(!Read(reinterpret_cast<std::uintptr_t>(actor)+4,blob)||blob<0x10000||
       !Read(reinterpret_cast<std::uintptr_t>(frame)+24,pc)||
       !Read(blob,codeBytes)||!Read(blob+0x30,codeOffset)||
       codeOffset!=script.codeOffset||codeBytes!=script.codeBytes||codeBytes>kMaximumCodeBytes||
       blob>UINT32_MAX-codeOffset-codeBytes)return decoded;
    const auto code=std::uintptr_t(blob)+codeOffset;
    if(pc<code||pc>=code+codeBytes)return decoded;
    const auto* rule=FindRule(scriptIndex,static_cast<std::uint32_t>(pc-code),decoded,configured);
    if(!rule)return decoded;
    try {
        // Hash each actual override rather than caching an allocation address:
        // same-room retries may reuse that address for a replaced script.
        std::vector<unsigned char> bytes(codeBytes);
        std::array<unsigned char,32> digest{};
        if(!NativeUiSupport::Copy(bytes.data(),reinterpret_cast<void*>(code),bytes.size())||
           !Digest(bytes.data(),codeBytes,digest)||digest!=script.codeHash){
            status=Code::ScriptMismatch;
            Publish(script.option,Code::ScriptMismatch);
            const unsigned bit=1u<<scriptIndex;
            if(logger&&!(loggedMismatch.fetch_or(bit)&bit)){
                char line[160]{};std::snprintf(line,sizeof(line),"[ffx-hooks] Minigame assist: %s code differs from the verified script; native instruction retained\n",script.field);logger(line);
            }
            return decoded;
        }
        DWORD expected=0;
        if(!ownerThread.compare_exchange_strong(expected,thread)&&expected!=thread)return decoded;
        if(!enabled.load())return decoded;
        status=Code::Ready;
        Publish(script.option,Code::Ready);
        // Only the returned PUSHII literal changes. Bytecode, stack layout,
        // branching, reward calls and saved progress remain native-owned.
        return 0x2E000000u|rule->replacement;
    }catch(...){return decoded;}
}
std::uint32_t __cdecl FetchShim(void* actor,void* frame){
    const auto fetch=reinterpret_cast<Fetch>(original[0]);
    const auto decoded=fetch?fetch(actor,frame):0;
    return Adjust(actor,frame,decoded);
}
}
bool Profile(std::uintptr_t base) noexcept {return base&&NativeUiSupport::Profile(base,Evidence::spans);}
bool Start(std::uintptr_t base,unsigned options,bool validateOnly,void(*log)(const char*),Feedback publish){
    if(!(options&3)||options&~3u||validateOnly)return false;
    if(attempted)return enabled.load();
    if(!Profile(base)){status=Code::Unsupported;return false;}
    attempted=true;moduleBase=base;configured=options;logger=log;feedback=publish;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0){status=Code::Unsupported;return false;}
    const std::uint32_t targets[]={Evidence::fetchRva};void* replacements[]={reinterpret_cast<void*>(FetchShim)};
    if(!NativeUiSupport::Install(base,targets,replacements,original,MinHookBatch::Owner::MinigameAssist,reinterpret_cast<const void*>(&Start))){status=Code::Unsupported;return false;}
    // The provider and trampoline are pinned for process lifetime. Logical
    // teardown is non-blocking and cannot free an in-flight decoder dependency.
    enabled=true;status=Code::Ready;
    if(logger)logger("[ffx-hooks] Minigame assist: verified ATEL literal overrides enabled; native rewards preserved\n");
    return true;
}
void Stop() noexcept {enabled=false;status=Code::Stopped;}
bool Active() noexcept {return enabled.load();}
Code Status() noexcept {return status.load();}
}
