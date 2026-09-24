#pragma once
#include "../../../../research/equipment_workshop/include/workshop.h"
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
    bool Write(const std::wstring& path,const SaveImage&,const workshop::State&) const;
    std::wstring RecordPath(const std::wstring& path,const SaveImage&) const;
private:
    std::wstring directory_;
};
}
