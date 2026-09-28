#pragma once
#include "../../../../research/equipment_workshop/include/workshop.h"
#include "NativeSaveEvents.h"
#include "AeonAscensionCore.h"
#define FFXHOOKS_ASCENSION_RECEIPTS_V1 1
#include <array>
#include <string>

namespace FfxHooks::EquipmentWorkshop {
constexpr std::size_t kSaveBytes=0x6900;
using SaveImage=std::array<unsigned char,kSaveBytes>;
using Hash=std::array<unsigned char,32>;
bool Fingerprint(const void*,std::size_t,Hash&);
enum class StoreResult { Missing, Found, Invalid, Unavailable };
bool ImportSave(const SaveImage&,std::uint64_t seed,workshop::State&);
bool MatchesSave(const SaveImage&,const workshop::State&);
class Store {
public:
    bool Initialize(const std::wstring& directory,bool create);
    StoreResult Read(const std::wstring& path,const SaveImage&,workshop::State&) const;
    StoreResult ReadLoaded(const std::wstring& path,const Hash& diskHash,
                           const SaveImage& loaded,workshop::State&) const;
    // Create-only admission. A concurrent winner supplies the seed; never replace it.
    bool PinLoaded(const std::wstring& path,const Hash& diskHash,
                   const SaveImage& loaded,workshop::State&) const;
    bool PrepareSave(const std::wstring& path,const SaveImage&,const workshop::State&,
                     const AeonAscension::Ledger* receipts=nullptr) const;
    bool CommitPrepared(const std::wstring& path,const SaveImage&) const;
    bool StageTransaction(const std::wstring& path,const Hash& anchor,const SaveImage& before,
                          const workshop::State&,const workshop::Request&,const workshop::Economy&,const workshop::Plan&,
                          const AeonAscension::Ledger* receipts=nullptr) const;
    bool SaveIdentity(const std::wstring& path,const Hash& initialAnchor,AeonAscension::SaveId&) const;
    std::wstring ReceiptPath(const std::wstring& path,const workshop::State&) const;
    StoreResult ReadReceipts(const std::wstring& path,const workshop::State&,AeonAscension::Ledger&) const;
    bool PrepareReceipts(const std::wstring& path,const workshop::State&,const AeonAscension::Ledger&) const;
    bool StageAscensionTransaction(const std::wstring& path,const Hash& anchor,const SaveImage& before,
        const workshop::State&,const AeonAscension::Ledger&,const AeonAscension::SaveId&,
        const AeonAscension::Mapping&,const workshop::Economy&,const AeonAscension::Request&,
        const AeonAscension::Plan&) const;
    // One full paid checkpoint per native save path. Found selects recovery;
    // Missing means the disk save is current. Invalid never falls back to import.
    StoreResult SelectCheckpoint(const std::wstring& path,const SaveImage& disk,
                                 SaveImage& selected,NativeSaveEvents::CheckpointSelection&) const;
    StoreResult ReadCheckpointLoaded(const std::wstring& path,const Hash& disk,
                                    const Hash& proof,const SaveImage& loaded,workshop::State&) const;
    bool PublishCheckpoint(const std::wstring& path,const Hash& anchor,const SaveImage& serialized,
                           const NativeSaveEvents::CheckpointOwnership&,const workshop::State&,Hash& expectedHead,
                           const AeonAscension::Ledger* receipts=nullptr) const;
#ifdef FFXHOOKS_TESTING
    static void FailCheckpointWriteForTests(int point);
    static void CrashCheckpointWriteForTests(int point);
#endif
    bool Write(const std::wstring& path,const SaveImage&,const workshop::State&,
               const AeonAscension::Ledger* receipts=nullptr) const;
    std::wstring RecordPath(const std::wstring& path,const SaveImage&) const;
private:
    std::wstring directory_;
};
}
