#pragma once
// Jarvis-HOOK: construct an authored private kernel from the supplied read-only
// native fixture. Existing rows/text remain intact; no installed asset is edited.
#include "../hooks/SpiraAbilityCatalog.h"
#include "../hooks/VanguardCatalog.h"
#include <vector>
#include <cstring>
#include <stdexcept>
namespace SpiraKernelFixture {
inline void Word(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
inline void Dword(unsigned char* p,unsigned v){std::memcpy(p,&v,4);}
inline std::vector<unsigned char> Build(const std::vector<unsigned char>& original){
    namespace S=FfxHooks::SpiraAbilities;
    unsigned last=0;std::size_t pool=0;
    if(!S::Table(original.data(),original.size(),last,pool)||last<130)throw std::runtime_error("Invalid private native ability kernel");
    constexpr unsigned count=201,body=count*108;
    std::vector<unsigned char> result(20+body);
    Dword(result.data(),1);Word(result.data()+10,count-1);Word(result.data()+12,108);
    Word(result.data()+14,body);Dword(result.data()+16,20);
    const auto copy=(std::min)(std::size_t(body),pool-20);
    std::memcpy(result.data()+20,original.data()+20,copy);
    result.insert(result.end(),original.begin()+pool,original.end());
    auto name=[&](unsigned id,const char* text){
        const auto offset=result.size()-20-body;
        if(offset>65535)throw std::runtime_error("Private text pool exceeds its native WORD offset");
        Word(result.data()+20+108*id,static_cast<unsigned>(offset));
        for(;*text;++text){const auto glyph=S::Glyph(*text);if(glyph>255)throw std::runtime_error("Unrepresentable fixture label");result.push_back(static_cast<unsigned char>(glyph));}
        result.push_back(0);
    };
    for(const auto& ability:FfxHooks::Vanguard::Abilities){
        std::memset(result.data()+20+108*ability.id,0,108);name(ability.id,ability.label);
    }
    for(unsigned i=0;i<S::Count;++i){
        const unsigned id=S::FirstId+i;auto* row=result.data()+20+108*id;std::memset(row,0,108);
        if(i==3)Word(row+0x64,0x600);
        else if(i==7)Word(row+0x64,0x1000);
        else if(i==8)Word(row+0x64,0x2000);
        else if(i==9)row[0x12]=1;
        else if(i>=10&&i<=13){constexpr unsigned values[]={10,20,40,60};row[0x55]=static_cast<unsigned char>(values[i-10]);Word(row+0x56,0x300);}
        else if(i>=14&&i<=17){row[0x55]=static_cast<unsigned char>(3*(i-13));Word(row+0x56,0x3F00);}
        else if(i>=18&&i<=19){row[0x55]=static_cast<unsigned char>(10*(i-17));Word(row+0x56,0xC00);}
        else if(i>=20&&i<=21){row[0x55]=static_cast<unsigned char>(10*(i-19));Word(row+0x56,0x3000);}
        else if(i==24||i==25)row[0x11]=15;
        name(id,S::Entries[i].label);
    }
    if(result.size()>65535)throw std::runtime_error("Private kernel exceeds the loaded-size WORD");
    return result;
}
} // namespace SpiraKernelFixture
