#pragma once
#include "ArcanaCore.h"
#include <array>
#include <filesystem>

namespace FfxHooks::Arcana {
using Hash=std::array<unsigned char,32>;
struct Resources {
    std::uint8_t valid=0;
    std::array<std::uint32_t,kActorCount> hp{},mp{};
};
struct Record {State state;Hash nativeHash{},packHash{};Resources resources;};
inline constexpr std::size_t kRecordBytes=276;
using RecordBytes=std::array<unsigned char,kRecordBytes>;
bool Encode(const Record&,RecordBytes&) noexcept;
bool Decode(const unsigned char*,std::size_t,Record&) noexcept;
enum class StoreCode {Found,Missing,Invalid,Foreign,IoFailure,Recovered};
class Store {
public:
    StoreCode Read(const std::filesystem::path& native,const Hash& nativeHash,const Hash& packHash,Record& out) const;
    StoreCode ReadCompatible(const std::filesystem::path& native,const Hash& nativeHash,const Hash& currentPack,
                             const Hash* previousPacks,std::size_t count,Record& out,bool& migrated) const;
    bool Prepare(const std::filesystem::path& native,const Record& record) const;
    bool Commit(const std::filesystem::path& native,const Hash& writtenHash) const;
    bool Abort(const std::filesystem::path& native,const Record& prepared) const;
    static std::filesystem::path Extension(const std::filesystem::path& native,const char* suffix);
};
}
