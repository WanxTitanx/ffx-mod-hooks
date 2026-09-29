// Jarvis-HOOK: built-in registry and pack-selection contracts, without a game.
#include "../hooks/ElementBuiltinPack.h"
#include "../hooks/ElementNameSettings.h"
#include <cstdio>
#include <cstring>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",why);}}
}
int main(){
    namespace E=FfxHooks::ElementalDominion;
    E::Pack pack;E::PackProblem problem{};
    Check(E::LoadPack(E::BuiltinPackText(),E::KnownCapabilities,pack,problem),
          "the built-in catalog passes the same strict pack parser");
    Check(pack.registry.Size()==10,"eight native and two external elements exist without a file");
    const char* names[]={"Fire","Ice","Thunder","Water","Holy","Darkness","Earth","Wind","Poison","Gravity"};
    const unsigned bits[]={1,2,4,8,0x10,0x80,0x20,0x40,0,0};
    for(unsigned i=0;i<10;++i){
        const auto* item=pack.registry.At(i);
        Check(item&&item->label==names[i],"default element labels follow the user-approved elemental roles");
        Check(item&&item->nativeBit==bits[i],"external elements never acquire fictional native bits");
        Check(item&&E::ValidKey(item->key),"every default has a stable namespaced identity");
    }
    Check(pack.registry.Index("hook.custom03")==8&&pack.registry.Index("hook.custom04")==9,
          "the two hook identities survive display renaming");
    Check(pack.banks.empty()&&pack.commands.empty()&&pack.profiles.empty()&&pack.equipment.empty(),
          "registering unused elements does not rewrite or assign native attacks/items");
    Check(E::UseBuiltinElements("",false),"a missing default file selects the built-in registry");
    Check(!E::UseBuiltinElements("",true),"an existing default pack still goes through admission");
    Check(!E::UseBuiltinElements("custom-pack.json",false),"a missing explicit selection never falls back silently");
    Check(!E::UseBuiltinElements("custom-pack.json",true),"an explicit custom selection retains priority");
    Check(E::UseBuiltinElements("builtin",true),"an explicit built-in choice is deterministic");
    E::Pack nativeOnly;
    for(unsigned i=0;i<8;++i)Check(nativeOnly.registry.Add(*pack.registry.At(i))==E::Error::Ok,"native test registry preserves each existing identity");
    Check(E::EnsureHookSlots(nativeOnly)&&nativeOnly.registry.Size()==10,"an authored native-only pack gains two neutral unused hook slots");
    Check(E::EnsureHookSlots(nativeOnly)&&nativeOnly.registry.Size()==10,"completion is idempotent and preserves indices");
    E::Pack oneExternal;
    for(unsigned i=0;i<8;++i)(void)oneExternal.registry.Add(*pack.registry.At(i));
    (void)oneExternal.registry.Add({"mod.wind","label.wind","Wind",0xFFFFFF,0});
    Check(E::EnsureHookSlots(oneExternal)&&oneExternal.registry.At(8)->key=="mod.wind"&&oneExternal.registry.At(9)->key=="hook.custom03",
          "existing external bindings retain their indices when a missing slot is added");
    namespace N=FfxHooks::ElementNames;char normalized[65]{};
    Check(N::Normalize("  Radiance  ",normalized)==N::Result::Saved&&!std::strcmp(normalized,"Radiance"),"names normalize surrounding spaces");
    Check(N::Normalize("   ",normalized)==N::Result::Empty,"blank names are not silent resets");
    Check(N::Normalize(std::string(33,'A'),normalized)==N::Result::TooLong,"oversized labels are rejected without truncation");
    Check(N::Normalize("Light;core=1",normalized)==N::Result::InvalidCharacter,"INI delimiters cannot inject settings");
    Check(N::Normalize("Light\nDark",normalized)==N::Result::InvalidCharacter,"control bytes cannot enter native labels");
    Check(N::Same("HOLY","Holy")&&!N::Same("Holy","Holy 2"),"duplicate matching ignores case but retains identity distinctions");
    std::printf("ELEMENT_BUILTIN_CORE %d/%d passed\n",checks-failures,checks);
    return failures?1:0;
}
