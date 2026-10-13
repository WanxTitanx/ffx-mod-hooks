#include "../hooks/ArcanaStore.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace FfxHooks::Arcana;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}}
std::vector<unsigned char> Legacy(){
    // Independent little-endian fixture: schema 1, revision 5, card 7 worn by
    // Tidus, HP 123/MP 9. CRC32 e1636222 was computed with Python zlib.
    std::vector<unsigned char> b(276,0);
    const unsigned char header[]={0x41,0x52,0x43,0x41,0x4e,0x41,0x30,0x31,1,0,0,0,0x14,1,0,0};
    std::copy(std::begin(header),std::end(header),b.begin());
    std::fill(b.begin()+16,b.begin()+48,0x11);std::fill(b.begin()+48,b.begin()+80,0x22);
    b[80]=5;b[89]=1;b[103]=1;std::fill(b.begin()+174,b.begin()+216,0xff);b[174]=7;b[175]=0;
    b[216]=123;b[244]=9;b[272]=0x22;b[273]=0x62;b[274]=0x63;b[275]=0xe1;
    return b;
}
void RepairCrc(RecordBytes& bytes){
    unsigned crc=~0u;
    for(std::size_t i=0;i+4<bytes.size();++i){
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc&1)?(crc>>1)^0xedb88320u:crc>>1;
    }
    crc=~crc;
    for(unsigned i=0;i<4;++i)bytes[bytes.size()-4+i]=static_cast<unsigned char>(crc>>(8*i));
}
std::vector<unsigned char> Read(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};
}
}
int main(){
    const auto legacy=Legacy();Record record;
    Check(Decode(legacy.data(),legacy.size(),record),"literal deployed record decodes");
    Check(record.state.acquired[7]&&record.state.slots[0][0]==7&&record.state.revision==5&&record.resources.hp[0]==123&&record.resources.mp[0]==9,"legacy equipment and resources survive migration");
    Check(record.presentation.count==0&&record.presentation.legacy&&Presentation::Current(record.presentation).kind==Presentation::Kind::Introduction,"legacy cards introduce without fictitious acquisition notices");
    const auto before=record.state.acquired;
    Award(record.state,36);
    Check(Presentation::ObserveAwards(record.presentation,before,record.state.acquired),"new acquisition after legacy migration queues a real notice");
    RecordBytes bytes{};Record decoded;
    record.objectives={1,2,3,4,5,6,31,3,1,2,3,1};
    Check(Encode(record,bytes)&&bytes.size()==396&&bytes[8]==3,"new record uses explicit version three and bounded size");
    Check(Decode(bytes.data(),bytes.size(),decoded)&&decoded.objectives==record.objectives,"authored objective progress survives the same snapshot as the collection");
    Check(Decode(bytes.data(),bytes.size(),decoded)&&decoded.presentation.count==1&&decoded.presentation.pending[0]==36&&decoded.presentation.legacy,"pending notice and legacy introduction survive a round trip");
    Presentation::Acknowledge(record.presentation,Presentation::Current(record.presentation),Presentation::Navigation::Next);
    Presentation::Acknowledge(record.presentation,Presentation::Current(record.presentation),Presentation::Navigation::Next);
    Check(Encode(record,bytes)&&Decode(bytes.data(),bytes.size(),decoded)&&decoded.presentation.page==1&&decoded.presentation.intro==Presentation::Intro::Reading,"unfinished introduction resumes at the saved page");
    auto bad=bytes;bad[194+kCardCount]=99;RepairCrc(bad);
    Check(!Decode(bad.data(),bad.size(),decoded)&&decoded.presentation.page==1,"valid CRC cannot admit unknown presentation phase or overwrite output");
    bad=bytes;bad[198+kCardCount]=kCardCount+1;RepairCrc(bad);Check(!Decode(bad.data(),bad.size(),decoded),"oversized pending queue rejects even with valid CRC");
    bad=bytes;bad[199+2*kCardCount]=1;RepairCrc(bad);Check(!Decode(bad.data(),bad.size(),decoded),"reserved schema bits reject noncanonical state");
    Check(!Decode(legacy.data(),legacy.size()-1,decoded),"legacy truncation never migrates");
    auto inconsistent=record;inconsistent.presentation={};Check(!Encode(inconsistent,bytes),"owned cards cannot serialize missing presentation provenance");
    const auto dir=std::filesystem::temp_directory_path()/ ("arcana-v2-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);const auto native=dir/"ffx_000";
    const auto legacyPath=Store::Extension(native,".arcana.v1");
    {std::ofstream f(legacyPath,std::ios::binary);f.write(reinterpret_cast<const char*>(legacy.data()),legacy.size());}
    Store store;
    Check(store.Read(native,record.nativeHash,record.packHash,decoded)==StoreCode::Recovered&&decoded.presentation.legacy,"store recovers the legacy suffix as an explicit migration");
    Check(Read(legacyPath)==legacy,"legacy migration is read-only before saving");
    Check(store.Prepare(native,record)&&store.Commit(native,record.nativeHash),"new save commits version three atomically");
    Check(std::filesystem::exists(Store::Extension(native,".arcana.v3"))&&Read(legacyPath)==legacy,"version three never overwrites the deployed legacy sidecar");
    Check(store.Read(native,record.nativeHash,record.packHash,decoded)==StoreCode::Found&&decoded.presentation.page==1&&decoded.state.acquired[36],"new schema takes precedence over older matching legacy bytes");
    {std::ofstream damaged(Store::Extension(native,".arcana.v3"),std::ios::binary);damaged<<"invalid current record";}
    Check(store.Read(native,record.nativeHash,record.packHash,decoded)==StoreCode::Invalid&&decoded.state.acquired[36],"corrupt migrated state cannot silently fall back to an older legacy collection");
    std::filesystem::remove_all(dir);
    std::printf("ArcanaStoreMigrationRt0: %u/%u checks passed\n",checks-failures,checks);
    return failures?1:0;
}
