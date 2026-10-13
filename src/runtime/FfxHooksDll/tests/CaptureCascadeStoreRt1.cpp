#include "../hooks/CaptureCascadeStore.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace FfxHooks::CaptureCascade;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
Record Make(unsigned seed){Record r;r.nativeHash.fill(static_cast<unsigned char>(seed));r.state=Adopt(0);Stamp stamp;stamp.utc=1791586800;std::strcpy(stamp.build.data(),"Jarvis-HOOK-fixture");ObserveVictories(r.state,1,stamp);ObserveRegion(r.state,Aeon::Shiva,true);return r;}
}
int main(){
    auto record=Make(1);RecordBytes encoded{};Check(Encode(record,encoded),"save record encodes with pending independent tutorials");
    Record decoded;Check(Decode(encoded.data(),encoded.size(),decoded)&&decoded.state.defeated==1&&decoded.state.sin.queued&&decoded.state.cascade.queued,"record round trip preserves progress and notices");
    for(unsigned i=0;i<encoded.size();++i){auto bad=encoded;bad[i]^=1;Check(!Decode(bad.data(),bad.size(),decoded),"one-byte corruption is rejected");}
    Check(!Decode(encoded.data(),encoded.size()-1,decoded),"truncated record is rejected");
    auto invalid=record;invalid.nativeHash={};Check(!Encode(invalid,encoded),"unbound native image cannot be persisted");
    invalid=record;invalid.state.sin.page=3;Check(!Encode(invalid,encoded),"out-of-range tutorial page is rejected");
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("capture-cascade-store-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);const auto native=root/"ffx_001";Store store;
    Check(store.Read(native,record.nativeHash,decoded)==StoreCode::Missing,"new save has no invented sidecar");
    Check(store.Prepare(native,record),"sidecar is prepared before verified native write");
    Check(store.Read(native,record.nativeHash,decoded)==StoreCode::Recovered,"exact pending native hash is recoverable after an interrupted commit");
    Hash other{};other.fill(2);Check(!store.Commit(native,other),"another native image cannot commit the pending record");
    Check(store.Commit(native,record.nativeHash)&&store.Read(native,record.nativeHash,decoded)==StoreCode::Found,"verified native image commits its own state");
    auto newer=Make(2);Acknowledge(newer.state,Current(newer.state,Aeon::Valefor));
    Check(store.Prepare(native,newer)&&store.Abort(native,newer),"aborted native save removes only its matching staged sidecar");
    Check(store.Read(native,record.nativeHash,decoded)==StoreCode::Found,"aborted write preserves prior saved progress");
    Check(store.Prepare(native,newer)&&!store.Abort(native,record),"stale abort cannot remove another write's pending record");
    Check(store.Commit(native,newer.nativeHash),"next verified native image is committed");
    Check(store.Read(native,record.nativeHash,decoded)==StoreCode::Recovered,"retained previous state matches the old native snapshot");
    Check(store.Read(root/"ffx_002",newer.nativeHash,decoded)==StoreCode::Missing,"another slot never inherits process-global progress");
    Hash foreign{};foreign.fill(3);Check(store.Read(native,foreign,decoded)==StoreCode::Foreign,"same path with unrelated native data does not reset or inherit state");
    const auto corrupt=Store::Extension(native,".cascade.v1");
    {std::ofstream stream(corrupt,std::ios::binary|std::ios::trunc);stream<<"foreign data";}
    Check(store.Prepare(native,record)&&!store.Commit(native,record.nativeHash),"foreign final file is preserved instead of overwritten");
    std::filesystem::remove_all(root);
    std::printf("Capture Cascade store RT1: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
