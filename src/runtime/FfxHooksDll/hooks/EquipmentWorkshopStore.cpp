#include "EquipmentWorkshopStore.h"
#include "RonsoPoolStore.h"
#include "RonsoPoolSave.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <vector>

namespace FfxHooks::EquipmentWorkshop {
namespace {
constexpr unsigned char magic[8]={'F','F','X','W','K','S','0','1'};
#pragma pack(push,1)
struct Record {unsigned char magic[8];Hash pathHash,imageHash,stateHash;workshop::State state;};
struct PreparedSave {Record record;SaveImage image;};
struct TransactionBody {Hash anchor;SaveImage before;workshop::State state;workshop::Request request;workshop::Economy economy;workshop::Plan plan;};
struct TransactionIntent {unsigned char magic[8];Hash pathHash,bodyHash;TransactionBody body;};
struct CheckpointBody {Hash anchor;SaveImage image;NativeSaveEvents::CheckpointOwnership pool;workshop::State state;};
struct CheckpointRecord {unsigned char magic[8];Hash pathHash,bodyHash;CheckpointBody body;};
struct ReceiptRecord {unsigned char magic[8];Hash pathHash,stateHash,ledgerHash;AeonAscension::Ledger ledger;};
struct AscensionIntentBody {
    Hash anchor;SaveImage before;workshop::State state;AeonAscension::Ledger owned;
    AeonAscension::SaveId save;AeonAscension::Mapping mapping;workshop::Economy economy;
    AeonAscension::Request request;AeonAscension::Plan plan;
};
struct AscensionIntent {unsigned char magic[8];Hash pathHash,bodyHash;AscensionIntentBody body;};
#pragma pack(pop)
std::atomic<unsigned> sequence{1};
#ifdef FFXHOOKS_TESTING
std::atomic<int> checkpointWriteFailure{-1};
std::atomic<int> checkpointWriteCrash{-1};
#endif
void CheckpointCrashBoundary(bool checkpoint,int point){
#ifdef FFXHOOKS_TESTING
    if(checkpoint&&checkpointWriteCrash.compare_exchange_strong(point,-1)){
        // Test-only abrupt death: no C++ unwinding, close, cleanup or flush.
        TerminateProcess(GetCurrentProcess(),238);ExitProcess(238);
    }
#else
    (void)checkpoint;(void)point;
#endif
}
bool CheckpointBoundary(bool checkpoint,int point){
    CheckpointCrashBoundary(checkpoint,point);
#ifdef FFXHOOKS_TESTING
    if(checkpoint&&checkpointWriteFailure.compare_exchange_strong(point,-1))return false;
#else
    (void)checkpoint;(void)point;
#endif
    return true;
}
bool Digest(const void* data,std::size_t size,Hash& out){
    if(!data || size>UINT32_MAX)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    DWORD objectSize=0,returned=0;
    bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&returned,0)>=0 && objectSize>0 && objectSize<=65536;
    std::vector<unsigned char> object(ok?objectSize:0);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(size),0)>=0 && BCryptFinishHash(hash,out.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
bool Keys(const std::wstring& path,const SaveImage& image,Hash& pathHash,Hash& imageHash){
    if(!RonsoPool::OwnerStore::IsSavePath(path)||path.size()>4096)return false;
    std::wstring normalized(path.size(),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path.data(),static_cast<int>(path.size()),normalized.data(),static_cast<int>(normalized.size()),nullptr,nullptr,0))return false;
    return Digest(normalized.data(),normalized.size()*sizeof(wchar_t),pathHash)&&Digest(image.data(),image.size(),imageHash);
}
std::wstring Hex(const Hash& hash){
    static constexpr wchar_t digits[]=L"0123456789abcdef";std::wstring out;
    for(unsigned i=0;i<16;++i){out.push_back(digits[hash[i]>>4]);out.push_back(digits[hash[i]&15]);}return out;
}
bool MaterialCounts(const SaveImage& image,std::array<std::uint16_t,112>& out){
    std::array<bool,112> seen{};out.fill(0);
    for(unsigned slot=0;slot<256;++slot){const unsigned at=0x3F0C+2*slot;
        const unsigned id=image[at]|(unsigned(image[at+1])<<8);
        if(id>=0x2000 && id<0x2070){const auto item=id-0x2000;if(seen[item])return false;seen[item]=true;out[item]=image[0x410C+slot];}
    }
    return true;
}
StoreResult ReadBytes(const std::wstring& path,void* bytes,DWORD size){
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StoreResult::Missing:StoreResult::Unavailable;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return StoreResult::Invalid;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return StoreResult::Unavailable;
    LARGE_INTEGER length{};DWORD read=0;
    const bool ok=GetFileSizeEx(file,&length)&&length.QuadPart==size&&ReadFile(file,bytes,size,&read,nullptr)&&read==size;
    CloseHandle(file);return ok?StoreResult::Found:StoreResult::Invalid;
}
StoreResult ReadLeaf(const std::wstring& path,Record& record){return ReadBytes(path,&record,sizeof(record));}
bool ValidRecord(const Record& record,const Hash& pathHash,const Hash& imageHash,const SaveImage& image){
    Hash stateHash{};
    return Digest(&record.state,sizeof(record.state),stateHash)&&
        std::memcmp(record.magic,magic,8)==0&&record.pathHash==pathHash&&record.imageHash==imageHash&&
        record.stateHash==stateHash&&MatchesSave(image,record.state);
}
bool Supersedes(const workshop::State& prior,const workshop::State& next){
    return next.revision>prior.revision||std::memcmp(&prior,&next,sizeof(next))==0;
}
StoreResult ReadPrepared(const std::wstring& leaf,const Hash& pathHash,const Hash& imageHash,
                         const SaveImage& loaded,PreparedSave& prepared){
    const auto found=ReadBytes(leaf+L".pending",&prepared,sizeof(prepared));
    if(found!=StoreResult::Found)return found;
    Hash actual{};
    if(!RonsoPool::IsValidSave(prepared.image)||!Digest(prepared.image.data(),prepared.image.size(),actual)||actual!=imageHash||
       !ValidRecord(prepared.record,pathHash,imageHash,prepared.image)||!MatchesSave(loaded,prepared.record.state))return StoreResult::Invalid;
    return StoreResult::Found;
}
bool AtomicBytes(const std::wstring& leaf,const void* bytes,DWORD size,bool checkpoint=false){
    const auto temporary=leaf+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=CheckpointBoundary(checkpoint,0)&&
        WriteFile(file,bytes,size,&written,nullptr)&&written==size&&CheckpointBoundary(checkpoint,1)&&
        FlushFileBuffers(file)&&CheckpointBoundary(checkpoint,2);
    CloseHandle(file);
    if(ok)ok=CheckpointBoundary(checkpoint,3)&&
        MoveFileExW(temporary.c_str(),leaf.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(ok)CheckpointCrashBoundary(checkpoint,4);
    if(!ok)DeleteFileW(temporary.c_str());
    return ok;
}
bool ValidCheckpoint(const CheckpointRecord& record,const Hash& pathHash){
    Hash bodyHash{};const auto& body=record.body;
    if(std::memcmp(record.magic,"FFXWKCP1",8)||record.pathHash!=pathHash||
       !Digest(&body,sizeof(body),bodyHash)||bodyHash!=record.bodyHash||
       !RonsoPool::IsValidSave(body.image)||!MatchesSave(body.image,body.state))return false;
    const auto& pool=body.pool;
    if(!pool.present)return !pool.originalMax&&!pool.charge&&!pool.maximum;
    return pool.present==1&&pool.originalMax>0&&pool.originalMax<=RonsoPool::kCapacity&&
        pool.maximum==RonsoPool::kCapacity&&pool.charge<=pool.maximum&&
        body.image[RonsoPool::kSaveCharge]==pool.charge&&body.image[RonsoPool::kSaveMaximum]==pool.maximum;
}
bool CheckpointAnchor(const CheckpointRecord& record,const Hash& disk){
    if(record.body.anchor==disk)return true;
    Hash serialized{};return Digest(record.body.image.data(),record.body.image.size(),serialized)&&serialized==disk;
}
struct CheckpointLease {
    HANDLE file=INVALID_HANDLE_VALUE;
    explicit CheckpointLease(const std::wstring& leaf){
        file=CreateFileW((leaf+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        if(file!=INVALID_HANDLE_VALUE&&(!GetFileInformationByHandle(file,&info)||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))){
            CloseHandle(file);file=INVALID_HANDLE_VALUE;
        }
    }
    ~CheckpointLease(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    CheckpointLease(const CheckpointLease&)=delete;
    CheckpointLease& operator=(const CheckpointLease&)=delete;
};
}
bool Fingerprint(const void* bytes,std::size_t size,Hash& out){return Digest(bytes,size,out);}
bool ImportSave(const SaveImage& image,std::uint64_t seed,workshop::State& out){
    std::array<std::uint16_t,112> items{};
    return RonsoPool::IsValidSave(image)&&MaterialCounts(image,items)&&
        workshop::Import(image.data()+0x44DC,items.data(),seed,out)==workshop::Error::Ok;
}
bool MatchesSave(const SaveImage& image,const workshop::State& state){
    if(workshop::Validate(state)!=workshop::Error::Ok)return false;
    std::array<std::uint16_t,112> items{};if(!MaterialCounts(image,items))return false;
    if(std::memcmp(items.data(),state.items,sizeof(state.items))!=0)return false;
    for(unsigned i=0;i<200;++i)if(std::memcmp(state.pieces[i].native,image.data()+0x44DC+22*i,22)!=0)return false;
    return true;
}
bool Store::Initialize(const std::wstring& directory,bool create){
    directory_.clear();if(directory.empty())return false;
    DWORD attrs=GetFileAttributesW(directory.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES && create){
        if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        attrs=GetFileAttributesW(directory.c_str());
    }
    if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    directory_=directory;return true;
}
std::wstring Store::RecordPath(const std::wstring& path,const SaveImage& image) const {
    if(directory_.empty())return {};
    Hash pathHash{},imageHash{};if(!Keys(path,image,pathHash,imageHash))return {};
    return directory_+L"\\"+Hex(pathHash)+L"-"+Hex(imageHash)+L".bin";
}
StoreResult Store::Read(const std::wstring& path,const SaveImage& image,workshop::State& state) const {
    Hash diskHash{};if(!Digest(image.data(),image.size(),diskHash))return StoreResult::Unavailable;
    return ReadLoaded(path,diskHash,image,state);
}
StoreResult Store::ReadLoaded(const std::wstring& path,const Hash& diskHash,const SaveImage& image,workshop::State& state) const {
    Hash pathHash{},ignored{};
    if(directory_.empty() || !Keys(path,image,pathHash,ignored))return StoreResult::Unavailable;
    const auto leaf=directory_+L"\\"+Hex(pathHash)+L"-"+Hex(diskHash)+L".bin";
    Record record{};const auto result=ReadLeaf(leaf,record);
    if(result!=StoreResult::Found&&result!=StoreResult::Missing)return result;
    if(result==StoreResult::Found&&!ValidRecord(record,pathHash,diskHash,image))return StoreResult::Invalid;
    // The complete matching native image, including currency, is the admission
    // marker. Physical bytes can be identical across logical-only operations.
    // An older committed sidecar must not hide a newer prepared snapshot, but
    // equal-revision divergence is a conflict, never a timestamp-based choice.
    PreparedSave prepared{};const auto staged=ReadPrepared(leaf,pathHash,diskHash,image,prepared);
    if(staged!=StoreResult::Found&&staged!=StoreResult::Missing)return staged;
    if(result==StoreResult::Missing){if(staged==StoreResult::Found)state=prepared.record.state;return staged;}
    if(staged==StoreResult::Found){
        const auto oldRevision=record.state.revision,newRevision=prepared.record.state.revision;
        if(newRevision==oldRevision&&std::memcmp(&record.state,&prepared.record.state,sizeof(state)))return StoreResult::Invalid;
        if(newRevision>oldRevision){state=prepared.record.state;return StoreResult::Found;}
    }
    state=record.state;return StoreResult::Found;
}
bool Store::Write(const std::wstring& path,const SaveImage& image,const workshop::State& state,
                  const AeonAscension::Ledger* receipts) const {
    if(!RonsoPool::IsValidSave(image)||!MatchesSave(image,state))return false;
    if(receipts&&!PrepareReceipts(path,state,*receipts))return false;
    const auto leaf=RecordPath(path,image);if(leaf.empty())return false;
    Record old{};const auto prior=ReadLeaf(leaf,old);
    if(prior!=StoreResult::Found && prior!=StoreResult::Missing)return false;
    Record record{};std::memcpy(record.magic,magic,8);record.state=state;
    if(!Keys(path,image,record.pathHash,record.imageHash)||!Digest(&record.state,sizeof(record.state),record.stateHash))return false;
    if(prior==StoreResult::Found&&!ValidRecord(old,record.pathHash,record.imageHash,image))return false;
    if(prior==StoreResult::Found&&!Supersedes(old.state,state))return false;
    PreparedSave prepared{};const auto pending=ReadPrepared(leaf,record.pathHash,record.imageHash,image,prepared);
    if(pending!=StoreResult::Found&&pending!=StoreResult::Missing)return false;
    if(pending==StoreResult::Found&&!Supersedes(prepared.record.state,state))return false;
    if(prior==StoreResult::Found && std::memcmp(&old.state,&state,sizeof(state))==0)return true;
    const auto temporary=leaf+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,&record,sizeof(record),&written,nullptr)&&written==sizeof(record)&&FlushFileBuffers(file);
    CloseHandle(file);
    if(ok && prior==StoreResult::Found)ok=CopyFileW(leaf.c_str(),(leaf+L".bak").c_str(),FALSE)!=FALSE;
    if(ok)ok=MoveFileExW(temporary.c_str(),leaf.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temporary.c_str());return false;}
    workshop::State verified{};return Read(path,image,verified)==StoreResult::Found && std::memcmp(&verified,&state,sizeof(state))==0;
}
bool Store::PinLoaded(const std::wstring& path,const Hash& diskHash,const SaveImage& loaded,workshop::State& state) const {
    if(directory_.empty()||!RonsoPool::IsValidSave(loaded)||!MatchesSave(loaded,state))return false;
    workshop::State existing{};const auto prior=ReadLoaded(path,diskHash,loaded,existing);
    if(prior==StoreResult::Found){state=existing;return true;}
    if(prior!=StoreResult::Missing)return false;
    Record record{};Hash ignored{};std::memcpy(record.magic,magic,8);record.state=state;
    if(!Keys(path,loaded,record.pathHash,ignored)||!Digest(&state,sizeof(state),record.stateHash))return false;
    // Ronso ownership can transform the load buffer. Its original on-disk hash,
    // not the transformed payload hash, is the identity of this save version.
    record.imageHash=diskHash;
    const auto leaf=directory_+L"\\"+Hex(record.pathHash)+L"-"+Hex(diskHash)+L".bin";
    const auto temporary=leaf+L".seed-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool complete=WriteFile(file,&record,sizeof(record),&written,nullptr)&&written==sizeof(record)&&FlushFileBuffers(file);
    CloseHandle(file);
    // Without REPLACE_EXISTING two admissions cannot overwrite one another's
    // generator. An existing valid winner is read back instead of drawing again.
    const bool moved=complete&&MoveFileExW(temporary.c_str(),leaf.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!moved)DeleteFileW(temporary.c_str());
    if(ReadLoaded(path,diskHash,loaded,existing)!=StoreResult::Found)return false;
    state=existing;return true;
}
bool Store::PrepareSave(const std::wstring& path,const SaveImage& image,const workshop::State& state,
                        const AeonAscension::Ledger* receipts) const {
    if(!RonsoPool::IsValidSave(image)||!MatchesSave(image,state))return false;
    if(receipts&&!PrepareReceipts(path,state,*receipts))return false;
    const auto leaf=RecordPath(path,image);if(leaf.empty())return false;
    PreparedSave prepared{};prepared.image=image;auto& record=prepared.record;
    std::memcpy(record.magic,magic,8);record.state=state;
    if(!Keys(path,image,record.pathHash,record.imageHash)||!Digest(&state,sizeof(state),record.stateHash))return false;
    Record committed{};const auto complete=ReadLeaf(leaf,committed);
    if(complete!=StoreResult::Found&&complete!=StoreResult::Missing)return false;
    if(complete==StoreResult::Found&&(!ValidRecord(committed,record.pathHash,record.imageHash,image)||!Supersedes(committed.state,state)))return false;
    PreparedSave existing{};const auto prior=ReadPrepared(leaf,record.pathHash,record.imageHash,image,existing);
    if(prior!=StoreResult::Found&&prior!=StoreResult::Missing)return false;
    if(prior==StoreResult::Found&&std::memcmp(&existing,&prepared,sizeof(prepared))==0)return true;
    if(prior==StoreResult::Found&&!Supersedes(existing.record.state,state))return false;
    if(!AtomicBytes(leaf+L".pending",&prepared,sizeof(prepared)))return false;
    return ReadPrepared(leaf,record.pathHash,record.imageHash,image,existing)==StoreResult::Found&&
        std::memcmp(&existing,&prepared,sizeof(prepared))==0;
}
bool Store::CommitPrepared(const std::wstring& path,const SaveImage& image) const {
    const auto leaf=RecordPath(path,image);Hash pathHash{},imageHash{};
    if(leaf.empty()||!Keys(path,image,pathHash,imageHash))return false;
    PreparedSave prepared{};
    return ReadPrepared(leaf,pathHash,imageHash,image,prepared)==StoreResult::Found&&
        Write(path,image,prepared.record.state);
}
bool Store::StageTransaction(const std::wstring& path,const Hash& anchor,const SaveImage& before,
    const workshop::State& state,const workshop::Request& request,const workshop::Economy& economy,const workshop::Plan& reviewed,
    const AeonAscension::Ledger* receipts) const {
    if(directory_.empty()||!RonsoPool::IsValidSave(before)||!MatchesSave(before,state))return false;
    workshop::Plan plan{};
    const auto result=receipts?AeonAscension::PreviewGeneric(state,*receipts,receipts->saveId,{},economy,request,plan):
        workshop::Preview(state,request,plan,economy);
    if(result!=workshop::Error::Ok||std::memcmp(&plan,&reviewed,sizeof(plan)))return false;
    std::uint32_t gil=0;std::memcpy(&gil,before.data()+0x3D88,4);if(gil!=economy.gil)return false;
    TransactionIntent intent{};Hash imageHash{};
    if(!Keys(path,before,intent.pathHash,imageHash))return false;
    std::memcpy(intent.magic,"FFXWKTX1",8);
    intent.body.anchor=anchor;intent.body.before=before;intent.body.state=state;
    intent.body.request=request;intent.body.economy=economy;intent.body.plan=reviewed;
    if(!Digest(&intent.body,sizeof(intent.body),intent.bodyHash))return false;
    // One bounded active intent per save path, not an unbounded file per click.
    // This private audit/rollback record is NEVER an eligible save sidecar.
    const auto leaf=directory_+L"\\"+Hex(intent.pathHash)+L".transaction";
    TransactionIntent previous{};const auto found=ReadBytes(leaf,&previous,sizeof(previous));
    if(found!=StoreResult::Found&&found!=StoreResult::Missing)return false;
    if(found==StoreResult::Found){Hash hash{};
        if(std::memcmp(previous.magic,"FFXWKTX1",8)||previous.pathHash!=intent.pathHash||
           !Digest(&previous.body,sizeof(previous.body),hash)||hash!=previous.bodyHash)return false;
    }
    if(!AtomicBytes(leaf,&intent,sizeof(intent)))return false;
    return ReadBytes(leaf,&previous,sizeof(previous))==StoreResult::Found&&std::memcmp(&previous,&intent,sizeof(intent))==0;
}
StoreResult Store::SelectCheckpoint(const std::wstring& path,const SaveImage& disk,
    SaveImage& selected,NativeSaveEvents::CheckpointSelection& selection) const {
    selection={};Hash pathHash{},diskHash{};
    if(directory_.empty()||!RonsoPool::IsValidSave(disk)||!Keys(path,disk,pathHash,diskHash))return StoreResult::Invalid;
    const auto leaf=directory_+L"\\"+Hex(pathHash)+L".checkpoint.bin";
    CheckpointRecord record{};const auto found=ReadBytes(leaf,&record,sizeof(record));
    if(found!=StoreResult::Found)return found;
    if(!ValidCheckpoint(record,pathHash))return StoreResult::Invalid;
    selection.proof=record.bodyHash;
    workshop::State native{};const auto observed=Read(path,disk,native);
    if(observed!=StoreResult::Found&&observed!=StoreResult::Missing)return observed;
    if(observed==StoreResult::Found&&native.revision>=record.body.state.revision){
        if(native.revision==record.body.state.revision&&
           std::memcmp(&native,&record.body.state,sizeof(native)))return StoreResult::Invalid;
        return StoreResult::Missing;
    }
    if(!CheckpointAnchor(record,diskHash))return StoreResult::Invalid;
    selected=record.body.image;selection.pool=record.body.pool;
    selection.proof=record.bodyHash;selection.selected=true;return StoreResult::Found;
}
StoreResult Store::ReadCheckpointLoaded(const std::wstring& path,const Hash& disk,const Hash& proof,
    const SaveImage& loaded,workshop::State& state) const {
    Hash pathHash{},ignored{};
    if(directory_.empty()||!Keys(path,loaded,pathHash,ignored))return StoreResult::Unavailable;
    CheckpointRecord record{};
    const auto found=ReadBytes(directory_+L"\\"+Hex(pathHash)+L".checkpoint.bin",&record,sizeof(record));
    if(found!=StoreResult::Found)return found==StoreResult::Missing?StoreResult::Invalid:found;
    if(!ValidCheckpoint(record,pathHash)||record.bodyHash!=proof||!CheckpointAnchor(record,disk))return StoreResult::Invalid;
    const auto& p=record.body.pool;const RonsoPool::SavedOwner owner{p.originalMax,p.charge,p.maximum};
    bool matches=false;
    for(bool active:{false,true}){
        RonsoPool::SaveSession session{};SaveImage expected{};
        const auto decision=RonsoPool::LoadPool(active,record.body.image,p.present?&owner:nullptr,0,&session,&expected);
        if((decision==RonsoPool::SaveDecision::Native||decision==RonsoPool::SaveDecision::Converted)&&expected==loaded)matches=true;
    }
    if(!matches)return StoreResult::Invalid;
    state=record.body.state;return StoreResult::Found;
}
bool Store::PublishCheckpoint(const std::wstring& path,const Hash& anchor,const SaveImage& serialized,
    const NativeSaveEvents::CheckpointOwnership& pool,const workshop::State& state,Hash& expectedHead,
    const AeonAscension::Ledger* receipts) const {
    if(directory_.empty())return false;
    CheckpointRecord record{};Hash ignored{};
    std::memcpy(record.magic,"FFXWKCP1",8);record.body.anchor=anchor;
    record.body.image=serialized;record.body.pool=pool;record.body.state=state;
    if(!Keys(path,serialized,record.pathHash,ignored)||!Digest(&record.body,sizeof(record.body),record.bodyHash)||
       !ValidCheckpoint(record,record.pathHash))return false;
    const auto leaf=directory_+L"\\"+Hex(record.pathHash)+L".checkpoint.bin";
    CheckpointLease lease(leaf);if(lease.file==INVALID_HANDLE_VALUE)return false;
    CheckpointRecord prior{};const auto found=ReadBytes(leaf,&prior,sizeof(prior));
    if(found!=StoreResult::Found&&found!=StoreResult::Missing)return false;
    if(found==StoreResult::Found){
        if(!ValidCheckpoint(prior,record.pathHash))return false;
        if(std::memcmp(&prior,&record,sizeof(record))==0){expectedHead=record.bodyHash;return true;}
        if(prior.bodyHash!=expectedHead)return false;
        if(record.body.state.revision<=prior.body.state.revision)return false;
    }else if(expectedHead!=Hash{})return false;
    // Immutable receipt attachment is flushed before the existing commit point.
    // Its complete state hash includes paid materials, revision and ability IDs.
    // An orphan attachment cannot select that state; only this checkpoint or a
    // matching native-save journal can. The v1 inventory format stays unchanged.
    if(receipts&&!PrepareReceipts(path,state,*receipts))return false;
    // Rename is the commit point. No fallible readback after it may turn an
    // accepted durable result into a RAM rollback. Tests verify its exact bytes.
    if(!AtomicBytes(leaf,&record,sizeof(record),true))return false;
    expectedHead=record.bodyHash;return true;
}
bool Store::SaveIdentity(const std::wstring& path,const Hash& initialAnchor,AeonAscension::SaveId& out) const {
    Hash pathHash{},ignored{};SaveImage empty{};
    if(directory_.empty()||!Keys(path,empty,pathHash,ignored))return false;
    struct Seed {Hash path,anchor;} seed{pathHash,initialAnchor};
    return Digest(&seed,sizeof(seed),out)&&AeonAscension::Nonzero(out);
}
std::wstring Store::ReceiptPath(const std::wstring& path,const workshop::State& state) const {
    Hash pathHash{},ignored{},stateHash{};SaveImage empty{};
    if(directory_.empty()||workshop::Validate(state)!=workshop::Error::Ok||
       !Keys(path,empty,pathHash,ignored)||!Digest(&state,sizeof(state),stateHash))return {};
    return directory_+L"\\"+Hex(pathHash)+L"-ascension-"+Hex(stateHash)+L".bin";
}
StoreResult Store::ReadReceipts(const std::wstring& path,const workshop::State& state,AeonAscension::Ledger& out) const {
    out={};const auto leaf=ReceiptPath(path,state);if(leaf.empty())return StoreResult::Unavailable;
    ReceiptRecord record{};const auto result=ReadBytes(leaf,&record,sizeof(record));if(result!=StoreResult::Found)return result;
    Hash pathHash{},ignored{},stateHash{},ledgerHash{};SaveImage empty{};
    if(std::memcmp(record.magic,"FFXASCP1",8)||!Keys(path,empty,pathHash,ignored)||record.pathHash!=pathHash||
       !Digest(&state,sizeof(state),stateHash)||record.stateHash!=stateHash||
       !Digest(&record.ledger,sizeof(record.ledger),ledgerHash)||record.ledgerHash!=ledgerHash||
       !AeonAscension::Validate(record.ledger,record.ledger.saveId,state))return StoreResult::Invalid;
    out=record.ledger;return StoreResult::Found;
}
bool Store::PrepareReceipts(const std::wstring& path,const workshop::State& state,const AeonAscension::Ledger& receipts) const {
    if(!AeonAscension::Validate(receipts,receipts.saveId,state))return false;
    const auto leaf=ReceiptPath(path,state);if(leaf.empty())return false;
    AeonAscension::Ledger prior{};const auto existing=ReadReceipts(path,state,prior);
    if(existing==StoreResult::Found)return !std::memcmp(&prior,&receipts,sizeof(receipts));
    if(existing!=StoreResult::Missing)return false;
    ReceiptRecord record{};Hash ignored{};SaveImage empty{};std::memcpy(record.magic,"FFXASCP1",8);record.ledger=receipts;
    if(!Keys(path,empty,record.pathHash,ignored)||!Digest(&state,sizeof(state),record.stateHash)||
       !Digest(&record.ledger,sizeof(record.ledger),record.ledgerHash))return false;
    const auto temporary=leaf+L".new-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence.fetch_add(1));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool complete=WriteFile(file,&record,sizeof(record),&written,nullptr)&&written==sizeof(record)&&FlushFileBuffers(file);
    CloseHandle(file);
    // Create-only publication cannot overwrite a concurrent conflicting record.
    const bool moved=complete&&MoveFileExW(temporary.c_str(),leaf.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!moved)DeleteFileW(temporary.c_str());
    return ReadReceipts(path,state,prior)==StoreResult::Found&&!std::memcmp(&prior,&receipts,sizeof(receipts));
}
bool Store::StageAscensionTransaction(const std::wstring& path,const Hash& anchor,const SaveImage& before,
    const workshop::State& state,const AeonAscension::Ledger& owned,const AeonAscension::SaveId& save,
    const AeonAscension::Mapping& mapping,const workshop::Economy& economy,const AeonAscension::Request& request,
    const AeonAscension::Plan& reviewed) const {
    if(directory_.empty()||!RonsoPool::IsValidSave(before)||!MatchesSave(before,state))return false;
    AeonAscension::Plan candidate{};
    if(AeonAscension::Preview(state,owned,save,mapping,economy,request,candidate)!=workshop::Error::Ok||
       std::memcmp(&candidate,&reviewed,sizeof(candidate)))return false;
    std::uint32_t gil=0;std::memcpy(&gil,before.data()+0x3D88,4);if(gil!=economy.gil)return false;
    AscensionIntent intent{};Hash ignored{};
    if(!Keys(path,before,intent.pathHash,ignored))return false;
    std::memcpy(intent.magic,"FFXASTX1",8);auto& body=intent.body;
    body.anchor=anchor;body.before=before;body.state=state;body.owned=owned;body.save=save;
    body.mapping=mapping;body.economy=economy;body.request=request;body.plan=reviewed;
    if(!Digest(&body,sizeof(body),intent.bodyHash))return false;
    // Intent is audit-only and is never accepted as a save or paid checkpoint.
    return AtomicBytes(directory_+L"\\"+Hex(intent.pathHash)+L".ascension-transaction",&intent,sizeof(intent));
}
#ifdef FFXHOOKS_TESTING
void Store::FailCheckpointWriteForTests(int point){checkpointWriteFailure.store(point);}
void Store::CrashCheckpointWriteForTests(int point){checkpointWriteCrash.store(point);}
#endif
}
