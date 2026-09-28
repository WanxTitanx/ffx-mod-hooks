#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include "PrivatePeFixture.h"
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const std::filesystem::path scratch(argv[2]);std::filesystem::create_directories(scratch);
    const auto temporary=scratch.wstring()+L"\\";
    SetEnvironmentVariableW(L"TEMP",temporary.c_str());SetEnvironmentVariableW(L"TMP",temporary.c_str());
    SetEnvironmentVariableW(L"FFXHOOKS_VALIDATE_ONLY",L"1");SetEnvironmentVariableW(L"FFXHOOKS_INSTALL_DELAY_MS",L"0");
    SetEnvironmentVariableW(L"FFXHOOKS_ARCANA",L"1");SetEnvironmentVariableW(L"FFXHOOKS_ARCANA_FULL_DECK",L"1");
    const auto game=LoadLibraryExW(argv[3],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!game||!PrivatePeFixture::NormalizeRelocations(game))return 2;
    const unsigned rvas[]={0x4CEFF0,0x38E680,0x39A0D0};unsigned char before[3][32]{};
    for(unsigned i=0;i<3;++i)std::memcpy(before[i],reinterpret_cast<unsigned char*>(game)+rvas[i],32);
    if(!LoadLibraryW(argv[1])){std::printf("FAIL DLL load error=%lu\n",GetLastError());return 1;}
    for(unsigned attempt=0;attempt<400;++attempt){
        Sleep(25);
        std::ifstream file(scratch/L"ffx-hooks.log",std::ios::binary);
        std::string log((std::istreambuf_iterator<char>(file)),{});
        if(log.find("worker thread leave")!=std::string::npos&&log.find("StartupTiming stage=install-complete")!=std::string::npos){
            if(log.find("Arcana combat consumers installed")!=std::string::npos||log.find("Arcana Equip: native category/picker callbacks installed")!=std::string::npos){std::puts("FAIL validate-only admitted Arcana");return 1;}
            for(unsigned i=0;i<3;++i)if(std::memcmp(before[i],reinterpret_cast<unsigned char*>(game)+rvas[i],32)){std::puts("FAIL validation-only changed a native Arcana boundary");return 1;}
            std::puts("PASS Arcana DLL x86 loader/worker smoke with validation-only and unchanged private PE boundaries; no game launched");return 0;
        }
    }
    std::puts("FAIL DLL worker did not reach the bounded initialization checkpoint");return 1;
}
