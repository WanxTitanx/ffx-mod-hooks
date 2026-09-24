// Actual CRT-IAT read/write producers and the installed Workshop load detour.
// Only the unrelated post-load world-initialization tail is replaced in this
// private PE fixture. The native memcpy and destructive CRC function execute.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolSave.h"
#include "PrivatePeFixture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace FfxHooks;
using EquipmentWorkshop::SaveImage;
namespace {
uintptr_t base=0;
using OpenFn=void*(__cdecl*)(const wchar_t*,const wchar_t*);
using CloseFn=int(__cdecl*)(void*);
OpenFn openFile=nullptr;CloseFn closeFile=nullptr;
unsigned checks=0,failures=0;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
bool Patch(uintptr_t at,const void* bytes,size_t size){DWORD prior=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),size,PAGE_EXECUTE_READWRITE,&prior))return false;std::memcpy(reinterpret_cast<void*>(at),bytes,size);const bool ok=VirtualProtect(reinterpret_cast<void*>(at),size,prior,&ignored)!=FALSE;FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),size);return ok;}
__declspec(naked) size_t __cdecl NativeWrite(size_t,void*,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        push edi
        mov edi,[ebp+0Ch]
        mov esi,[ebp+10h]
        call dword ptr [ebp+14h]
        pop edi
        pop esi
        pop ebp
        ret
    }
}
__declspec(naked) size_t __cdecl NativeRead(void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        mov esi,[ebp+8]
        call dword ptr [ebp+0Ch]
        pop esi
        pop ebp
        ret
    }
}
bool Put(const std::wstring& path,const SaveImage& bytes){HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);if(f==INVALID_HANDLE_VALUE)return false;DWORD count=0;const bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();CloseHandle(f);return ok;}
bool Read(const std::wstring& path,SaveImage& bytes){void* file=openFile(path.c_str(),L"rb");if(!file)return false;uint32_t count=static_cast<uint32_t>(bytes.size());void* data=bytes.data();Patch(base+0x8E72F4,&count,4);Patch(base+0x8E72F8,&data,4);const auto got=NativeRead(file,reinterpret_cast<void*>(base+0x2F0213));closeFile(file);return got==bytes.size();}
bool Write(const std::wstring& path,SaveImage& bytes){void* file=openFile(path.c_str(),L"wb");if(!file)return false;const auto got=NativeWrite(bytes.size(),bytes.data(),file,reinterpret_cast<void*>(base+0x2F06B8));closeFile(file);return got==bytes.size();}
void Apply(SaveImage& image){reinterpret_cast<int(__cdecl*)(void*,const void*)>(base+0x4B5450)(reinterpret_cast<void*>(base+0xD2CA90),image.data());}
uint16_t ChecksumAndClear(SaveImage& image){return reinterpret_cast<uint16_t(__cdecl*)(void*)>(base+0x248030)(image.data());}
void Log(const char* s){std::fputs(s,stdout);}
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    const bool ronsoActive=std::strcmp(argv[4],"on")==0;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES),crt=LoadLibraryW(L"msvcr110.dll");
    if(!image||!crt||!PrivatePeFixture::NormalizeRelocations(image))return 2;base=reinterpret_cast<uintptr_t>(image);
    const auto nativeRead=GetProcAddress(crt,"fread"),nativeWrite=GetProcAddress(crt,"fwrite"),nativeCopy=GetProcAddress(crt,"memcpy");
    openFile=reinterpret_cast<OpenFn>(GetProcAddress(crt,"_wfopen"));closeFile=reinterpret_cast<CloseFn>(GetProcAddress(crt,"fclose"));
    if(!nativeRead||!nativeWrite||!nativeCopy||!openFile||!closeFile)return 2;
    Patch(base+0x70C3F4,&nativeRead,4);Patch(base+0x70C428,&nativeWrite,4);Patch(base+0x70C368,&nativeCopy,4);
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));CreateDirectoryW(root.c_str(),nullptr);
    Check(EquipmentWorkshop::StartForTests(base,true,(root+L"\\workshop").c_str(),Log),"production Workshop detours installed");
    RonsoPool::PreparedRuntime prepared{};
    Check(RonsoPool::PrepareRuntime(base,ronsoActive,Log,&prepared,(root+L"\\ronso").c_str())&&RonsoPool::InstallIoImports(),"shared native save I/O owns the real FFX imports");
    if(failures)return 2;RonsoPool::ActivateRuntime();
    const unsigned char returnAfterCall[]={0x83,0xC4,0x10,0xC3};
    Patch(base+0x2F0228,returnAfterCall,sizeof(returnAfterCall));Patch(base+0x2F06C5,returnAfterCall,sizeof(returnAfterCall));
    const unsigned char postLoadReturn[]={0x31,0xC0,0xC3,0x90,0x90};Patch(base+0x4B546B,postLoadReturn,sizeof(postLoadReturn));
    SaveImage disk{},loaded{};std::ifstream f(argv[2],std::ios::binary);if(!f.read(reinterpret_cast<char*>(disk.data()),disk.size()))return 2;
    const auto path=root+L"\\ffx_093";
    Check(Put(path,disk)&&Read(path,loaded),"real native fread reaches the production observer");
    workshop::State state{};
    Check(!EquipmentWorkshop::Capture(state),"reading or previewing a file alone cannot admit an inventory");
    const auto before=loaded;const auto crc=ChecksumAndClear(loaded);const auto stored=static_cast<uint16_t>(before[26]|(unsigned(before[27])<<8));
    bool onlyTrailer=true;for(unsigned i=0;i<loaded.size();++i)if((i<25844||i>=25848)&&loaded[i]!=before[i])onlyTrailer=false;
    Check(crc==stored&&onlyTrailer&&loaded[25844]==0&&loaded[25845]==0&&loaded[25846]==0&&loaded[25847]==0,"actual native checksum clears only the four-byte payload trailer");
    const auto unmodifiedSource=loaded;Apply(loaded);
    Check(EquipmentWorkshop::Capture(state),"actual load detour admits the native post-checksum image");
    Check(loaded==unmodifiedSource,"admission does not rewrite the game's load buffer");
    if(!EquipmentWorkshop::Capture(state)){std::printf("EquipmentWorkshopSaveFlowRt1 %u/%u passed\n",checks-failures,checks);return 1;}
    unsigned slot=200;for(unsigned i=0;i<200;++i)if(state.pieces[i].id&&state.pieces[i].native[6]==255&&!(state.pieces[i].native[3]&12)&&state.pieces[i].native[4]<7){slot=i;break;}if(slot==200)return 2;
    const auto identity=state.pieces[slot].id;
    workshop::Request request{};request.op=workshop::Op::Mode;request.slot=static_cast<uint16_t>(slot);request.pieceId=identity;request.revision=state.revision;request.value=1;
    workshop::Plan plan{};
    Check(EquipmentWorkshop::Preview(request,plan)==workshop::Error::Ok&&EquipmentWorkshop::Commit(request,plan),"the admitted native inventory supports a reviewed Workshop transaction");
    SaveImage saved=disk;std::memcpy(saved.data()+64,reinterpret_cast<void*>(base+0xD2CA90),0x68C0);RonsoPool::SealSave(saved);
    Check(Write(path,saved),"actual native fwrite persists the matching extension");
    Check(Read(path,loaded),"actual read reopens the saved version");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].id==identity&&state.pieces[slot].mode==1,"native read/checksum/apply roundtrip restores the same extension identity");
    const auto alternate=root+L"\\ffx_094";
    Check(Put(alternate,saved)&&Read(alternate,loaded),"another slot can contain identical native bytes in the reused read buffer");
    ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].mode==0,"reused buffer binds the latest completed file, not another slot's extension");
    Check(Read(path,loaded),"the original slot is observed again");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].id==identity&&state.pieces[slot].mode==1,"returning to the original slot restores its own extension");
    Check(Read(path,loaded),"fresh file observation precedes the negative load case");ChecksumAndClear(loaded);loaded[0x44DC+22*slot]^=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"a non-checksum payload change cannot borrow the observed save's identity");
    Check(Read(path,loaded),"fresh observation precedes a change outside the CRC-covered region");ChecksumAndClear(loaded);loaded.back()^=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"full payload association rejects trailing bytes even when the short native CRC still matches");
    Check(Read(path,loaded),"fresh observation precedes an unexpected checksum-field change");ChecksumAndClear(loaded);loaded[25846]=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"only the native four-byte zero transition may be canonicalized");
    EquipmentWorkshop::RequestStop();
    std::printf("EquipmentWorkshopSaveFlowRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
