#pragma once
#include "OmegaLastSealCore.h"
#include "SpiraNoticeMux.h"
#include "NativeUiHookSupport.h"
#include "TextLanguageSettings.h"
#include <array>
namespace FfxHooks::OmegaLastSeal::BattleNotice {
inline constexpr unsigned FirstId=0x7FFE7100;
inline constexpr const char* English[]={
    "Both seals have broken. Four party actions remain.",
    "A Custodian is preparing to restore its partner.",
    "Supernova is charging. Three party actions remain.",
    "Supernova is charging. Two party actions remain.",
    "Supernova cancelled. Both seals have broken.",
    "The Last Seal holds. Omega is preparing its final form.",
    "The Last Seal is open. Two Echoes have joined the battle."
};
inline constexpr const char* Portuguese[]={
    "Os dois selos ruíram. Restam quatro ações do grupo.",
    "Um Custódio prepara a restauração de seu parceiro.",
    "Supernova está carregando. Restam três ações do grupo.",
    "Supernova está carregando. Restam duas ações do grupo.",
    "Supernova cancelada. Os dois selos ruíram.",
    "O Último Selo resiste. Omega prepara sua forma final.",
    "O Último Selo se abriu. Dois Ecos entraram na batalha."
};
inline std::array<std::array<unsigned char,512>,14> encoded{};
inline std::array<bool,14> issued{};
inline std::array<bool,7> portugueseReady{};
inline bool ready=false,enabled=false;
inline unsigned pending=7;
inline std::uintptr_t image=0;
inline void* originals[1]{};
using Getter=const unsigned char*(__cdecl*)(unsigned,unsigned);
inline const unsigned char* __cdecl Get(unsigned id,unsigned variant){
    const auto at=id-FirstId;
    // Native queues may outlive logical Stop. Their immutable text remains
    // valid until retirement; this lookup never retains a battle pointer.
    if(ready&&at<encoded.size()&&issued[at]&&variant==0)return encoded[at].data();
    return reinterpret_cast<Getter>(originals[0])(id,variant);
}
inline bool EncodeEnglish() noexcept {
    for(unsigned i=0;i<7;++i){std::size_t count=0;if(!SpiraNoticeMux::Encode(English[i],encoded[i].data(),encoded[i].size(),count))return false;}
    return true;
}
inline bool Start(std::uintptr_t base){
    if(ready){enabled=image==base;return enabled;}
    unsigned char first=0;if(!NativeUiSupport::Copy(&first,reinterpret_cast<const void*>(base+0x39CFD0),1)||first!=0x55)return false;
    if(!EncodeEnglish())return false;
    constexpr std::uint32_t rvas[]={0x38F8F0};void* replacements[]={reinterpret_cast<void*>(&Get)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::OmegaLastSeal,reinterpret_cast<void*>(&Start)))return false;
    image=base;ready=enabled=true;return true;
}
inline void Flush() noexcept {
    if(!enabled||pending>=7)return;
    using Queue=int(__cdecl*)(unsigned,unsigned,unsigned,unsigned,unsigned);
    unsigned text=pending;
    if(TextLanguage::Settings::Selection()==1&&TextLanguage::Native::Inspect().fontReady){
        if(!portugueseReady[pending]){std::size_t size=0;portugueseReady[pending]=TextLanguage::Native::EncodeFieldLiteral(Portuguese[pending],encoded[pending+7].data(),encoded[pending+7].size(),size);}
        if(portugueseReady[pending])text+=7;
    }
    issued[text]=true;
    if(reinterpret_cast<Queue>(image+0x39CFD0)(1,20,FirstId+text,0,90)==1)pending=7;
}
inline void Publish(Notice kind,bool finalPhase,bool cancelled=false) noexcept {
    if(!enabled||kind==Notice::None)return;
    if(cancelled)pending=4;
    else switch(kind){
        case Notice::Exposure:pending=0;break;
        case Notice::RebuildWarning:pending=1;break;
        case Notice::Preparation:pending=finalPhase?3:2;break;
        case Notice::Cancelled:pending=4;break;
        case Notice::FinalPending:pending=5;break;
        case Notice::FinalEntry:pending=6;break;
        default:return;
    }
    Flush();
}
inline void Clear() noexcept {pending=7;}
inline void Stop() noexcept {enabled=false;Clear();}
}
