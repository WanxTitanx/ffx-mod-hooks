#include <cstdio>
#if __has_include("../hooks/NativeSaveSlotPath.h")
#include "../hooks/NativeSaveSlotPath.h"
#include <string>
#include <initializer_list>
static int checks=0,failures=0;
static void Check(bool ok){++checks;if(!ok)++failures;}
int main(){
    using FfxHooks::IsNativeSaveSlotPath;
    for(const wchar_t* leaf:{L"ffx_000",L"ffx_001",L"FFX_999",L"ffx_1000",L"ffx_100000",L"ffx_2147483647"})
        Check(IsNativeSaveSlotPath(std::wstring(L"C:/saves/")+leaf));
    for(const wchar_t* leaf:{L"ffx_",L"ffx_0",L"ffx_01",L"ffx_001.bak",L"ffx_0000",L"ffx_0999",L"ffx_2147483648",L"ffx_999999999999",L"ffx2_001"})
        Check(!IsNativeSaveSlotPath(std::wstring(L"C:/saves/")+leaf));
    Check(!IsNativeSaveSlotPath(L"ffx_001"));
    for(unsigned slot=0;slot<10000;++slot){
        std::wstring leaf=std::to_wstring(slot);while(leaf.size()<3)leaf.insert(leaf.begin(),L'0');
        Check(IsNativeSaveSlotPath(L"C:/set/ffx_"+leaf));
    }
    std::printf("Native save slot RT0: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL: canonical extended save-slot policy is missing");return 1;}
#endif
