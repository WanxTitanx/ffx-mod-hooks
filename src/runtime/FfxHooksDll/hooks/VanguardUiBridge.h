#pragma once
#include "VanguardRuntime.h"
#include "F8FlagCatalog.h"
#include <cstdio>
#include <atomic>
namespace FfxHooks::Vanguard {
// Native validation owns this process-lifetime provider; the menu only stages edits.
struct UiProvider {bool (*read)(MappingState&) noexcept=nullptr;bool (*save)(unsigned,unsigned) noexcept=nullptr;void (*refresh)() noexcept=nullptr;bool (*readBindings)(BindingState&) noexcept=nullptr;bool (*saveBinding)(unsigned,unsigned,std::uint64_t) noexcept=nullptr;};
inline std::atomic<const UiProvider*> uiProvider{nullptr};
inline bool RegisterUi(const UiProvider* provider) noexcept {
    if(!provider||!provider->read||!provider->save)return false;
    const UiProvider* expected=nullptr;return uiProvider.compare_exchange_strong(expected,provider)||expected==provider;
}
inline void UnregisterUi(const UiProvider* provider) noexcept {uiProvider.compare_exchange_strong(provider,nullptr);}
inline bool ReadUiMapping(MappingState& out) noexcept {
    const auto* provider=uiProvider.load();if(provider)return provider->read(out);
    out={};out.ids=DefaultMapping();out.codes.fill(MappingCode::NoKernel);return false;
}
inline bool SaveUiMapping(unsigned effect,unsigned id) noexcept {const auto* provider=uiProvider.load();return provider&&provider->save(effect,id);}
inline bool ReadUiBindings(BindingState& out) noexcept {out={};const auto* p=uiProvider.load();return p&&p->readBindings&&p->readBindings(out);}
inline bool SaveUiBinding(unsigned effect,unsigned packed,std::uint64_t stamp) noexcept {const auto* p=uiProvider.load();return p&&p->saveBinding&&p->saveBinding(effect,packed,stamp);}
inline void RefreshUi() noexcept {
    const auto* provider=uiProvider.load();
    if(provider){if(provider->refresh)provider->refresh();return;}
    // Called only by the menu owner, never by DllMain. Stop remains non-blocking.
    for(unsigned i=0;i<FeatureCount;++i)if(HasNativeConsumer(i)){
        char key[96]{};std::snprintf(key,sizeof(key),"vanguard.%s",Features[i].key);
        PublishF8RuntimeStatus(key,F8RuntimeAvailability::ProducerUnavailable,true,false);
    }
}
inline const char* UiMappingDetail(MappingCode code) noexcept {
    switch(code){case MappingCode::Valid:return "Valid";case MappingCode::NoKernel:return "No loaded kernel";
    case MappingCode::InvalidId:return "Invalid ID";case MappingCode::Duplicate:return "Duplicate ID";
    case MappingCode::NativePayload:return "Native payload";case MappingCode::IdentityMismatch:return "Wrong identity";}return "Invalid";
}
}
