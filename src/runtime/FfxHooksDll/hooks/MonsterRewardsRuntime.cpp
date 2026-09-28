#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "MonsterRewardsRuntime.h"
#include "MonsterRewardSettings.h"
#include "NativeUiHookSupport.h"
#include "F8FlagCatalog.h"
#include "../shared/Config.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace FfxHooks::MonsterRewards {
namespace {
constexpr std::uint32_t seam=0x399144;
struct NamedMonster {unsigned species;const char* name;};
constexpr NamedMonster names[]={
#include "MonsterRewardNames.inc"
};
struct Entry {char name[64]{};BaseRewards base{};Source source=Source::Unavailable;bool listed=false;};
std::array<Entry,SpeciesCount> entries{};
std::array<unsigned,SpeciesCount> list{};
unsigned listSize=0;
std::uintptr_t module=0;
// MinHook publishes this process-lifetime trampoline before enabling the seam.
void* originals[1]{};
std::atomic<bool> published{false},installed{false},admission{false},terminal{false};
static_assert(std::atomic<bool>::is_always_lock_free,"Loader detach only closes lock-free admission gates");
std::atomic<DWORD> owner{0};
std::atomic<unsigned> rejected{0};
RuntimeLog logger=nullptr;
const EquipmentWorkshop::Evidence::Span evidence[]={
#include "MonsterRewardEvidence.inc"
};
bool Copy(void* out,const void* in,std::size_t bytes) noexcept {return NativeUiSupport::Copy(out,in,bytes);}
void List(unsigned id) noexcept {
    if(id>=SpeciesCount||entries[id].listed)return;
    auto* at=std::lower_bound(list.data(),list.data()+listSize,id);
    std::move_backward(at,list.data()+listSize,list.data()+listSize+1);*at=id;++listSize;entries[id].listed=true;
    if(!entries[id].name[0])std::snprintf(entries[id].name,sizeof(entries[id].name),"Monster m%03u",id);
}
bool CanReadUi() noexcept {const auto thread=owner.load();return published.load(std::memory_order_acquire)&&(!thread||thread==GetCurrentThreadId());}
#include "MonsterRewardStore.inl"
bool ReadRate(unsigned id,Kind kind,unsigned& value){
    value=1;if(!ratesValid||id>=rates.size())return false;
    value=kind==Kind::Ap?rates[id].ap:rates[id].gil;return true;
}
bool Master(){const auto* flag=FindF8Flag("cheats.monster_rewards");return flag&&ResolveF8Flag(*flag).value;}
bool General(Kind kind,unsigned& value){
    value=1;const auto* flag=FindF8Flag(kind==Kind::Ap?"cheats.ap_100x":"cheats.gil_100x");
    if(!flag)return false;if(!ResolveF8Flag(*flag).value)return true;
    const auto scalar=ResolveF8Scalar(*flag);if(scalar.state==F8ScalarState::Invalid)return false;
    value=static_cast<unsigned>(scalar.value);return true;
}
bool CanonicalActor(std::uintptr_t actor,unsigned& species) noexcept {
    std::uint32_t base=0;std::uint16_t slot=0,raw=0;
    if(!module||actor<0x10000||actor>UINT32_MAX-0xF90u||
       !Copy(&base,reinterpret_cast<void*>(module+0xD334CC),4)||base<0x10000||base>UINT32_MAX-31u*0xF90u||
       !Copy(&slot,reinterpret_cast<void*>(actor+0xC),2)||slot<18||slot>=26||actor!=std::uintptr_t(base)+slot*0xF90u||
       !Copy(&raw,reinterpret_cast<void*>(actor+0xE),2)||!Species(raw,species))return false;
    return true;
}
void Capture(unsigned species,BaseRewards base) noexcept {entries[species].base=base;entries[species].source=Source::LiveActor;List(species);}
// Original data are unsigned WORDs. Only the two already-widened stack locals
// are replaced, before native AP abilities and Gillionaire see them.
void __cdecl AdjustFrame(std::uintptr_t frame) noexcept {
    if(!admission.load(std::memory_order_acquire)||terminal.load()||owner.load()!=GetCurrentThreadId())return;
    try {
        std::array<std::uint32_t,4> args{};std::array<std::int32_t,2> amounts{};
        if(frame<0x10000||frame>UINT32_MAX-24u||!Copy(args.data(),reinterpret_cast<void*>(frame+8),sizeof(args))||
           !Copy(amounts.data(),reinterpret_cast<void*>(frame-8),sizeof(amounts)))return;
        unsigned id=0;if(!CanonicalActor(args[1],id)||args[2]<0x10000||args[2]>UINT32_MAX-6u)return;
        std::array<unsigned char,6> words{};if(!Copy(words.data(),reinterpret_cast<void*>(std::uintptr_t(args[2])),words.size()))return;
        const auto rewards=Decode(words.data());Capture(id,rewards);
        unsigned ap=1,gil=1;if(!ReadRate(id,Kind::Ap,ap)||!ReadRate(id,Kind::Gil,gil)){++rejected;return;}
        if(ap==1&&gil==1)return;
        if(!Adjust(rewards,args[3]!=0,ap,gil,amounts[1],amounts[0])){++rejected;return;}
        if(!Copy(reinterpret_cast<void*>(frame-8),amounts.data(),sizeof(amounts)))++rejected;
    }catch(...){++rejected;}
}
#if defined(_M_IX86) && defined(FFXHOOKS_HAVE_POLYHOOK)
__declspec(naked) void RewardShim(){
    __asm {
        pushfd
        pushad
        push ebp
        call AdjustFrame
        add esp,4
        popad
        popfd
        jmp dword ptr [originals]
    }
}
#endif
void ReadModCatalog(const std::filesystem::path& root){
    std::error_code error;
    for(const auto& directory:std::filesystem::directory_iterator(root,error)){
        if(error)break;const auto folder=directory.path().filename().string();
        if(folder.size()<3||folder[0]!='_'||folder[1]!='m')continue;
        unsigned id=0;bool valid=true;for(std::size_t i=2;i<folder.size();++i){if(folder[i]<'0'||folder[i]>'9'){valid=false;break;}id=id*10u+unsigned(folder[i]-'0');if(id>=SpeciesCount){valid=false;break;}}
        if(!valid)continue;
        const auto path=directory.path()/(folder.substr(1)+".bin");
        const auto size=std::filesystem::file_size(path,error);if(error){error.clear();continue;}
        if(size<0x34||size>8u*1024u*1024u)continue;
        std::ifstream file(path,std::ios::binary);std::array<unsigned char,0x24> header{};std::array<unsigned char,6> loot{};
        if(!file.read(reinterpret_cast<char*>(header.data()),header.size())||Dword(header.data()+0x20)!=size)continue;
        const auto offset=Dword(header.data()+0x14);if(offset<0x30||offset>size-loot.size())continue;
        file.seekg(offset);if(!file.read(reinterpret_cast<char*>(loot.data()),loot.size()))continue;
        entries[id].base=Decode(loot.data());entries[id].source=Source::ModFile;List(id);
    }
}
void PrepareCatalog(){
    for(const auto& named:names){std::snprintf(entries[named.species].name,sizeof(entries[named.species].name),"%s",named.name);List(named.species);}
    wchar_t path[32768]{};const auto length=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    if(!length||length>=std::size(path))return;
    const auto game=std::filesystem::path(path).parent_path();
    // Unpacked native files are the preview source; live actor data supersede
    // this cache. No VBF, monster asset, or reward WORD is rewritten.
    ReadModCatalog(game/L"data/ffx_ps2/ffx/master/jppc/battle/mon");
    ReadModCatalog(game/L"data/mods/ffx_ps2/ffx/master/jppc/battle/mon");
}
}
bool Prepare(std::uintptr_t image,bool validateOnly,RuntimeLog log){
    if(published.load()||terminal.load()||validateOnly)return false;
    logger=log;
    try{
        if(!NativeUiSupport::Profile(image,evidence))return false;
        module=image;PrepareCatalog();LoadRates();published.store(true,std::memory_order_release);
        if(!Master())return true;
#if defined(_M_IX86) && defined(FFXHOOKS_HAVE_POLYHOOK)
        const std::uint32_t rvas[]={seam};void* replacements[]={reinterpret_cast<void*>(&RewardShim)};
        if(!NativeUiSupport::Install(image,rvas,replacements,originals,MinHookBatch::Owner::MonsterRewards,reinterpret_cast<const void*>(&Prepare)))return false;
        installed.store(true,std::memory_order_release);
        if(logger)logger("[ffx-hooks] Per-monster rewards installed at RVA399144; native reward words preserved\n");
#endif
        return installed.load();
    }catch(...){return false;}
}
void TickMainThread() noexcept {
    if(!published.load()||terminal.load())return;
    DWORD empty=0;owner.compare_exchange_strong(empty,GetCurrentThreadId());if(owner.load()!=GetCurrentThreadId())return;
    try{
        admission=installed.load()&&ratesValid&&Master();
        std::uint32_t actors=0;if(!Copy(&actors,reinterpret_cast<void*>(module+0xD334CC),4)||actors<0x10000||actors>UINT32_MAX-31u*0xF90u)return;
        for(unsigned slot=18;slot<26;++slot){const auto address=std::uintptr_t(actors)+slot*0xF90u;unsigned id=0;unsigned char exists=0;std::uint32_t loot=0;
            if(!Copy(&exists,reinterpret_cast<void*>(address+0xDC8),1)||!exists||!CanonicalActor(address,id)||
               !Copy(&loot,reinterpret_cast<void*>(address+0xF88),4)||loot<0x10000||loot>UINT32_MAX-6u)continue;
            std::array<unsigned char,6> words{};if(Copy(words.data(),reinterpret_cast<void*>(std::uintptr_t(loot)),words.size()))Capture(id,Decode(words.data()));
        }
        if(rejected.exchange(0)&&logger)logger("[ffx-hooks] Per-monster reward adjustment rejected invalid configuration or unexpected native values\n");
    }catch(...){admission=false;}
}
void RequestStop() noexcept {terminal=true;admission=false;published=false;}
bool Installed() noexcept {return installed.load()&&!terminal.load();}
unsigned Count() noexcept {return CanReadUi()?listSize:0;}
bool SpeciesAt(unsigned row,unsigned& species) noexcept {if(!CanReadUi()||row>=listSize)return false;species=list[row];return true;}
bool ReadPreview(unsigned id,Preview& output) noexcept {
    output={};if(id>=SpeciesCount||!CanReadUi())return false;
    try{const auto& entry=entries[id];output.species=id;output.base=entry.base;output.source=entry.source;output.running=admission.load();
        std::snprintf(output.name,sizeof(output.name),"%s",entry.name[0]?entry.name:"Unlisted monster");
        output.apValid=ReadRate(id,Kind::Ap,output.apMultiplier);output.gilValid=ReadRate(id,Kind::Gil,output.gilMultiplier);
        output.globalApValid=General(Kind::Ap,output.globalAp);output.globalGilValid=General(Kind::Gil,output.globalGil);
        if(output.source!=Source::Unavailable){
            if(output.apValid&&output.globalApValid){output.ap=Calculate(output.base.ap,output.apMultiplier,output.globalAp,Kind::Ap);output.overkillAp=Calculate(output.base.overkillAp,output.apMultiplier,output.globalAp,Kind::Ap);}
            if(output.gilValid&&output.globalGilValid)output.gil=Calculate(output.base.gil,output.gilMultiplier,output.globalGil,Kind::Gil);
        }return true;
    }catch(...){return false;}
}
bool SaveMultiplier(unsigned id,Kind kind,unsigned value){
    if(id>=SpeciesCount||!value||value>MaximumMultiplier||!CanReadUi()||owner.load()!=GetCurrentThreadId())return false;
    try{auto candidate=rates;auto& requested=kind==Kind::Ap?candidate[id].ap:candidate[id].gil;
        requested=static_cast<std::uint16_t>(value);if(!StoreRates(candidate))return false;List(id);return true;
    }catch(...){return false;}
}
const char* Detail() noexcept {return !published.load()?"Unsupported or unavailable":!ratesValid?"Invalid reward settings; repair file and restart":installed.load()?admission.load()?"Running":"Disabled":"Restart required to enable per-monster rewards";}
}
