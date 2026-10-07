// Jarvis-HOOK: execute only the native read-only learned-command getter in a private mapped PE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "PrivatePeFixture.h"
#include "../shared/ffx_addresses.h"

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    HMODULE image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!image||!PrivatePeFixture::NormalizeRelocations(image))return 3;
    auto* base=reinterpret_cast<unsigned char*>(image);
    using Learned=int(__cdecl*)(int,int);
    const auto learned=reinterpret_cast<Learned>(base+RVA_FFX_BATTLE_HAS_COMMAND_BIT_SAVE);
    // Independent native evidence: command IDs >=96 use a party-wide word bank,
    // base RVA D307FC. IDs 104..115 occupy bits 8..19, the two bytes at D307FD.
    constexpr unsigned bank=0xD307FC;
    constexpr unsigned actualRonsoByte=0xD307FD;
    unsigned char originalBank[4]{};std::memcpy(originalBank,base+bank,4);
    unsigned short originalTarget=0;std::memcpy(&originalTarget,base+RVA_FFX_KIMAHRI_RONSO_UNLOCK,2);
    std::memset(base+bank,0,4);
    unsigned checks=0,failures=0;
    for(unsigned id=104;id<=115;++id) {
        std::memset(base+bank,0,4);
        const unsigned short bit=static_cast<unsigned short>(1u<<(id-104));
        std::memcpy(base+RVA_FFX_KIMAHRI_RONSO_UNLOCK,&bit,2);
        const bool ok=learned(3,0x3000|id)!=0;
        ++checks;if(!ok){++failures;std::printf("FAIL: native command %u did not observe the selected Ronso address\n",id);}
    }
    std::memcpy(base+RVA_FFX_KIMAHRI_RONSO_UNLOCK,&originalTarget,2);
    std::memcpy(base+bank,originalBank,4);
    ++checks;if(RVA_FFX_KIMAHRI_RONSO_UNLOCK!=actualRonsoByte)++failures;
    std::printf("RONSO UNLOCK NATIVE RT1: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
