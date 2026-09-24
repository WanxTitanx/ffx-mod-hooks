#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../hooks/EquipmentWorkshopStore.h"
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace FfxHooks::EquipmentWorkshop;
static unsigned checks,failures;
static void Check(bool b,const char* m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}
int main(int argc,char** argv){
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
    std::printf("EquipmentWorkshopStoreRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
