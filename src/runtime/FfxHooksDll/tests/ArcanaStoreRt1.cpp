#include "../hooks/ArcanaStore.h"
#include "../hooks/ArcanaCatalog.generated.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <chrono>
using namespace FfxHooks::Arcana;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
static Hash Digest(unsigned value){Hash h{};h.fill(static_cast<unsigned char>(value));return h;}
static bool Same(const Record& a,const Record& b){return a.nativeHash==b.nativeHash&&a.packHash==b.packHash&&a.state.revision==b.state.revision&&a.state.mode==b.state.mode&&a.state.acquired==b.state.acquired&&a.state.slots==b.state.slots&&a.resources.valid==b.resources.valid&&a.resources.hp==b.resources.hp&&a.resources.mp==b.resources.mp;}
int main(){
    Record first;first.nativeHash=Digest(1);first.packHash=Digest(2);AwardAll(first.state,0);Equip(first.state,1,0,0,0);
    first.resources.valid=1;first.resources.hp[0]=1234;first.resources.mp[0]=56;
    RecordBytes bytes{};Record read;
    const bool encoded=Encode(first,bytes);
    Check(encoded,"valid Arcana state can be encoded without native save changes");
    Check(Decode(bytes.data(),bytes.size(),read)&&Same(first,read),"record round trip preserves ownership, identity and current resources");
    auto invalid=bytes;invalid[3]^=1;
    const auto before=read;
    Check(!Decode(invalid.data(),invalid.size(),read)&&Same(before,read),"tampered record rejects without publishing partial state");
    Check(!Decode(bytes.data(),bytes.size()-1,read),"truncated record rejected");
    std::array<unsigned char,kRecordBytes+1> oversized{};
    Check(!Decode(oversized.data(),oversized.size(),read),"oversized record rejected");
    auto duplicate=first;duplicate.state.slots[1][0]=0;
    Check(!Encode(duplicate,bytes),"duplicated ownership cannot be serialized");
    const auto nonce=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto dir=std::filesystem::temp_directory_path()/("arcana-store-rt1-"+std::to_string(nonce));
    std::filesystem::create_directories(dir);
    const auto native=dir/"ffx_000";{std::ofstream f(native,std::ios::binary);f<<"untouched native fixture";}
    Store store;
    Check(store.Read(native,first.nativeHash,first.packHash,read)==StoreCode::Missing,"missing extension preserves native-only save");
    Check(store.Prepare(native,first),"prepare creates a bounded pending record");
    Check(store.Read(native,Digest(9),first.packHash,read)==StoreCode::Foreign,"pending transaction cannot attach to different native bytes");
    Check(store.Read(native,first.nativeHash,first.packHash,read)==StoreCode::Recovered&&Same(first,read),"matching native write can recover after crash before extension commit");
    Check(!store.Commit(native,Digest(9)),"partial or different native write cannot commit extension");
    Check(store.Commit(native,first.nativeHash),"completed native write commits matching pending record");
    Check(store.Read(native,first.nativeHash,first.packHash,read)==StoreCode::Found&&Same(first,read),"committed extension reopens by exact native identity");
    Check(store.Read(native,first.nativeHash,Digest(3),read)==StoreCode::Foreign,"different typed pack cannot reinterpret card state");
    auto second=first;second.nativeHash=Digest(4);Equip(second.state,second.state.revision,1,0,0,true);
    Check(store.Prepare(native,second),"next generation can prepare while prior commit stays recoverable");
    Check(store.Read(native,first.nativeHash,first.packHash,read)==StoreCode::Found&&Same(first,read),"crash before native write keeps old committed pair");
    Check(store.Read(native,second.nativeHash,second.packHash,read)==StoreCode::Recovered&&Same(second,read),"crash after new native write recovers new pending pair");
    Check(store.Commit(native,second.nativeHash),"new pair can finish commit");
    const auto previous=store.Read(native,first.nativeHash,first.packHash,read);
    Check(previous==StoreCode::Recovered&&Same(first,read),"prior native bytes still match preserved previous extension");
    const auto saveAs=dir/"ffx_001";
    Check(store.Prepare(saveAs,second)&&store.Commit(saveAs,second.nativeHash),"save-as keeps an independent native/extension pair");
    Check(store.Prepare(saveAs,first)&&!store.Abort(saveAs,second),"failed older transaction cannot discard another prepared record");
    Check(store.Abort(saveAs,first)&&store.Abort(saveAs,first),"failed native write discards only its own pending extension idempotently");
    Check(store.Read(saveAs,second.nativeHash,second.packHash,read)==StoreCode::Found&&Same(second,read),"aborting a failed write preserves the last committed pair");
    const Hash known[]={second.packHash};bool migrated=false;const auto newPack=Digest(8);
    Check(store.ReadCompatible(saveAs,second.nativeHash,newPack,known,1,read,migrated)==StoreCode::Recovered&&migrated&&
          read.packHash==newPack&&read.state.slots==second.state.slots&&read.state.acquired==second.state.acquired&&read.resources.hp==second.resources.hp,
          "a known balance-only revision preserves ownership and current resources");
    Check(store.Read(saveAs,second.nativeHash,second.packHash,read)==StoreCode::Found&&Same(second,read),"loading a migrated pack does not rewrite the extension prematurely");
    const Hash foreign[]={Digest(7)};
    Check(store.ReadCompatible(saveAs,second.nativeHash,newPack,foreign,1,read,migrated)==StoreCode::Foreign&&!migrated,"unknown pack identities remain rejected");
    Check(store.ReadCompatible(saveAs,Digest(9),newPack,known,1,read,migrated)==StoreCode::Foreign&&!migrated,"balance migration never bypasses native-save identity");
    auto v4=first;v4.state.slots[0]={{18,12,kEmpty}};
    v4.packHash={0xc6,0xfe,0x69,0xe9,0xf9,0x6c,0xf7,0x76,0xa9,0x43,0xeb,0xeb,0xa7,0x25,0xed,0x65,0xdc,0x18,0x72,0xcc,0x6e,0x8e,0x0e,0x15,0x39,0x96,0x3c,0x16,0x4d,0xf2,0x45,0x3e};
    const auto v4Path=dir/"ffx_v4";
    Check(store.Prepare(v4Path,v4)&&store.Commit(v4Path,v4.nativeHash),"prior deployed v4 catalog has a real stored extension fixture");
    Hash currentPack{};std::copy(std::begin(kPackHash),std::end(kPackHash),currentPack.begin());
    std::array<Hash,std::size(kCompatiblePackHashes)> compatibility{};
    for(unsigned i=0;i<compatibility.size();++i)std::copy(std::begin(kCompatiblePackHashes[i]),std::end(kCompatiblePackHashes[i]),compatibility[i].begin());
    Check(store.ReadCompatible(v4Path,v4.nativeHash,currentPack,compatibility.data(),compatibility.size(),read,migrated)==StoreCode::Recovered&&
          migrated&&read.state.acquired==v4.state.acquired&&read.state.slots==v4.state.slots&&read.resources.hp==v4.resources.hp,
          "the deployed v4 catalog migrates to elemental strikes without losing cards, loadouts or current resources");
    Check(store.Read(v4Path,v4.nativeHash,v4.packHash,read)==StoreCode::Found&&Same(v4,read),"v4 migration remains read-only until a real save commits");
    std::ifstream f(native,std::ios::binary);std::string contents((std::istreambuf_iterator<char>(f)),{});f.close();
    Check(contents=="untouched native fixture","extension store never opens native save for writing");
    const auto backup=Store::Extension(native,".arcana.previous.v1");
    std::ifstream backupBefore(backup,std::ios::binary);const std::string protectedBackup((std::istreambuf_iterator<char>(backupBefore)),{});backupBefore.close();
    {std::ofstream damaged(Store::Extension(native,".arcana.v1"),std::ios::binary);std::string junk(kRecordBytes,'x');damaged.write(junk.data(),static_cast<std::streamsize>(junk.size()));}
    Check(store.Prepare(native,second)&&!store.Commit(native,second.nativeHash),"corrupt committed extension is not overwritten silently");
    std::ifstream backupAfter(backup,std::ios::binary);const std::string preserved((std::istreambuf_iterator<char>(backupAfter)),{});backupAfter.close();
    Check(preserved==protectedBackup,"corrupt current record cannot replace a valid recovery copy");
    std::filesystem::remove_all(dir);
    std::printf("ArcanaStoreRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
