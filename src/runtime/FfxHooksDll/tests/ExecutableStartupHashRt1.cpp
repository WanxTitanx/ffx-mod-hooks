#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../hooks/ExecutableStartupHash.h"
#include <cstdio>
#include <string>

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}};
    HMODULE image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    check(image!=nullptr,"private current PE maps without executing its entrypoint");
    if(!image)return 2;
    check(FfxHooks::ExecutableStartup::VerifyModuleFile(image),"exact current native file is admitted by SHA256");
    check(!FfxHooks::ExecutableStartup::VerifyModuleFile(nullptr),"missing host file fails closed");
    const std::wstring changed=std::wstring(argv[2])+L"\\sha-mutated.exe";
    check(CopyFileW(argv[1],changed.c_str(),TRUE)!=FALSE,"isolated tamper control is created");
    HANDLE file=CreateFileW(changed.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(file!=INVALID_HANDLE_VALUE,"only the private copied file is writable");
    if(file!=INVALID_HANDLE_VALUE){
        LARGE_INTEGER at{};at.QuadPart=FfxHooks::ExecutableProfile::FileBytes-1;
        unsigned char byte=0;DWORD count=0;
        bool ok=SetFilePointerEx(file,at,nullptr,FILE_BEGIN)&&ReadFile(file,&byte,1,&count,nullptr)&&count==1;
        byte^=1u;ok=ok&&SetFilePointerEx(file,at,nullptr,FILE_BEGIN)&&WriteFile(file,&byte,1,&count,nullptr)&&count==1;
        check(ok,"tamper control preserves the PE header and exact file size");CloseHandle(file);
        HMODULE tampered=LoadLibraryExW(changed.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
        check(tampered!=nullptr,"tampered same-profile PE still maps without entrypoint execution");
        if(tampered){check(!FfxHooks::ExecutableStartup::VerifyModuleFile(tampered),"same header and file size cannot bypass SHA256 admission");FreeLibrary(tampered);}
    }
    FreeLibrary(image);check(DeleteFileW(changed.c_str())!=FALSE,"private tamper control is removed");
    std::printf("STARTUP FILE HASH RT1: %u/%u passed\n",checks-failures,checks);
    return failures?1:0;
}
