#pragma once
#include "OmegaT7Seed.h"
#include <filesystem>
namespace FfxHooks::OmegaT7 {
using Hash=std::array<unsigned char,32>;
struct Reference {Hash native{},campaign{};};
struct Campaign {Hash id{};Seed::State seed{};std::uint64_t revision=0;};
enum class StoreCode {Found,Missing,Recovered,Foreign,Invalid,IoFailure};
inline constexpr unsigned RecordBytes=128;
using Bytes=std::array<unsigned char,RecordBytes>;
bool Encode(const Reference&,Bytes&) noexcept;
bool Encode(const Campaign&,Bytes&) noexcept;
bool Decode(const Bytes&,Reference&) noexcept;
bool Decode(const Bytes&,Campaign&) noexcept;
Campaign AdoptCampaign(const Hash&) noexcept;
class Store {
    std::filesystem::path root_;
public:
    explicit Store(std::filesystem::path root={}):root_(std::move(root)){}
    StoreCode ReadCampaign(const Hash&,Campaign&) const;
    bool SaveCampaign(const Campaign&) const;
    StoreCode ReadReference(const std::filesystem::path&,const Hash&,Reference&) const;
    bool AdoptReference(const std::filesystem::path&,const Reference&) const;
    bool PrepareReference(const std::filesystem::path&,const Reference&) const;
    bool CommitReference(const std::filesystem::path&,const Hash&) const;
    bool AbortReference(const std::filesystem::path&,const Reference&) const;
    std::filesystem::path CampaignPath(const Hash&) const;
    static std::filesystem::path ReferencePath(const std::filesystem::path&,const char* suffix=".omega-t7.v1");
};
}
