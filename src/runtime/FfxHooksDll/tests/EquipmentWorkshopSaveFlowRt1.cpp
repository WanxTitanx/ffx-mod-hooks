// Actual CRT-IAT read/write producers and the installed Workshop load detour.
// Only the unrelated post-load world-initialization tail is replaced in this
// private PE fixture. The native memcpy and destructive CRC function execute.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/RonsoPoolRuntime.h"
#include "../hooks/RonsoPoolSave.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/FahrenheitServices.h"
#include "PrivatePeFixture.h"
#include "WorkshopEconomyFixture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace FfxHooks;
using EquipmentWorkshop::SaveImage;
namespace {
uintptr_t base=0;
bool managedTransport=false;
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
unsigned Slot(const std::wstring& path){return static_cast<unsigned>(std::stoul(path.substr(path.find_last_of(L'_')+1)));}
uint16_t ChecksumAndClear(SaveImage& image);
bool Read(const std::wstring& path,SaveImage& bytes){
    if(managedTransport){
        const auto ticket=FfxHooks_FahrenheitBeginReadV2(path.c_str(),Slot(path),bytes.data(),static_cast<std::uint32_t>(bytes.size()));
        if(!ticket)return false;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        SaveImage disk{};DWORD count=0;
        bool ok=file!=INVALID_HANDLE_VALUE&&ReadFile(file,disk.data(),static_cast<DWORD>(disk.size()),&count,nullptr)&&count==disk.size()&&
            FfxHooks_FahrenheitTransformReadV2(ticket,reinterpret_cast<std::uintptr_t>(file),disk.data(),static_cast<std::uint32_t>(disk.size()))==1;
        if(file!=INVALID_HANDLE_VALUE)ok=CloseHandle(file)&&ok;
        if(ok){const auto expected=static_cast<uint16_t>(bytes[26]|(unsigned(bytes[27])<<8));ok=ChecksumAndClear(bytes)==expected;}
        if(!ok){FfxHooks_FahrenheitAbortIoV2(ticket);return false;}
        return FfxHooks_FahrenheitEndReadV2(ticket,1)==1;
    }
    void* file=openFile(path.c_str(),L"rb");if(!file)return false;uint32_t count=static_cast<uint32_t>(bytes.size());void* data=bytes.data();Patch(base+0x8E72F4,&count,4);Patch(base+0x8E72F8,&data,4);const auto got=NativeRead(file,reinterpret_cast<void*>(base+0x2F0213));closeFile(file);return got==bytes.size();
}
bool Write(const std::wstring& path,SaveImage& bytes){
    if(managedTransport){
        SaveImage output{};
        const auto ticket=FfxHooks_FahrenheitBeginWriteV2(path.c_str(),Slot(path),bytes.data(),static_cast<std::uint32_t>(bytes.size()),output.data());
        if(!ticket)return false;
        const auto temp=path+L".cooperative-test-tmp";
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD count=0;bool ok=file!=INVALID_HANDLE_VALUE&&FfxHooks_FahrenheitOpenWriteV2(ticket,reinterpret_cast<std::uintptr_t>(file))==1&&
            WriteFile(file,output.data(),static_cast<DWORD>(output.size()),&count,nullptr)&&count==output.size()&&FlushFileBuffers(file);
        if(file!=INVALID_HANDLE_VALUE)ok=CloseHandle(file)&&ok;
        if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!ok){FfxHooks_FahrenheitAbortIoV2(ticket);if(file!=INVALID_HANDLE_VALUE)DeleteFileW(temp.c_str());return false;}
        return FfxHooks_FahrenheitEndWriteV2(ticket,1)==1;
    }
    void* file=openFile(path.c_str(),L"wb");if(!file)return false;const auto got=NativeWrite(bytes.size(),bytes.data(),file,reinterpret_cast<void*>(base+0x2F06B8));closeFile(file);return got==bytes.size();
}
void Apply(SaveImage& image){reinterpret_cast<int(__cdecl*)(void*,const void*)>(base+0x4B5450)(reinterpret_cast<void*>(base+0xD2CA90),image.data());}
uint16_t ChecksumAndClear(SaveImage& image){return reinterpret_cast<uint16_t(__cdecl*)(void*)>(base+0x248030)(image.data());}
void Log(const char* s){std::fputs(s,stdout);}
void LostPostWrite(const wchar_t*,const unsigned char*,std::size_t) noexcept {}
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    managedTransport=std::strncmp(argv[4],"managed-",8)==0;
    const bool ronsoActive=std::strcmp(argv[4],"on")==0||std::strcmp(argv[4],"managed-on")==0;
    if(managedTransport){
        auto& state=Coexistence::runtime;state.Observe(true);
        if(!state.ConfigureServices(2,4)||!state.Ready(1,3)||!state.TryStart())return 2;
    }
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
    if(managedTransport&&!Coexistence::runtime.Finish(true))return 2;
    const unsigned char returnAfterCall[]={0x83,0xC4,0x10,0xC3};
    Patch(base+0x2F0228,returnAfterCall,sizeof(returnAfterCall));Patch(base+0x2F06C5,returnAfterCall,sizeof(returnAfterCall));
    const unsigned char postLoadReturn[]={0x31,0xC0,0xC3,0x90,0x90};Patch(base+0x4B546B,postLoadReturn,sizeof(postLoadReturn));
    SaveImage disk{},loaded{};std::ifstream f(argv[2],std::ios::binary);if(!f.read(reinterpret_cast<char*>(disk.data()),disk.size()))return 2;
    WorkshopEconomyFixture::Seed(disk);WorkshopEconomyFixture::Mode(1);
    const auto path=root+L"\\ffx_093";
    Check(Put(path,disk)&&Read(path,loaded),"real native fread reaches the production observer");
    workshop::State state{};
    Check(!EquipmentWorkshop::Capture(state),"reading or previewing a file alone cannot admit an inventory");
    const auto before=loaded;const auto crc=ChecksumAndClear(loaded);const auto stored=static_cast<uint16_t>(before[26]|(unsigned(before[27])<<8));
    bool onlyTrailer=true;for(unsigned i=0;i<loaded.size();++i)if((i<25844||i>=25848)&&loaded[i]!=before[i])onlyTrailer=false;
    Check(crc==stored&&onlyTrailer&&loaded[25844]==0&&loaded[25845]==0&&loaded[25846]==0&&loaded[25847]==0,"actual native checksum clears only the four-byte payload trailer");
    // Fahrenheit confirms a read after its CRC check. Existing consumers must
    // associate the same payload without rewriting the provider's ref buffer.
    if(!managedTransport)NativeSaveEvents::ReadCompleted(path.c_str(),disk.data(),loaded.data(),loaded.size());
    const auto unmodifiedSource=loaded;Apply(loaded);
    Check(EquipmentWorkshop::Capture(state),"actual load detour admits the native post-checksum image");
    Check(loaded==unmodifiedSource,"admission does not rewrite the game's load buffer");
    if(!EquipmentWorkshop::Capture(state)){std::printf("EquipmentWorkshopSaveFlowRt1 %u/%u passed\n",checks-failures,checks);return 1;}
    unsigned char mixed[22]{};mixed[2]=1;mixed[6]=255;mixed[11]=4;
    const unsigned words[]={0,100,98,99};
    for(unsigned i=0;i<4;++i){mixed[14+2*i]=static_cast<unsigned char>(words[i]);mixed[15+2*i]=0x80;}
    const auto created=reinterpret_cast<unsigned(__cdecl*)(const void*)>(base+0x3AB930)(mixed);
    Check(created>=0x5000&&created<0x50C8&&EquipmentWorkshop::Capture(state),
          "private native producer creates a mixed generic/numeric refinement fixture");
    if(created<0x5000||created>=0x50C8||!EquipmentWorkshop::Capture(state))return 2;
    const unsigned slot=created&0xFFF;
    const auto identity=state.pieces[slot].id;
    workshop::Request request{};request.op=workshop::Op::Mode;request.slot=static_cast<uint16_t>(slot);request.pieceId=identity;request.revision=state.revision;request.value=1;
    workshop::Plan plan{};
    Check(EquipmentWorkshop::Preview(request,plan)==workshop::Error::Ok&&EquipmentWorkshop::Commit(request,plan),"the admitted native inventory supports a reviewed Workshop transaction");
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].rank==0,"mode selection spends no refinement rank");
    request.op=workshop::Op::Refine;request.revision=state.revision;request.value=0;
    const auto spheres=state.items[73];
    const auto refinementGil=WorkshopEconomyFixture::Gil(base);
    Check(EquipmentWorkshop::Preview(request,plan)==workshop::Error::Ok&&EquipmentWorkshop::Commit(request,plan)&&
          EquipmentWorkshop::Capture(state)&&workshop::AbilityRank(state.pieces[slot],0)==1&&state.items[73]+1==spheres,
          "actual generic refinement commits one rank and its material debit");
    Check(refinementGil-WorkshopEconomyFixture::Gil(base)==10000,"four initial A ranks charge sequential native Gil prices");
    Check(!EquipmentWorkshop::Commit(request,plan),"native flow cannot confirm the same refinement twice");
    const auto donor=reinterpret_cast<unsigned(__cdecl*)(const void*)>(base+0x3AB930)(mixed);
    Check(donor>=0x5000&&donor<0x50C8&&EquipmentWorkshop::Capture(state),"native save-flow fixture adds a distinct fusion donor");
    if(donor<0x5000||donor>=0x50C8||!EquipmentWorkshop::Capture(state))return 2;
    const unsigned donorSlot=donor&0xFFF;
    workshop::Request mode{};mode.op=workshop::Op::Mode;mode.slot=static_cast<uint16_t>(donorSlot);
    mode.pieceId=state.pieces[donorSlot].id;mode.revision=state.revision;mode.value=1;
    Check(EquipmentWorkshop::Preview(mode,plan)==workshop::Error::Ok&&EquipmentWorkshop::Commit(mode,plan)&&EquipmentWorkshop::Capture(state),"save-flow donor uses the same mode as its target");
    const auto unfusedPath=root+L"\\ffx_092";
    SaveImage unfused=disk;std::memcpy(unfused.data()+64,reinterpret_cast<void*>(base+0xD2CA90),0x68C0);RonsoPool::SealSave(unfused);
    Check(Write(unfusedPath,unfused),"native write keeps an independent pre-fusion save and sidecar");
    const auto donorIdentity=state.pieces[donorSlot].id;
    workshop::Request fusion{};fusion.op=workshop::Op::Fuse;fusion.slot=static_cast<uint16_t>(slot);
    fusion.pieceId=identity;fusion.other=static_cast<uint16_t>(donorSlot);fusion.otherId=donorIdentity;
    fusion.revision=state.revision;fusion.count=2;fusion.from[1]=fusion.to[1]=1;
    const auto gilBeforeFusion=WorkshopEconomyFixture::Gil(base);
    Check(EquipmentWorkshop::Preview(fusion,plan)==workshop::Error::Ok&&EquipmentWorkshop::Commit(fusion,plan)&&EquipmentWorkshop::Capture(state),"native save flow commits two-ability fusion");
    Check(!state.pieces[donorSlot].id&&*reinterpret_cast<unsigned char*>(base+0xD30F2C+22*donorSlot+2)==0,"donor occupancy is removed before native save serialization");
    const auto afterFusionSpheres=state.items[73];
    const auto gilAfterFusion=WorkshopEconomyFixture::Gil(base);
    Check(gilBeforeFusion-gilAfterFusion==20000,"fusion debits actual native Gil in the same transaction");
    SaveImage saved=disk;std::memcpy(saved.data()+64,reinterpret_cast<void*>(base+0xD2CA90),0x68C0);RonsoPool::SealSave(saved);
    // Simulate process loss after successful native fwrite but before the
    // post-write metadata callback. The pre-write journal must already exist.
    const NativeSaveEvents::Observer* observer=nullptr;
    for(const auto& entry:NativeSaveEvents::observers){const auto* value=entry.load();if(value&&value->selectRead)observer=value;}
    Check(observer!=nullptr,"the paid checkpoint selector remains registered alongside other save observers");if(!observer)return 1;
    NativeSaveEvents::Observer lostPostWrite=*observer;lostPostWrite.write=LostPostWrite;
    NativeSaveEvents::Unsubscribe(observer);Check(NativeSaveEvents::Subscribe(&lostPostWrite),"post-write interruption fixture replaces only its own observer");
    const bool nativeWritten=Write(path,saved);
    NativeSaveEvents::Unsubscribe(&lostPostWrite);Check(NativeSaveEvents::Subscribe(observer),"the original checkpoint observer is restored");
    Check(nativeWritten,"actual native fwrite succeeds with its completion callback interrupted");
    Check(Read(path,loaded),"actual read reopens the saved version");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].id==identity&&state.pieces[slot].mode==2&&workshop::AbilityRank(state.pieces[slot],2)==1&&state.items[73]==afterFusionSpheres&&WorkshopEconomyFixture::Gil(base)==gilAfterFusion,"native read/checksum/apply roundtrip restores the same extension identity and native Gil");
    Check(!state.pieces[donorSlot].id&&!state.pieces[donorSlot].native[2],"native reload preserves donor consumption");
    fusion.revision=state.revision;
    Check(EquipmentWorkshop::Preview(fusion,plan)==workshop::Error::Stale,"reloading cannot make a consumed donor reusable");
    Check(Read(unfusedPath,loaded),"independent pre-fusion save is observed");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[donorSlot].id==donorIdentity&&state.pieces[donorSlot].native[2]&&WorkshopEconomyFixture::Gil(base)==gilBeforeFusion,"fusion in one save does not consume donor or Gil in another save");
    Check(Read(path,loaded),"fused save is observed again");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&!state.pieces[donorSlot].id&&state.items[73]==afterFusionSpheres,"returning to fused save restores consumption and its exact material debit");
    const auto alternate=root+L"\\ffx_094";
    Check(Put(alternate,saved)&&Read(alternate,loaded),"another slot can contain identical native bytes in the reused read buffer");
    ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].mode==0,"reused buffer binds the latest completed file, not another slot's extension");
    Check(Read(path,loaded),"the original slot is observed again");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.pieces[slot].id==identity&&state.pieces[slot].mode==2&&workshop::AbilityRank(state.pieces[slot],2)==1&&WorkshopEconomyFixture::Gil(base)==gilAfterFusion,"returning to the original slot restores its own extension and native Gil");
    const auto checkpoint=state;const auto checkpointGil=WorkshopEconomyFixture::Gil(base);
    WorkshopEconomyFixture::Mode(2);
    workshop::Request replay{};replay.op=workshop::Op::Refine;replay.slot=static_cast<uint16_t>(slot);
    replay.pieceId=identity;replay.revision=state.revision;workshop::Plan firstRoll{};
    Check(EquipmentWorkshop::Preview(replay,firstRoll)==workshop::Error::Ok&&EquipmentWorkshop::Commit(replay,firstRoll)&&EquipmentWorkshop::Capture(state),
          "unsaved random refinement commits its outcome and resources together");
    Check(state.rolls==checkpoint.rolls+1&&WorkshopEconomyFixture::Gil(base)==checkpointGil-firstRoll.gilDebit,
          "the unsaved outcome has exactly one generator advance and one debit");
    const auto accepted=state;const auto acceptedGil=WorkshopEconomyFixture::Gil(base);
    NativeSaveEvents::ResetCompleted();
    Check(!EquipmentWorkshop::Capture(state),"native reset retires unsaved Workshop admission");
    Check(Read(path,loaded),"post-reset reload observes the actual saved checkpoint");ChecksumAndClear(loaded);Apply(loaded);
    Check(EquipmentWorkshop::Capture(state)&&state.rng==accepted.rng&&state.rolls==accepted.rolls&&
          std::memcmp(state.pieces,accepted.pieces,sizeof(state.pieces))==0&&
          std::memcmp(state.items,accepted.items,sizeof(state.items))==0&&WorkshopEconomyFixture::Gil(base)==acceptedGil,
          "reload recovers the accepted paid outcome and RNG, not a refundable opportunity to choose another roll");
    Check(!EquipmentWorkshop::Commit(replay,firstRoll),"a pre-reset confirmation cannot replay a durable accepted transaction");
    replay.revision=state.revision;workshop::Plan repeatedRoll{};
    Check(EquipmentWorkshop::Preview(replay,repeatedRoll)==workshop::Error::Ok&&
          repeatedRoll.after.rolls==accepted.rolls+1&&state.rng==accepted.rng&&
          std::memcmp(repeatedRoll.after.items,accepted.items,sizeof(accepted.items))!=0&&repeatedRoll.gilDebit>0,
          "a post-reload refinement must pay again and advance beyond the accepted roll");
    WorkshopEconomyFixture::Mode(1);
    Check(Read(path,loaded),"fresh file observation precedes the negative load case");ChecksumAndClear(loaded);loaded[0x44DC+22*slot]^=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"a non-checksum payload change cannot borrow the observed save's identity");
    Check(Read(path,loaded),"fresh observation precedes a change outside the CRC-covered region");ChecksumAndClear(loaded);loaded.back()^=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"full payload association rejects trailing bytes even when the short native CRC still matches");
    Check(Read(path,loaded),"fresh observation precedes an unexpected checksum-field change");ChecksumAndClear(loaded);loaded[25846]=1;
    Apply(loaded);Check(!EquipmentWorkshop::Capture(state),"only the native four-byte zero transition may be canonicalized");
    EquipmentWorkshop::RequestStop();
    std::printf("EquipmentWorkshopSaveFlowRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
