#pragma once
#include "ElementIdentity.h"
#include "ElementPackCore.h"

namespace FfxHooks::ElementalDominion {
inline bool UseBuiltinElements(std::string_view selected,bool defaultFileExists) noexcept {
    return selected=="builtin"||(selected.empty()&&!defaultFileExists);
}
inline bool EnsureHookSlots(Pack& pack){
    unsigned external=0;
    for(unsigned i=0;i<pack.registry.Size();++i)if(!pack.registry.At(i)->nativeBit)++external;
    for(unsigned i=ElementIdentity::NativeCount;i<ElementIdentity::Count&&external<2;++i){
        const auto& item=ElementIdentity::Defaults[i];
        if(const auto* existing=pack.registry.Find(item.key)){
            if(existing->nativeBit)return false;
            continue;
        }
        if(pack.registry.Add({item.key,std::string("label.")+item.key,item.label,item.rgb,0})!=Error::Ok)return false;
        ++external;
    }
    return external>=2;
}
inline std::string BuiltinPackText(){
    std::string text=R"({"schema":"ffx.mod007.elements.v1","package_id":"hook.builtin-elements","version":1,"exe_sha256":")";
    text+=SupportedExecutable;
    text+=R"(","requires":["mod007.registry.v1","mod007.affinity.v1","mod007.context.v1"],"fallback":"native-unmodified","elements":[)";
    for(unsigned i=0;i<ElementIdentity::Count;++i){
        if(i)text+=',';
        const auto& item=ElementIdentity::Defaults[i];
        text+="{\"key\":\"";text+=item.key;text+="\",\"label_key\":\"label.";text+=item.key;
        text+="\",\"label\":\"";text+=item.label;text+="\",\"rgb\":";text+=std::to_string(item.rgb);
        text+=",\"native_bit\":";text+=std::to_string(item.nativeBit);text+='}';
    }
    // Registering elements does not invent assignments to native commands/items.
    text+=R"(],"banks":[],"commands":[],"profiles":[],"equipment":[]})";
    return text;
}
}
