#pragma once
// RT1-only entry/exit bridge in a private mapped PE. Calls the installed
// production Field/Gear/Row detours with real caller addresses. Grid setup and
// final stat stores are deliberately skipped; this is not an in-game test.
#include "../shared/ExecutableProfile.h"
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>
namespace WorkshopFieldFixture {
inline bool Write(std::uintptr_t address,const void* bytes,unsigned size){
    DWORD prior=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),size,PAGE_EXECUTE_READWRITE,&prior))return false;
    std::memcpy(reinterpret_cast<void*>(address),bytes,size);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size);
    return VirtualProtect(reinterpret_cast<void*>(address),size,prior,&ignored)!=FALSE;
}
inline int Percent(std::uintptr_t base,unsigned owner,unsigned statIndex){
    if(owner>=7 || statIndex>=14)return INT_MIN;
    auto* entry=static_cast<unsigned char*>(VirtualAlloc(nullptr,512,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!entry)return INT_MIN;
    // The MinHook trampoline has already executed push ebp/mov ebp,esp/sub esp,E4.
    std::vector<unsigned char> code={0x53,0x56,0x57};
    auto imm=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(value>>(8*i)));};
    for(int offset=-0x74;offset<=-8;offset+=4){
        code.insert(code.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(offset));imm(offset>=-0x3C?100:0);
    }
    // The native loop at RVA386802 advances this local from weapon to armor.
    // Initialize it instead of bypassing the second equipped piece entirely.
    code.insert(code.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(-0x90));imm(0);
    code.push_back(0xBB);imm(static_cast<std::uint32_t>(base+(::FfxHooks::ExecutableProfile::Rva<0xD3205C>())+owner*0x94));
    code.insert(code.end(),{0x31,0xC0,0xE9});
    imm(static_cast<std::uint32_t>(base+(::FfxHooks::ExecutableProfile::Rva<0x386765>())-reinterpret_cast<std::uintptr_t>(entry)-code.size()-4));
    const auto tail=code.size();
    code.insert(code.end(),{0x8B,0x45,static_cast<unsigned char>(-0x3C+4*statIndex),0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3});
    std::memcpy(entry,code.data(),code.size());
    struct Patch {std::uintptr_t address,target;unsigned size;std::array<unsigned char,6> before;};
    Patch patches[]={
        {base+(::FfxHooks::ExecutableProfile::Rva<0x3861B9>()),reinterpret_cast<std::uintptr_t>(entry),5,{}},
        {base+(::FfxHooks::ExecutableProfile::Rva<0x386850>()),reinterpret_cast<std::uintptr_t>(entry)+tail,6,{}}
    };
    unsigned applied=0;bool ok=true;
    for(auto& patch:patches){
        std::memcpy(patch.before.data(),reinterpret_cast<void*>(patch.address),patch.size);
        std::array<unsigned char,6> branch={0xE9,0,0,0,0,0x90};
        const auto delta=static_cast<std::uint32_t>(patch.target-patch.address-5);
        std::memcpy(branch.data()+1,&delta,4);++applied;
        if(!Write(patch.address,branch.data(),patch.size)){ok=false;break;}
    }
    FlushInstructionCache(GetCurrentProcess(),entry,code.size());
    const int result=ok?reinterpret_cast<int(__cdecl*)(unsigned)>(base+(::FfxHooks::ExecutableProfile::Rva<0x3861B0>()))(owner):INT_MIN;
    while(applied){const auto& patch=patches[--applied];if(!Write(patch.address,patch.before.data(),patch.size))ok=false;}
    VirtualFree(entry,0,MEM_RELEASE);return ok?result:INT_MIN;
}
}
