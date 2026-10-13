#pragma once
#include "CaptureCascadeCore.h"
#include <filesystem>

namespace FfxHooks::CaptureCascade {
using Hash=std::array<unsigned char,32>;
struct Record {State state;Hash nativeHash{};};
inline constexpr std::size_t kRecordBytes=820;
using RecordBytes=std::array<unsigned char,kRecordBytes>;
bool Encode(const Record&,RecordBytes&) noexcept;
bool Decode(const unsigned char*,std::size_t,Record&) noexcept;
enum class StoreCode {Found,Missing,Recovered,Foreign,Invalid,IoFailure};
class Store {
public:
    StoreCode Read(const std::filesystem::path& native,const Hash&,Record&) const;
    bool Prepare(const std::filesystem::path& native,const Record&) const;
    bool Commit(const std::filesystem::path& native,const Hash&) const;
    bool Abort(const std::filesystem::path& native,const Record&) const;
    static std::filesystem::path Extension(const std::filesystem::path&,const char*);
};
}
