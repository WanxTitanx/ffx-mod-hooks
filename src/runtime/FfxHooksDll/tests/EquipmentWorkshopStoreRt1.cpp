#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../hooks/EquipmentWorkshopStore.h"
#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace FfxHooks::EquipmentWorkshop;
static unsigned checks,failures;
static void Check(bool b,const char* m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}
#include "WorkshopCheckpointStoreCases.inl"
#include "WorkshopCheckpointCrashCases.inl"
int main(int argc,char** argv){
    if(argc==6&&std::strcmp(argv[3],"checkpoint-child")==0)return CheckpointCrashChild(argv);
    if(argc!=3)return 2;
    SaveImage file{};std::ifstream input(argv[1],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(file.data()),file.size()))return 2;
    const std::wstring root(argv[2],argv[2]+std::strlen(argv[2]));
    Store store;Check(store.Initialize(root,true),"private extension directory initialized");
    workshop::State state{},loaded{};
    Check(ImportSave(file,17,state),"real native save imports with22-byte gear and bounded material counts");
    Check(MatchesSave(file,state),"native association matches exact gear and materials");
    const auto path=root+L"\\ffx_091";
    Check(store.Read(path,file,loaded)==StoreResult::Missing,"missing metadata is explicit");
    Check(store.Write(path,file,state),"full extension snapshot is atomically persisted");
    Check(store.Read(path,file,loaded)==StoreResult::Found && std::memcmp(&state,&loaded,sizeof(state))==0,"save/readback preserves every identity and generator byte");
    Check(store.Read(root+L"\\ffx_092",file,loaded)==StoreResult::Missing,"another save path cannot borrow identities");
    auto changed=file;changed[100]^=1;
    Check(store.Read(path,changed,loaded)==StoreResult::Missing,"another save version cannot borrow metadata");
    auto wrong=state;wrong.items[0]^=1;
    Check(!store.Write(path,file,wrong),"metadata for different native quantities is rejected");
    const auto leaf=store.RecordPath(path,file);
    HANDLE h=CreateFileW(leaf.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    const unsigned char bad=0;DWORD written=0;
    if(h!=INVALID_HANDLE_VALUE){WriteFile(h,&bad,1,&written,nullptr);CloseHandle(h);}
    Check(store.Read(path,file,loaded)==StoreResult::Invalid,"corrupt record is quarantined");
    Check(!store.Write(path,file,state),"corruption is not silently overwritten");
    Check(store.RecordPath(root+L"\\foreign.bin",file).empty(),"only actual native save leaves have ownership records");
    const auto pinnedPath=root+L"\\ffx_093";Hash disk{};Fingerprint(file.data(),file.size(),disk);
    auto first=state,second=state;first.rng=123;second.rng=456;
    Check(store.PinLoaded(pinnedPath,disk,file,first),"first import durably pins its generator");
    Check(store.PinLoaded(pinnedPath,disk,file,second)&&second.rng==123,"repeated import reads the existing generator instead of replacing it");
    const auto pendingPath=root+L"\\ffx_095";
    Check(store.PrepareSave(pendingPath,file,state),"pre-save journal persists exact native bytes and metadata");
    Check(store.Read(pendingPath,file,loaded)==StoreResult::Found&&std::memcmp(&loaded,&state,sizeof(state))==0,
          "matching native read recovers a journal without its completion callback");
    Check(store.Read(root+L"\\ffx_096",file,loaded)==StoreResult::Missing,"a journal never migrates to a different save slot");
    auto currencyChanged=file;currencyChanged[0x3D88]^=1;FfxHooks::RonsoPool::SealSave(currencyChanged);
    Check(MatchesSave(currencyChanged,state),"currency-only control has otherwise identical inventory bytes");
    Check(store.Read(pendingPath,currencyChanged,loaded)==StoreResult::Missing,
          "identical equipment without the exact currency save cannot borrow a prepared extension");
    Check(store.CommitPrepared(pendingPath,file),"completed native save promotes the matching prepared extension");
    Check(store.Read(pendingPath,file,loaded)==StoreResult::Found&&std::memcmp(&loaded,&state,sizeof(state))==0,"journal promotion retains all generator and ability identities");
    auto metadataOnly=state;++metadataOnly.revision;metadataOnly.rng^=0xABCD;
    Check(store.PrepareSave(pendingPath,file,metadataOnly),"a newer logical snapshot may have identical serialized native bytes");
    Check(store.Read(pendingPath,file,loaded)==StoreResult::Found&&std::memcmp(&loaded,&metadataOnly,sizeof(loaded))==0,
          "newer matching prepared metadata survives even when an older committed record exists");
    auto conflicting=metadataOnly;conflicting.rng^=1;
    Check(!store.PrepareSave(pendingPath,file,conflicting),"equal-revision divergent journals are conflicts, not replacements");
    Check(!store.Write(pendingPath,file,state),"an older snapshot cannot overwrite a newer matching journal");
    const auto tornPath=root+L"\\ffx_097";
    Check(store.PrepareSave(tornPath,file,state),"corrupt-journal fixture creates its private record");
    const auto tornLeaf=store.RecordPath(tornPath,file)+L".pending";
    h=CreateFileW(tornLeaf.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(h!=INVALID_HANDLE_VALUE){SetFilePointer(h,8,nullptr,FILE_BEGIN);SetEndOfFile(h);CloseHandle(h);}
    Check(store.Read(tornPath,file,loaded)==StoreResult::Invalid,"truncated pending records fail closed");
    Check(!store.PrepareSave(tornPath,file,state),"a corrupt pending record is not silently overwritten");
    const auto intentPath=root+L"\\ffx_099";unsigned slot=200;
    for(unsigned i=0;i<200;++i){const auto& p=state.pieces[i];
        if(p.id&&p.native[4]<7&&p.native[6]==255&&!(p.native[3]&12)){slot=i;break;}}
    Check(slot<200,"real fixture supplies a regular unequipped transaction subject");
    if(slot<200){
        workshop::Request request{};request.op=workshop::Op::Mode;request.value=1;
        request.slot=static_cast<std::uint16_t>(slot);request.pieceId=state.pieces[slot].id;request.revision=state.revision;
        workshop::Economy economy{};economy.customizeUnlocked=1;std::memcpy(&economy.gil,file.data()+0x3D88,4);
        workshop::Plan plan{};Check(workshop::Preview(state,request,plan,economy)==workshop::Error::Ok,"transaction fixture obtains a valid core plan");
        Check(store.StageTransaction(intentPath,disk,file,state,request,economy,plan),"durable intent records the verified request and native snapshot");
        Check(store.Read(intentPath,file,loaded)==StoreResult::Missing,"unsaved transaction intents never become loadable sidecars");
        auto forged=plan;forged.after.rng^=1;
        Check(!store.StageTransaction(intentPath,disk,file,state,request,economy,forged),"a forged transaction snapshot cannot enter durable storage");
    }
    CheckpointStoreCases(root,file,state);
    CheckpointCrashCases(argv[1],root,file,state);
    std::printf("EquipmentWorkshopStoreRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
