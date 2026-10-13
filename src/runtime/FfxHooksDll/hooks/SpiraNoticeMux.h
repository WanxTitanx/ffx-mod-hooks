#pragma once
#include "ArcanaFieldPresentation.h"
#include "ArcanaFieldDialogCore.h"
#include "TextLanguageHook.h"
#include <array>
#include <atomic>
#include <cstring>
namespace FfxHooks::SpiraNoticeMux {
using Source=Arcana::FieldPresentation::SupplementalSource;
inline std::array<std::atomic<const Source*>,2> sources{};
inline thread_local unsigned selected=2;
inline bool Register(unsigned slot,const Source* source) noexcept {
    if(slot>=2||!source||!source->capture||!source->acknowledge||!source->format)return false;
    const Source* empty=nullptr;return sources[slot].compare_exchange_strong(empty,source)||empty==source;
}
inline void Unregister(unsigned slot,const Source* source) noexcept {if(slot<2)sources[slot].compare_exchange_strong(source,nullptr);}
inline bool Capture(std::uint16_t room,bool stable,Arcana::Presentation::View& out,std::uint64_t& generation) noexcept {
    std::array<Arcana::Presentation::View,2> views{};std::array<std::uint64_t,2> ids{};bool captured[2]{};
    for(unsigned i=0;i<2;++i){const auto* s=sources[i].load();captured[i]=s&&s->capture(room,stable,views[i],ids[i]);
        if(views[i].kind==Arcana::Presentation::Kind::System&&views[i].card>=(i?3:10))captured[i]=false;}
    if(selected>=2||!captured[selected]||views[selected].kind!=Arcana::Presentation::Kind::System){selected=2;
        for(unsigned i=0;i<2;++i)if(captured[i]&&views[i].kind==Arcana::Presentation::Kind::System){selected=i;break;}}
    out={};if(selected<2){if(ids[selected]>UINT64_MAX/2)return false;out=views[selected];out.card+=static_cast<std::uint8_t>(selected*10);generation=(ids[selected]<<1)|selected;return true;}
    generation=0;return captured[0]||captured[1];
}
inline Arcana::Error Acknowledge(std::uint64_t generation,const Arcana::Presentation::View& view,Arcana::Presentation::Navigation action) noexcept {
    if(view.card>12)return Arcana::Error::Stale;
    const unsigned slot=view.card>=10?1:0;const auto* source=sources[slot].load();if(!source||view.kind!=Arcana::Presentation::Kind::System||(generation&1)!=slot)return Arcana::Error::Stale;
    auto local=view;local.card-=static_cast<std::uint8_t>(slot*10);return source->acknowledge(generation>>1,local,action);
}
inline bool Format(const Arcana::Presentation::View& view,char* out,std::size_t capacity) noexcept {
    if(view.card>12)return false;
    const unsigned slot=view.card>=10?1:0;const auto* source=sources[slot].load();if(!source||view.kind!=Arcana::Presentation::Kind::System)return false;
    auto local=view;local.card-=static_cast<std::uint8_t>(slot*10);return source->format(local,out,capacity);
}
inline bool Encode(const char* text,unsigned char* out,std::size_t capacity,std::size_t& size) noexcept {
    if(TextLanguage::Native::EncodeFieldLiteral(text,out,capacity,size))return true;
    Arcana::FieldDialog::EncodedText encoded;if(!out||!Arcana::FieldDialog::Encode(text,encoded)||encoded.size>capacity)return false;
    std::memcpy(out,encoded.bytes.data(),encoded.size);size=encoded.size;return true;
}
inline Source Combined() noexcept {return {Capture,Acknowledge,Format,Encode};}
}
