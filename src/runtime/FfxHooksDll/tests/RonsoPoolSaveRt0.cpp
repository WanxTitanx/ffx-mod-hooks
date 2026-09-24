#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <fstream>
using namespace FfxHooks::RonsoPool;
namespace {
int checks=0,failures=0;
void Expect(bool ok,const char* why) {
    ++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}
}
void OnlyPoolAndChecksum(const SaveImage& a,const SaveImage& b) {
    bool good=true;
    for(size_t i=0;i<a.size();++i)if(a[i]!=b[i] && i!=26 && i!=27 &&
        i!=kSaveCharge && i!=kSaveMaximum && !(i>=25844 && i<25848))good=false;
    Expect(good,"unrelated progress, inventory, skills and footer are byte-identical");
}
void RoundTrips(const SaveImage& original) {
    Expect(IsValidSave(original)&&SaveChecksum(original)==0x8FC6,"real autosave CRC matches its known native checksum");
    SaveSession session{};SaveImage loaded{};
    Expect(LoadPool(true,original,nullptr,23,&session,&loaded)==SaveDecision::Converted,
           "old vanilla save enters a capacity200 session");
    Expect(loaded[kSaveCharge]==100&&loaded[kSaveMaximum]==200&&session.originalMax==100,
           "increasing capacity never creates charge");
    OnlyPoolAndChecksum(original,loaded);Expect(IsValidSave(loaded),"converted load remains native-CRC valid");
    for(uint16_t charge=0;charge<=200;++charge) {
        SaveImage playing=loaded;playing[kSaveCharge]=static_cast<uint8_t>(charge);SealSave(playing);
        SaveImage disk{};SavedOwner metadata{};bool need=false;
        const auto saved=SavePool(true,session,playing,&disk,&metadata,&need);
        Expect(saved!=SaveDecision::Invalid&&need&&metadata.originalMax==100,
               "active save carries original-capacity ownership");
        Expect(disk[kSaveCharge]==charge&&disk[kSaveMaximum]==200&&IsValidSave(disk),
               "native coherent save preserves the entire current balance");
        SaveSession reopened{};SaveImage again{};
        Expect(LoadPool(true,disk,&metadata,23,&reopened,&again)!=SaveDecision::Invalid&&
               again[kSaveCharge]==charge&&again[kSaveMaximum]==200,
               "full balance survives process close and reopen");
        SaveSession off{};SaveImage native{};
        Expect(LoadPool(false,disk,&metadata,23,&off,&native)==SaveDecision::Converted&&
               native[kSaveMaximum]==100&&native[kSaveCharge]==(charge>100?100:charge),
               "OFF restores the owned native capacity and coherent current value");
        Expect(off.dormant==(charge>100?charge-100:0)&&IsValidSave(native),
               "OFF retains surplus separately instead of discarding it");
        SaveImage offDisk{};SavedOwner offMeta{};bool offNeed=false;
        SavePool(false,off,native,&offDisk,&offMeta,&offNeed);
        SaveSession onAgain{};SaveImage resumed{};
        LoadPool(true,offDisk,offNeed?&offMeta:nullptr,23,&onAgain,&resumed);
        Expect(resumed[kSaveCharge]==charge,"save while OFF does not erase dormant surplus");
        OnlyPoolAndChecksum(playing,offDisk);
    }
}
void RefusalsAndTransitions(const SaveImage& source) {
    auto corrupt=source;corrupt[1234]^=1;SaveSession old{100,50,true,164};SaveImage out=source;
    Expect(LoadPool(true,corrupt,nullptr,23,&old,&out)==SaveDecision::Invalid,
           "corrupt native save is never repaired into an accepted one");
    Expect(!old.owned&&old.dormant==0,"failed load cannot retain another slot's dormant balance");
    SaveImage modded=source;modded[kSaveCharge]=150;modded[kSaveMaximum]=200;SealSave(modded);
    SavedOwner forged{100,151,200};
    Expect(LoadPool(false,modded,&forged,23,&old,&out)==SaveDecision::OwnershipConflict&&out==modded,
           "metadata/value mismatch never normalizes a foreign save");
    Expect(LoadPool(false,modded,nullptr,23,&old,&out)==SaveDecision::Native&&out==modded,
           "missing ownership preserves a foreign custom-capacity save");
    SavedOwner owned{100,150,200};LoadPool(false,modded,&owned,23,&old,&out);
    ObserveScene(&old,23);Expect(old.dormant==50,"repeated title sample after load does not erase loaded surplus");
    ObserveScene(&old,164);ObserveScene(&old,200);Expect(old.dormant==50,"ordinary map changes preserve balance");
    ObserveScene(&old,23);Expect(!old.owned&&old.dormant==0,"return to title clears stale balance before new game");
    LoadPool(false,modded,&owned,23,&old,&out);out[kSaveCharge]=40;SealSave(out);
    SaveImage paid{};SavedOwner paidMeta{};bool need=false;
    SavePool(false,old,out,&paid,&paidMeta,&need);
    Expect(paid[kSaveCharge]==90&&paid[kSaveMaximum]==200,
           "spending60 of native100 while OFF preserves dormant50, total90");
    auto tooLarge=source;tooLarge[kSaveMaximum]=255;SealSave(tooLarge);
    Expect(LoadPool(true,tooLarge,nullptr,23,&old,&out)==SaveDecision::OwnershipConflict,
           "foreign capacity above200 is not silently reduced");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    SaveImage image{};std::ifstream stream(argv[1],std::ios::binary);
    if(!stream.read(reinterpret_cast<char*>(image.data()),image.size()))return 2;
    RoundTrips(image);RefusalsAndTransitions(image);
    std::printf("RonsoPoolSaveRt0: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
