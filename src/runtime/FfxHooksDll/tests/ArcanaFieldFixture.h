#pragma once
#include "../shared/ExecutableProfile.h"
#include "WorkshopFieldFixture.h"
namespace ArcanaFieldFixture {
// RT1 substitutes only unavailable Sphere Grid/gear input preparation. The
// real final clamp call sites, stat stores and installed field/clamp detours run.
class FinalStores {
    std::uintptr_t base_=0;
    unsigned char* code_=nullptr;
    std::array<unsigned char,5> before_{};
public:
    bool Open(std::uintptr_t base,unsigned actor,unsigned hp,unsigned mp){
        base_=base;code_=static_cast<unsigned char*>(VirtualAlloc(nullptr,512,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!code_)return false;
        std::vector<unsigned char> code={0x53,0x56,0x57};
        auto word=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(value>>(8*i)));};
        auto local=[&](int offset,unsigned value){code.insert(code.end(),{0xC7,0x85});word(static_cast<unsigned>(offset));word(value);};
        for(int at=-0x74;at<=-8;at+=4)local(at,at>=-0x3C?100u:0u);
        local(-0x98,actor);local(-0x1C,hp);local(-0x18,mp);
        code.push_back(0xA1);word(static_cast<unsigned>(base+(::FfxHooks::ExecutableProfile::Rva<0x8613D8>())));code.insert(code.end(),{0x33,0xC5,0x89,0x45,0xFC});
        code.push_back(0xBB);word(static_cast<unsigned>(base+(::FfxHooks::ExecutableProfile::Rva<0xD3205C>())+actor*0x94));
        code.insert(code.end(),{0xC7,0x43,0x4A,0,0,0,0,0x66,0xC7,0x43,0x4E,0,0,0xE9});
        word(static_cast<unsigned>(base+(::FfxHooks::ExecutableProfile::Rva<0x386850>())-reinterpret_cast<std::uintptr_t>(code_)-code.size()-4));
        std::memcpy(code_,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),code_,code.size());
        std::memcpy(before_.data(),reinterpret_cast<void*>(base+(::FfxHooks::ExecutableProfile::Rva<0x3861B9>())),5);
        unsigned char branch[5]={0xE9};const auto delta=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(code_)-base-::FfxHooks::ExecutableProfile::Rva<0x3861B9>()-5);std::memcpy(branch+1,&delta,4);
        return WorkshopFieldFixture::Write(base+(::FfxHooks::ExecutableProfile::Rva<0x3861B9>()),branch,5);
    }
    ~FinalStores(){if(code_){WorkshopFieldFixture::Write(base_+::FfxHooks::ExecutableProfile::Rva<0x3861B9>(),before_.data(),5);VirtualFree(code_,0,MEM_RELEASE);}}
};
}
