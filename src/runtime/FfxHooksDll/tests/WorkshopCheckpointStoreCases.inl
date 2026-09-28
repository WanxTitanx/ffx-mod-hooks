// Jarvis-HOOK: real private Win32 storage, not a mock of atomic publication.
static void CheckpointStoreCases(const std::wstring& root,const SaveImage& base,const workshop::State& original){
    namespace Events=FfxHooks::NativeSaveEvents;
    namespace Pool=FfxHooks::RonsoPool;
    Store store;Check(store.Initialize(root,false),"checkpoint uses the existing verified directory");
    const auto path=root+L"\\ffx_080";Hash anchor{},head{};Fingerprint(base.data(),base.size(),anchor);
    auto seed=original;Check(store.PinLoaded(path,anchor,base,seed),"checkpoint control pins the original disk version");
    auto paid=original;paid.revision+=10;paid.rng^=0xABCDEu;++paid.rolls;
    SaveImage accepted=base;std::uint32_t gil=0;std::memcpy(&gil,base.data()+0x3D88,4);
    Check(gil>2,"checkpoint currency fixture has a positive debit");
    const auto paidGil=gil-1;std::memcpy(accepted.data()+0x3D88,&paidGil,4);Pool::SealSave(accepted);
    Check(store.PublishCheckpoint(path,anchor,accepted,{},paid,head),"accepted native image and all logical/RNG bytes publish as one checkpoint");
    SaveImage selected{};Events::CheckpointSelection selection{};workshop::State restored{};
    Check(store.SelectCheckpoint(path,base,selected,selection)==StoreResult::Found&&selection.selected&&selected==accepted,
          "old native disk version selects the paid image including its actual Gil");
    const auto firstProof=selection.proof;
    Check(store.ReadCheckpointLoaded(path,anchor,selection.proof,selected,restored)==StoreResult::Found&&
          std::memcmp(&restored,&paid,sizeof(paid))==0,"checkpoint read restores the exact accepted generator and identities");
    Store reopened;Check(reopened.Initialize(root,false)&&
        reopened.SelectCheckpoint(path,base,selected,selection)==StoreResult::Found&&selected==accepted,
        "a fresh store instance recovers without any in-memory pending transaction");
    Check(store.SelectCheckpoint(root+L"\\ffx_081",base,selected,selection)==StoreResult::Missing,
          "the same native bytes in another save path never inherit a paid checkpoint");
    auto foreign=base;foreign.back()^=1;
    Check(store.SelectCheckpoint(path,foreign,selected,selection)==StoreResult::Invalid,
          "a different full disk image cannot borrow a checkpoint even outside native CRC coverage");
    auto next=paid;++next.revision;next.rng^=77;++next.rolls;
    auto nextImage=accepted;const auto nextGil=gil-2;std::memcpy(nextImage.data()+0x3D88,&nextGil,4);Pool::SealSave(nextImage);
    for(int point=0;point<4;++point){
        Store::FailCheckpointWriteForTests(point);
        Check(!store.PublishCheckpoint(path,anchor,nextImage,{},next,head),"an injected pre-commit I/O boundary rejects the new result");
        Check(reopened.SelectCheckpoint(path,base,selected,selection)==StoreResult::Found&&selected==accepted&&selection.proof==firstProof,
              "failed write/flush/rename preserves the previous complete checkpoint, never a torn pair");
    }
    const auto ordinaryLeaf=store.RecordPath(path,base);
    const auto checkpointLeaf=ordinaryLeaf.substr(0,ordinaryLeaf.find_last_of(L"\\/")+1+32)+L".checkpoint.bin";
    HANDLE lease=CreateFileW((checkpointLeaf+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    Check(lease!=INVALID_HANDLE_VALUE&&!store.PublishCheckpoint(path,anchor,nextImage,{},next,head),
          "another process's exclusive save lease prevents competing commits without waiting");
    if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);
    Check(store.PublishCheckpoint(path,anchor,nextImage,{},next,head)&&store.PublishCheckpoint(path,anchor,nextImage,{},next,head),
          "one successor publishes and an identical retry is idempotent");
    Check(!store.PublishCheckpoint(path,anchor,accepted,{},paid,head),"an older paid checkpoint cannot replace its successor");
    auto conflict=next;conflict.rng^=1;
    Check(!store.PublishCheckpoint(path,anchor,nextImage,{},conflict,head),"equal-revision divergent RNG is rejected");
    auto staleHead=firstProof;auto ahead=next;ahead.revision+=100;ahead.rng^=7;
    Check(!store.PublishCheckpoint(path,anchor,nextImage,{},ahead,staleHead)&&staleHead==firstProof,
          "a stale process cannot overwrite a newer paid outcome by accumulating a larger local revision");
    Check(store.ReadCheckpointLoaded(path,anchor,firstProof,accepted,restored)==StoreResult::Invalid,
          "a read selection proof expires when a newer transaction is accepted");
    Check(store.SelectCheckpoint(path,base,selected,selection)==StoreResult::Found&&selected==nextImage,
          "subsequent reloads select only the newest complete paid outcome");
    auto wrongLoaded=nextImage;wrongLoaded[0x3D88]^=1;Pool::SealSave(wrongLoaded);
    Check(store.ReadCheckpointLoaded(path,anchor,selection.proof,wrongLoaded,restored)==StoreResult::Invalid,
          "changing only currency cannot keep the checkpoint's identities and RNG");
    auto saved=next;++saved.revision;
    Check(store.PrepareSave(path,nextImage,saved)&&store.SelectCheckpoint(path,nextImage,selected,selection)==StoreResult::Missing,
          "a verified later native save supersedes private recovery even if completion was lost");
    const auto poolPath=root+L"\\ffx_082";auto pooled=accepted;
    pooled[Pool::kSaveCharge]=175;pooled[Pool::kSaveMaximum]=200;Pool::SealSave(pooled);
    Events::CheckpointOwnership ownership{1,100,175,200};
    Hash poolHead{};
    Check(store.PublishCheckpoint(poolPath,anchor,pooled,ownership,paid,poolHead),"checkpoint includes the Ronso original maximum and full dormant charge");
    Check(store.SelectCheckpoint(poolPath,base,selected,selection)==StoreResult::Found&&selection.pool.originalMax==100,
          "Ronso ownership is selected before its load normalization");
    Pool::SaveSession session{};SaveImage normalized{};const Pool::SavedOwner owner{100,175,200};
    Check(Pool::LoadPool(false,pooled,&owner,0,&session,&normalized)==Pool::SaveDecision::Converted&&
          normalized[Pool::kSaveCharge]==100&&session.dormant==75&&
          store.ReadCheckpointLoaded(poolPath,anchor,selection.proof,normalized,restored)==StoreResult::Found,
          "Ronso OFF recovers the paid state while retaining exactly 75 dormant charge");
    normalized[Pool::kSaveCharge]=99;Pool::SealSave(normalized);
    Check(store.ReadCheckpointLoaded(poolPath,anchor,selection.proof,normalized,restored)==StoreResult::Invalid,
          "an arbitrary charge/CRC rewrite is not a verified Ronso transformation");
    ownership.present=2;
    Check(!store.PublishCheckpoint(poolPath,anchor,pooled,ownership,next,poolHead),"serialized ownership flags have a bounded representation");
    HANDLE corrupt=CreateFileW(checkpointLeaf.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    Check(corrupt!=INVALID_HANDLE_VALUE,"corruption fixture opens only its private checkpoint");
    if(corrupt!=INVALID_HANDLE_VALUE){SetFilePointer(corrupt,17,nullptr,FILE_BEGIN);SetEndOfFile(corrupt);CloseHandle(corrupt);}
    Check(store.SelectCheckpoint(path,base,selected,selection)==StoreResult::Invalid&&
          !store.PublishCheckpoint(path,anchor,nextImage,{},next,head),"a corrupt checkpoint never falls back to a refundable import or silent replacement");
}
