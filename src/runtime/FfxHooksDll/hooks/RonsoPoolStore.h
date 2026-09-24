#pragma once
#include "RonsoPoolSave.h"
#include <string>

namespace FfxHooks::RonsoPool {
enum class OwnerRead { Missing, Found, Invalid, Unavailable };
class OwnerStore {
public:
    bool Initialize(const std::wstring& directory,bool create);
    OwnerRead Read(const std::wstring& canonicalPath,const SaveImage&,SavedOwner*) const;
    bool Write(const std::wstring& canonicalPath,const SaveImage&,const SavedOwner&) const;
    std::wstring RecordPath(const std::wstring& canonicalPath,const SaveImage&) const;
    static bool IsSavePath(const std::wstring&) noexcept;
private:
    std::wstring directory_;
};
}
