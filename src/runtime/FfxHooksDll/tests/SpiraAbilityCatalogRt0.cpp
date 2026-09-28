// Jarvis-HOOK: authored identity/payload checks; no native process or save access.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if __has_include("../hooks/SpiraAbilityCatalog.h")
#include "../hooks/SpiraAbilityCatalog.h"
namespace S=FfxHooks::SpiraAbilities;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
static void W16(unsigned char* p,unsigned value){p[0]=static_cast<unsigned char>(value);p[1]=static_cast<unsigned char>(value>>8);}
static void W32(unsigned char* p,unsigned value){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(value>>(8*i));}
static bool Pending(unsigned i){return i==6||(i>=22&&i<=26);}
static std::vector<unsigned char> Fixture(bool numeric=false){
    constexpr unsigned count=201;
    const char* names[]={"Aeon Break HP/MP Limit","Aeon Break Damage Limit","Mana Spring","Break Limits",
        "Devil's Bargain","Warden's Oath","Arcane Focus","Double Drop","Triple Drop","Element Eater",
        "HPMP +10%","HPMP +20%","HPMP +40%","HPMP +60%","AIO +3%","AIO +6%","AIO +9%","AIO +12%",
        "STR MAG +10%","STR MAG +20%","DEF MDEF +10%","DEF MDEF +20%","Foolstrike","Fooltouch",
        "Fourstrike","Fourtouch","Spell Spring"};
    std::vector<unsigned char> bytes(20+count*108+1);
    W32(bytes.data(),1);W16(bytes.data()+10,count-1);W16(bytes.data()+12,108);
    W16(bytes.data()+14,count*108);W32(bytes.data()+16,20);
    for(unsigned i=0;i<27;++i){
        W16(bytes.data()+20+(148+i)*108,static_cast<unsigned>(bytes.size())-(20+count*108));
        const auto label=numeric?std::to_string(148+i):std::string(names[i]);
        for(char ch:label){
            unsigned code=static_cast<unsigned char>(ch);
            if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z'))code+=15;
            else if(ch==' ')code=58;else if(ch=='%')code=63;else if(ch=='\'')code=65;
            else if(ch=='+')code=69;else if(ch=='-')code=71;else if(ch=='.')code=72;else if(ch=='/')code=73;
            bytes.push_back(static_cast<unsigned char>(code));
        }
        bytes.push_back(0);
    }
    auto row=[&](unsigned id){return bytes.data()+20+id*108;};
    W16(row(151)+0x64,0x600);W16(row(155)+0x64,0x1000);W16(row(156)+0x64,0x2000);
    row(157)[0x12]=1;
    const unsigned hpmp[]={10,20,40,60};
    for(unsigned i=0;i<4;++i){row(158+i)[0x55]=static_cast<unsigned char>(hpmp[i]);W16(row(158+i)+0x56,0x300);}
    for(unsigned i=0;i<4;++i){row(162+i)[0x55]=static_cast<unsigned char>(3*(i+1));W16(row(162+i)+0x56,0x3F00);}
    for(unsigned i=0;i<2;++i){row(166+i)[0x55]=static_cast<unsigned char>(10*(i+1));W16(row(166+i)+0x56,0xC00);
        row(168+i)[0x55]=static_cast<unsigned char>(10*(i+1));W16(row(168+i)+0x56,0x3000);}
    row(172)[0x11]=row(173)[0x11]=15;
    return bytes;
}
int main(){
    auto bytes=Fixture();auto mapping=S::DefaultMapping();
    auto result=S::Validate(bytes.data(),bytes.size(),mapping,nullptr,0,S::NameEncoding::Latin,0);
    for(unsigned i=0;i<27;++i){
        Check(result[i].rowVerified,"each authored payload and stable name is independently verified");
        Check(result[i].code==(Pending(i)?S::MappingCode::DefinitionPending:S::MappingCode::ConsumerUnavailable),
              "valid data cannot claim an unregistered gameplay consumer");
    }
    result=S::Validate(bytes.data(),bytes.size(),mapping,nullptr,0,S::NameEncoding::Latin,S::AllConsumers);
    for(unsigned i=0;i<27;++i){
        Check(result[i].code==(Pending(i)?S::MappingCode::DefinitionPending:S::MappingCode::Admitted),
              "explicit consumer capabilities cannot invent undefined effect contracts");
        for(unsigned at=16;at<108;++at){
            std::array<unsigned char,108> row{};std::memcpy(row.data(),bytes.data()+20+(148+i)*108,108);
            row[at]^=1;
            Check(!S::PayloadMatches(i,row.data(),row.size()),"every undeclared gameplay byte rejects the record");
        }
    }
    for(unsigned at:{0u,2u,8u,10u,12u,14u,16u,18u}){
        auto corrupt=bytes;corrupt[at]^=1;
        const auto invalid=S::Validate(corrupt.data(),corrupt.size(),mapping,nullptr,0,S::NameEncoding::Latin,S::AllConsumers);
        Check(invalid[0].code==S::MappingCode::NoTable&&!invalid[0].rowVerified,"malformed native headers do not authorize any slot");
    }
    auto moved=mapping;moved[0]=135;
    Check(S::Validate(bytes.data(),bytes.size(),moved,nullptr,0,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::ReservedId,
          "Aeon mappings cannot occupy the Vanguard range");
    moved=mapping;moved[0]=149;
    Check(S::Validate(bytes.data(),bytes.size(),moved,nullptr,0,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::ReservedId,
          "one effect cannot take another effect's reserved default");
    auto remap=bytes;std::memcpy(remap.data()+20+175*108,remap.data()+20+148*108,108);moved=mapping;moved[0]=175;
    Check(S::Validate(remap.data(),remap.size(),moved,nullptr,0,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::Admitted,
          "an explicit unreserved remap keeps the stable identity and payload");
    const unsigned vanguardIds[]={135,175};
    Check(S::Validate(remap.data(),remap.size(),moved,vanguardIds,2,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::Collision,
          "cross-module collisions close the Spira binding");
    moved[1]=175;
    const auto collision=S::Validate(remap.data(),remap.size(),moved,nullptr,0,S::NameEncoding::Latin,S::AllConsumers);
    Check(collision[0].code==S::MappingCode::Collision&&collision[1].code==S::MappingCode::Collision,
          "duplicate remaps invalidate both participating effects");
    auto wrong=bytes;W16(wrong.data()+20+148*108,0);
    Check(S::Validate(wrong.data(),wrong.size(),mapping,nullptr,0,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::IdentityMismatch,
          "a neutral row with a different name does not become an exclusive effect");
    auto asian=Fixture(true);
    Check(S::Validate(asian.data(),asian.size(),mapping,nullptr,0,S::NameEncoding::NumericPlaceholder,S::AllConsumers)[0].rowVerified,
          "authored CJK numeric placeholders preserve stable default identities");
    Check(S::Validate(asian.data(),asian.size(),mapping,nullptr,0,S::NameEncoding::Latin,S::AllConsumers)[0].code==S::MappingCode::IdentityMismatch,
          "numeric placeholders require their explicit text mode");
    for(unsigned owner=0;owner<31;++owner){
        Check(S::OwnerKindMatches(0,owner,1)==(owner>=8&&owner<=17),"HP/MP Break accepts only canonical Aeon owner numbers on armor");
        Check(S::OwnerKindMatches(1,owner,0)==(owner>=8&&owner<=17),"damage Break accepts only canonical Aeon owner numbers on weapons");
        Check(S::OwnerKindMatches(5,owner,1)==(owner==2),"Warden's Oath is restricted to Auron armor");
    }
    Check(!S::OwnerKindMatches(0,8,0)&&!S::OwnerKindMatches(1,8,1),"exclusive Break gear kinds cannot be swapped");
    Check(S::Entries[0].workshopOnly&&S::Entries[1].workshopOnly&&!S::Entries[2].workshopOnly,
          "paid Break authorization remains distinct from ordinary Spira effects");
    Check(!S::PayloadMatches(27,bytes.data(),108)&&!S::PayloadMatches(0,nullptr,108)&&!S::PayloadMatches(0,bytes.data(),107),
          "unknown entries and incomplete payloads are rejected");
    std::printf("SPIRA_ABILITY_CATALOG_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production SpiraAbilityCatalog.h is missing");return 1;}
#endif
