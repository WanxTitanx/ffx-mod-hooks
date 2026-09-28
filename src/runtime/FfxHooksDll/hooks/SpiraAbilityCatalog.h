#pragma once
#include "AutoAbilitySlots.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace FfxHooks::SpiraAbilities {
inline constexpr unsigned Count=27,FirstId=AutoAbilitySlots::AscensionFirst,LastId=AutoAbilitySlots::AscensionLast,RowBytes=108;
inline constexpr std::uint32_t AllConsumers=(std::uint32_t{1}<<Count)-1;
enum class Definition : unsigned char { Defined,Pending,NativeBaseOnly };
struct Entry {const char* key;const char* label;unsigned char kind;std::uint32_t owners;Definition definition;bool workshopOnly;};
// Source: research/autoability_expansion/registry.json and definitions.json.
// These identities and allowed payloads do not register or enable native hooks.
inline constexpr Entry Entries[]={
    {"aeon_break_hp_mp_limit","Aeon Break HP/MP Limit",1,0x3FF00,Definition::Defined,true},
    {"aeon_break_damage_limit","Aeon Break Damage Limit",0,0x3FF00,Definition::Defined,true},
    {"spira_mana_spring","Mana Spring",0,0x3FFFF,Definition::Defined,false},
    {"spira_break_limits","Break Limits",1,0x3FFFF,Definition::Defined,false},
    {"spira_devils_bargain","Devil's Bargain",1,0x3FFFF,Definition::Defined,false},
    {"spira_wardens_oath","Warden's Oath",1,1u<<2,Definition::Defined,false},
    {"spira_arcane_focus","Arcane Focus",255,1u<<5,Definition::Pending,false},
    {"spira_double_drop","Double Drop",1,0x3FFFF,Definition::Defined,false},
    {"spira_triple_drop","Triple Drop",1,0x3FFFF,Definition::Defined,false},
    {"spira_element_eater","Element Eater",0,0x3FFFF,Definition::Defined,false},
    {"spira_hpmp_10","HPMP +10%",1,0x3FFFF,Definition::Defined,false},
    {"spira_hpmp_20","HPMP +20%",1,0x3FFFF,Definition::Defined,false},
    {"spira_hpmp_40","HPMP +40%",1,0x3FFFF,Definition::Defined,false},
    {"spira_hpmp_60","HPMP +60%",1,0x3FFFF,Definition::Defined,false},
    {"spira_aio_3","AIO +3%",1,0x3FFFF,Definition::Defined,false},
    {"spira_aio_6","AIO +6%",1,0x3FFFF,Definition::Defined,false},
    {"spira_aio_9","AIO +9%",1,0x3FFFF,Definition::Defined,false},
    {"spira_aio_12","AIO +12%",1,0x3FFFF,Definition::Defined,false},
    {"spira_str_mag_10","STR MAG +10%",0,0x3FFFF,Definition::Defined,false},
    {"spira_str_mag_20","STR MAG +20%",0,0x3FFFF,Definition::Defined,false},
    {"spira_def_mdef_10","DEF MDEF +10%",1,0x3FFFF,Definition::Defined,false},
    {"spira_def_mdef_20","DEF MDEF +20%",1,0x3FFFF,Definition::Defined,false},
    {"spira_foolstrike","Foolstrike",0,0x3FFFF,Definition::Pending,false},
    {"spira_fooltouch","Fooltouch",0,0x3FFFF,Definition::Pending,false},
    {"spira_fourstrike","Fourstrike",0,0x3FFFF,Definition::NativeBaseOnly,false},
    {"spira_fourtouch","Fourtouch",0,0x3FFFF,Definition::NativeBaseOnly,false},
    {"spira_spell_spring","Spell Spring",255,0x3FFFF,Definition::Pending,false}
};
static_assert(sizeof(Entries)/sizeof(Entries[0])==Count);
using Mapping=std::array<unsigned,Count>;
inline Mapping DefaultMapping() noexcept {Mapping result{};for(unsigned i=0;i<Count;++i)result[i]=FirstId+i;return result;}
enum class NameEncoding { Latin,NumericPlaceholder };
enum class MappingCode { NoTable,ReservedId,Collision,IdentityMismatch,PayloadMismatch,DefinitionPending,ConsumerUnavailable,Admitted };
struct Binding {MappingCode code=MappingCode::NoTable;bool rowVerified=false;unsigned word=0;};
using Bindings=std::array<Binding,Count>;
inline unsigned Word(const unsigned char* bytes) noexcept {return bytes[0]|(unsigned(bytes[1])<<8);}
inline std::uint32_t Dword(const unsigned char* bytes) noexcept {return Word(bytes)|(std::uint32_t(Word(bytes+2))<<16);}
inline bool Table(const unsigned char* bytes,std::size_t size,unsigned& last,std::size_t& pool) noexcept {
    if(!bytes||size<20||size>65535||Dword(bytes)!=1||Word(bytes+8)||Dword(bytes+16)!=20)return false;
    last=Word(bytes+10);const unsigned width=Word(bytes+12),length=Word(bytes+14);
    if(last>4095||width!=RowBytes||length!=(last+1)*RowBytes||length>size-20)return false;
    pool=20+length;return true;
}
inline bool PayloadMatches(unsigned index,const unsigned char* row,std::size_t size) noexcept {
    if(index>=Count||!row||size<RowBytes)return false;
    unsigned strike=0,absorb=0,amount=0,mask=0,flags=0;
    if(index==3)flags=0x600;else if(index==7)flags=0x1000;else if(index==8)flags=0x2000;else if(index==9)absorb=1;
    else if(index>=10&&index<=13){constexpr unsigned values[]={10,20,40,60};amount=values[index-10];mask=0x300;}
    else if(index>=14&&index<=17){amount=3*(index-13);mask=0x3F00;}
    else if(index>=18&&index<=19){amount=10*(index-17);mask=0xC00;}
    else if(index>=20&&index<=21){amount=10*(index-19);mask=0x3000;}
    else if(index==24||index==25)strike=15;
    for(unsigned at=16;at<RowBytes;++at){
        const unsigned expected=at==0x11?strike:at==0x12?absorb:at==0x55?amount:
            at==0x56?mask&255u:at==0x57?mask>>8:at==0x64?flags&255u:at==0x65?flags>>8:0;
        if(row[at]!=expected)return false;
    }
    return true;
}
inline unsigned Glyph(char ch) noexcept {
    if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z'))return static_cast<unsigned char>(ch)+15u;
    if(ch>='0'&&ch<='9')return static_cast<unsigned char>(ch);
    switch(ch){case ' ':return 58;case '%':return 63;case '\'':return 65;case '+':return 69;
        case '-':return 71;case '.':return 72;case '/':return 73;default:return 256;}
}
inline bool Named(const unsigned char* bytes,std::size_t size,std::size_t pool,unsigned id,unsigned index,NameEncoding encoding) noexcept {
    if(!bytes||index>=Count||id>4095||size<20||std::size_t(id)*RowBytes>size-20||RowBytes>size-20-std::size_t(id)*RowBytes)return false;
    const auto offset=Word(bytes+20+id*RowBytes);
    if(pool>=size||offset>=size-pool)return false;
    const auto* name=bytes+pool+offset;const auto available=size-pool-offset;
    if(encoding==NameEncoding::NumericPlaceholder){
        char expected[12]{};const int count=std::snprintf(expected,sizeof(expected),"%u",FirstId+index);
        return count>0&&static_cast<std::size_t>(count)<available&&!std::memcmp(name,expected,static_cast<std::size_t>(count))&&!name[count];
    }
    if(encoding!=NameEncoding::Latin)return false;
    const char* expected=Entries[index].label;std::size_t n=0;
    for(;expected[n]&&n<available;++n)if(name[n]!=Glyph(expected[n]))return false;
    return !expected[n]&&n<available&&!name[n];
}
inline bool OwnerKindMatches(unsigned index,unsigned owner,unsigned kind) noexcept {
    return index<Count&&owner<18&&kind<2&&Entries[index].kind==kind&&(Entries[index].owners&(std::uint32_t{1}<<owner))!=0;
}
inline Bindings Validate(const unsigned char* bytes,std::size_t size,const Mapping& mapping,const unsigned* otherIds,
                         unsigned otherCount,NameEncoding encoding,std::uint32_t admittedConsumers=0) noexcept {
    Bindings result{};unsigned last=0;std::size_t pool=0;
    if((otherCount&&!otherIds)||otherCount>64||!Table(bytes,size,last,pool))return result;
    for(unsigned i=0;i<Count;++i){
        auto& out=result[i];const unsigned id=mapping[i];
        if(!AutoAbilitySlots::Ascension(i,id)||id>last){out.code=MappingCode::ReservedId;continue;}
        bool collision=false;
        for(unsigned j=0;j<Count;++j)if(j!=i&&mapping[j]==id)collision=true;
        for(unsigned j=0;j<otherCount;++j)if(otherIds[j]==id)collision=true;
        if(collision){out.code=MappingCode::Collision;continue;}
        if(!Named(bytes,size,pool,id,i,encoding)){out.code=MappingCode::IdentityMismatch;continue;}
        if(!PayloadMatches(i,bytes+20+id*RowBytes,RowBytes)){out.code=MappingCode::PayloadMismatch;continue;}
        out.rowVerified=true;out.word=0x8000u+id;
        out.code=Entries[i].definition!=Definition::Defined?MappingCode::DefinitionPending:
            (admittedConsumers&(std::uint32_t{1}<<i))?MappingCode::Admitted:MappingCode::ConsumerUnavailable;
    }
    return result;
}
} // namespace FfxHooks::SpiraAbilities
