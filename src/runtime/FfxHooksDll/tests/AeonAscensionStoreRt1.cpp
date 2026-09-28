// Jarvis-HOOK: immutable receipt snapshots join the existing paid checkpoint's
// commit point. An orphan prepared receipt must never select an unpaid future.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopStore.h"
#include "../hooks/AeonAscensionCore.h"
#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#ifdef FFXHOOKS_ASCENSION_RECEIPTS_V1
namespace W=FfxHooks::EquipmentWorkshop;
namespace A=FfxHooks::AeonAscension;
namespace N=FfxHooks::NativeSaveEvents;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static void Word(unsigned char* p,unsigned value){p[0]=static_cast<unsigned char>(value);p[1]=static_cast<unsigned char>(value>>8);}
static void Seal(W::SaveImage& image,const workshop::State& state,unsigned gil){
    for(unsigned slot=0;slot<200;++slot)std::memcpy(image.data()+0x44DC+22*slot,state.pieces[slot].native,22);
    for(unsigned item=0;item<112;++item)image[0x410C+item]=static_cast<unsigned char>(state.items[item]);
    std::memcpy(image.data()+0x3D88,&gil,4);FfxHooks::RonsoPool::SealSave(image);
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    W::SaveImage before{};std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(before.data()),before.size()))return 2;
    const std::filesystem::path root(argv[2]);std::filesystem::create_directories(root);
    const auto path=(root/L"ffx_000").wstring(),other=(root/L"ffx_001").wstring();
    std::memset(before.data()+0x44DC,0,4400);
    for(unsigned i=0;i<256;++i){Word(before.data()+0x3F0C+2*i,i<112?0x2000+i:0xFFFF);before[0x410C+i]=i<112?255:0;}
    auto* gear=before.data()+0x44DC;gear[2]=1;gear[3]=4;gear[4]=8;gear[6]=8;gear[11]=4;
    for(unsigned i=0;i<4;++i)Word(gear+14+2*i,i?workshop::Empty:0x807B);
    FfxHooks::RonsoPool::SealSave(before);
    workshop::State state{};Check(W::ImportSave(before,0xAEE1,state),"receipt fixture imports native inventory");
    workshop::Economy economy{};economy.gil=50000000;economy.customizeUnlocked=1;
    economy.aeons.obtained=1u<<8;economy.aeons.crests=2;economy.aeons.gear[0]=0;
    Seal(before,state,economy.gil);W::Hash anchor{};W::Fingerprint(before.data(),before.size(),anchor);
    W::Store store;Check(store.Initialize((root/L"metadata").wstring(),true),"private metadata directory initializes");
    Check(store.Write(path,before,state),"old v1 inventory records remain writable without receipts");
    A::Ledger empty{},read{};A::SaveId save{};
    Check(store.ReadReceipts(path,state,read)==W::StoreResult::Missing&&!read.count,"an old or absent receipt file grants no permission");
    Check(store.SaveIdentity(path,anchor,save)&&A::Nonzero(save),"initial receipt identity binds the native save path and anchor");
    A::Mapping mapping{};mapping.enabled=true;mapping.proof=123;
    A::Request request{};request.slot=0;request.position=1;request.effect=1;request.pieceId=state.pieces[0].id;request.revision=state.revision;
    A::Plan plan{};Check(A::Preview(state,empty,save,mapping,economy,request,plan)==workshop::Error::Ok,"the exact paid upgrade is reviewable");
    W::SaveImage paid=before;Seal(paid,plan.inventory.after,economy.gil-plan.inventory.gilDebit);
    Check(store.StageAscensionTransaction(path,anchor,before,state,empty,save,mapping,economy,request,plan),"the original inventory costs and receipt are durably staged together");
    Check(store.PrepareReceipts(path,plan.inventory.after,plan.receipts),"receipt preparation creates an immutable after-state attachment");
    W::SaveImage selected{};N::CheckpointSelection selection{};
    Check(store.SelectCheckpoint(path,before,selected,selection)==W::StoreResult::Missing,"prepared receipt alone cannot activate a future paid state");
    W::Hash head{};N::CheckpointOwnership pool{};
    W::Store::FailCheckpointWriteForTests(3);
    Check(!store.PublishCheckpoint(path,anchor,paid,pool,plan.inventory.after,head,&plan.receipts)&&head==W::Hash{},"failure before the existing rename leaves the old checkpoint owner intact");
    Check(store.SelectCheckpoint(path,before,selected,selection)==W::StoreResult::Missing,"failed publication cannot turn an orphan receipt into a free purchase");
    Check(store.PublishCheckpoint(path,anchor,paid,pool,plan.inventory.after,head,&plan.receipts),"one paid checkpoint commit publishes both inventory and receipt eligibility");
    Check(store.SelectCheckpoint(path,before,selected,selection)==W::StoreResult::Found&&selected==paid,"recovery chooses the fully paid image rather than refunding its cost");
    workshop::State recovered{};
    Check(store.ReadCheckpointLoaded(path,anchor,selection.proof,selected,recovered)==W::StoreResult::Found&&
          std::memcmp(&recovered,&plan.inventory.after,sizeof(recovered))==0,"checkpoint recovery retains the exact paid ability identity");
    Check(store.ReadReceipts(path,recovered,read)==W::StoreResult::Found&&A::Authorized(read,save,recovered.pieces[0],1,1,mapping),"the matching recovered state admits its existing receipt");
    Check(store.PublishCheckpoint(path,anchor,paid,pool,plan.inventory.after,head,&plan.receipts),"same durable checkpoint is idempotent");
    auto forged=plan.receipts;forged.entries[0].abilityId++;
    Check(!store.PrepareReceipts(path,plan.inventory.after,forged),"a mismatched receipt cannot replace the immutable state attachment");
    Check(store.ReadReceipts(other,recovered,read)==W::StoreResult::Missing,"another slot cannot locate the source receipt by gear ID");
    const auto receiptPath=store.ReceiptPath(path,recovered),otherPath=store.ReceiptPath(other,recovered);
    Check(CopyFileW(receiptPath.c_str(),otherPath.c_str(),TRUE)!=FALSE,"private cross-slot corruption fixture is created");
    Check(store.ReadReceipts(other,recovered,read)==W::StoreResult::Invalid,"a transplanted receipt fails its full path identity");
    std::ifstream file(std::filesystem::path(receiptPath),std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(file)),{});file.close();
    auto damaged=original;damaged.back()^=1;
    {std::ofstream out(std::filesystem::path(receiptPath),std::ios::binary|std::ios::trunc);out.write(damaged.data(),damaged.size());}
    Check(store.ReadReceipts(path,recovered,read)==W::StoreResult::Invalid,"corrupt receipt bytes fail integrity admission");
    {std::ofstream out(std::filesystem::path(receiptPath),std::ios::binary|std::ios::trunc);out.write(original.data(),original.size());}
    auto saved=recovered;++saved.revision;--saved.items[0];auto ordinary=paid;Seal(ordinary,saved,economy.gil-plan.inventory.gilDebit);
    Check(store.PrepareSave(path,ordinary,saved,&plan.receipts),"ordinary native save stages receipts before its matching inventory journal");
    Check(store.Read(path,ordinary,recovered)==W::StoreResult::Found&&store.ReadReceipts(path,recovered,read)==W::StoreResult::Found,
          "an interrupted ordinary save recovers the matching prepared state and receipts");
    Check(store.CommitPrepared(path,ordinary),"ordinary save completion retains the staged receipt attachment");
    Check(store.Write(other,ordinary,saved,&plan.receipts),"an observed Save As can create a new path-bound copy of already paid state");
    Check(store.ReadReceipts(other,saved,read)==W::StoreResult::Found&&A::Authorized(read,save,saved.pieces[0],1,1,mapping),"Save As provenance is distinct from transplanting a receipt file");
    A::Request remove=request;remove.revision=saved.revision;remove.remove=true;A::Plan removal{};
    Check(A::Preview(saved,plan.receipts,save,mapping,economy,remove,removal)==workshop::Error::Ok,"dedicated removal creates a receipt-free after-state");
    auto removed=ordinary;Seal(removed,removal.inventory.after,economy.gil-plan.inventory.gilDebit);
    Check(store.PublishCheckpoint(path,anchor,removed,pool,removal.inventory.after,head,&removal.receipts),"removal revokes permission at the same checkpoint commit boundary");
    Check(store.SelectCheckpoint(path,before,selected,selection)==W::StoreResult::Found&&selected==removed,"old disk save cannot revive a removed upgrade or refund its purchase");
    std::printf("AEON_ASCENSION_STORE_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production Ascension receipt store API is missing");return 1;}
#endif
