// Isolated composition through the real shared damage/affinity/Nul entries.
#include "../hooks/ArcanaElemental.h"
namespace AE=FfxHooks::Arcana::Elemental;
static std::array<AE::Snapshot,7> cardSnapshots{};
static DWORD cardThread=0;
static bool CardSnapshot(unsigned actor,AE::Snapshot& out) noexcept {
    if(GetCurrentThreadId()!=cardThread||actor>=cardSnapshots.size())return false;
    out=cardSnapshots[actor];return out.battle!=0;
}
static unsigned cardInterference=0;
static int cardNestedResult=0;
static unsigned char* cardOriginalRow=nullptr;
static unsigned __cdecl CardEndpoint(unsigned user,void* source,unsigned target,void* actor,const void* command,
    unsigned commandId,void* info,unsigned a8,unsigned a9,unsigned a10,unsigned a11){
    const unsigned mode=cardInterference;cardInterference=0;
    if(mode==1)AE::Unregister(CardSnapshot);
    if(mode==2&&cardOriginalRow)cardOriginalRow[0x28]^=1;
    if(mode==3)++cardSnapshots[0].revision;
    if(mode==4){
        std::array<unsigned char,44> nestedInfo{};
        const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(coreImage+0x38E680);
        cardNestedResult=static_cast<int>(producer(user,source,target,actor,cardOriginalRow,commandId,nestedInfo.data(),a8,a9,a10,a11));
    }
    return CoreEndpoint(user,source,target,actor,command,commandId,info,a8,a9,a10,a11);
}
static void CardRows(std::vector<unsigned char>& bank){
    for(unsigned id:{0u,80u,81u,82u}){
        auto* row=bank.data()+20+96*id;row[0x1E]=4;row[0x20]=0;row[0x23]=1;row[0x28]=1;row[0x2A]=16;
    }
}
static void ReplaceText(std::string& text,const std::string& from,const std::string& to){
    std::size_t at=0;while((at=text.find(from,at))!=std::string::npos){text.replace(at,from.size(),to);at+=to.size();}
}
static std::string CardPack(const std::vector<unsigned char>& bank,bool nativeExact){
    auto text=CorePack(bank);
    ReplaceText(text,"tests.e8","spira.poison");ReplaceText(text,"tests.e9","spira.gravity");
    ReplaceText(text,"\"key\":\"spell.test80\",","\"key\":\"spell.test80\",\"augment\":true,");
    ReplaceText(text,"\"key\":\"spira.poison\",\"base_bp\":15000","\"key\":\"spira.poison\",\"base_bp\":15000,\"locked\":true");
    ReplaceText(text,"\"key\":\"spira.gravity\",\"base_bp\":15000","\"key\":\"spira.gravity\",\"base_bp\":-10000");
    if(nativeExact){
        // NativeExact is deliberately incompatible with authored profiles.
        const auto begin=text.find("\"key\":\"spell.test82\"");
        const auto policy=text.find("highest_exposure",begin);text.replace(policy,std::strlen("highest_exposure"),"native_exact");
        text=text.substr(0,text.find("\"profiles\":"))+"\"profiles\":[],\"equipment\":[]}";
    }
    return text;
}
static void ArcanaCases(std::uintptr_t base,std::vector<unsigned char>& actors,std::vector<unsigned char>& bank,
                        bool authored,bool unrelated,bool nativeExact){
    coreImage=base;amount=1000;cardThread=GetCurrentThreadId();cardSnapshots={};
    actors[0x5C1]=1;actors[0x5D9]=0;
    cardInterference=0;cardOriginalRow=bank.data()+20;
    W::DamageProducerForTests(reinterpret_cast<void*>(&CardEndpoint));
    std::array<unsigned char,44> info{};
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+0x38E680);
    const auto hit=[&](unsigned target,unsigned id=0,const void* row=nullptr){return static_cast<int>(producer(0,actors.data(),target,
        actors.data()+target*0xF90,row?row:bank.data()+20+96*id,0x3000+id,info.data(),0,0,0,0));};
    const auto savedBank=bank;
    coreDifficulty={};
    std::snprintf(coreDifficulty.extra[0].key.data(),coreDifficulty.extra[0].key.size(),"%s",authored?"spira.poison":"hook.custom03");
    std::snprintf(coreDifficulty.extra[1].key.data(),coreDifficulty.extra[1].key.size(),"%s",authored?"spira.gravity":"hook.custom04");
    coreDifficulty.extra[0].affinity=FfxHooks::F7Elements::Affinity::Weak;
    coreDifficulty.extra[1].affinity=FfxHooks::F7Elements::Affinity::Absorb;
    const FfxHooks::F7Elements::Provider difficulty{CoreDifficultySelection};
    Check(FfxHooks::F7Elements::Register(&difficulty),"card fixture registers the existing F7 affinity provider");
    Check(hit(18)==1000,"no Arcana provider leaves an unbound native attack unchanged");
    Check(AE::Register(CardSnapshot),"card snapshots register without a second native hook");
    cardSnapshots[0]={AE::Poison,0,12,4};
    if(unrelated){
        Check(hit(18)==1000,"unrelated external descriptor indices never inherit Poison or Gravity semantics");
    }else{
        Check(hit(18)==1500,"Biostrike uses the applied ninth-element weakness from F7");
        cardInterference=4;
        Check(hit(18)==1500&&cardNestedResult==1500,"nested native hits retain separate external-element frames");
        cardInterference=1;Check(hit(18)==1000,"provider retirement between entry and affinity consumption fails closed");
        Check(AE::Register(CardSnapshot),"fixture provider can be republished after its completed retirement");
        cardInterference=2;Check(hit(18)==1000,"a native command changed after entry cannot consume the captured external affinity");bank=savedBank;
        cardInterference=3;Check(hit(18)==1000,"a card revision changed after entry cannot consume the captured external affinity");cardSnapshots[0].revision=4;
        cardSnapshots[0].strikes=AE::Gravity;
        Check(hit(18)==-1000,"Gravitystrike uses the applied tenth-element absorption from F7");
        amount=777;Check(hit(18)==-777,"Gravitystrike retains normal weapon damage instead of a fractional-HP formula");amount=1000;
        cardSnapshots[0].strikes=AE::All;
        Check(hit(18)==1500,"mixed extra strikes resolve highest exposure once");
        cardSnapshots[0].strikes=AE::Poison;actors[0x5D9]=1;info[0xE]=2;
        actors[18*0xF90+0x5DA]=1;
        Check(hit(18)==1500&&info[0xE]==2,"native absorption and Nul cannot cover Poison or consume a partial native charge");
        actors[0x5D9]=0;actors[18*0xF90+0x5DA]=0;info={};
        cardSnapshots[1]={0,AE::All,12,4};
        const bool locked=authored&&!nativeExact;
        Check(hit(1)==(locked?1500:500),locked?"locked authored exposure ignores card Ward":"Poison Ward halves positive exposure for its wearer");
        E::ElementalView view{};
        Check(E::ReadElement(1,8,view)&&view.effectiveBp==(locked?15000:5000),"Scan reports the same card Ward result as damage");
        cardSnapshots[0].strikes=AE::Gravity;
        Check(hit(1)==(locked?-1000:500),"Gravity Ward preserves absorption and halves positive exposure");
        cardSnapshots[0].strikes=AE::Poison;
        if(authored){
            Check(hit(18,80)==1500,"authored augment bindings include equipped external strikes");
            Check(hit(18,81)==1000,"authored non-augment bindings remain authoritative");
            Check(hit(18,82)==1000,nativeExact?"explicit NativeExact bindings remain native":"non-augment native-element bindings remain authoritative");
            bank[20+96*80+0x28]^=1;Check(hit(18,80)==1000,"mutated authored rows cannot fall through to unbound card admission");bank=savedBank;
            if(nativeExact){
                cardSnapshots[0].strikes=0;
                Check(hit(1,88)==500,"a card Ward also applies to an authored spell without a source card strike");
                cardInterference=1;Check(hit(1,88)==1000,"recipient Ward retirement is revalidated independently of source strikes");
                Check(AE::Register(CardSnapshot),"recipient fixture resumes only after fresh registration");
                cardSnapshots[0].strikes=AE::Poison;
            }
        }
        const auto savedActor=actors;
        actors[0x5C1]=5;Check(hit(18)==1000,"Demi-style weapon formulas cannot acquire Gravitystrike");actors=savedActor;
        auto* row=bank.data()+20;row[0x1E]=0;Check(hit(18)==1000,"non-weapon commands never inherit card strikes");bank=savedBank;
        bank[20+0x20]|=0x10;Check(hit(18)==1000,"healing commands remain native");bank=savedBank;
        bank[20+0x23]=3;Check(hit(18)==1000,"mixed HP/MP commands remain native");bank=savedBank;
        W16(bank.data(),17);Check(hit(18)==1000,"oversized native section counts fail closed");bank=savedBank;
        W16(bank.data()+12,95);Check(hit(18)==1000,"unexpected native row widths fail closed");bank=savedBank;
        std::array<unsigned char,96> foreign{};std::memcpy(foreign.data(),bank.data()+20,96);
        Check(hit(18,0,foreign.data())==1000,"a byte-identical row outside the native bank cannot authorize an external strike");
        cardSnapshots[0].battle=0;Check(hit(18)==1000,"retired card epochs fall back to native damage");cardSnapshots[0].battle=12;
        cardThread=0;Check(hit(18)==1000,"foreign owner-thread snapshots cannot affect damage");cardThread=GetCurrentThreadId();
    }
    AE::Unregister(CardSnapshot);Check(hit(18)==1000,"retiring Arcana removes every extra strike without editing native rows");
    Check(bank==savedBank&&B::currentDamage==nullptr,"native command data and transient damage scope remain intact");
    FfxHooks::F7Elements::Unregister(&difficulty);E::RequestStop();
    Check(hit(18)==1000,"stopping Elemental Dominion restores the native affinity path");
}
