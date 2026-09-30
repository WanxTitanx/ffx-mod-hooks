#pragma once
#include <cstdint>
#include <string_view>

namespace FfxHooks {
// Native slots use three digits. Fahrenheit uses the same minimum width with
// an Int32 slot index, so 1000 and larger must retain their exact identity.
inline bool IsNativeSaveSlotPath(std::wstring_view path) noexcept {
    if(path.empty()||path.size()>4096||path.find(wchar_t(0))!=path.npos)return false;
    const auto slash=path.find_last_of(L"/\\");
    if(slash==path.npos)return false;
    const auto name=path.substr(slash+1);
    if(name.size()<7||name.size()>14)return false;
    if((name[0]!=L'f'&&name[0]!=L'F')||(name[1]!=L'f'&&name[1]!=L'F')||
       (name[2]!=L'x'&&name[2]!=L'X')||name[3]!=L'_')return false;
    if(name.size()>7&&name[4]==L'0')return false;
    std::uint32_t slot=0;
    for(std::size_t i=4;i<name.size();++i){
        if(name[i]<L'0'||name[i]>L'9')return false;
        const auto digit=static_cast<std::uint32_t>(name[i]-L'0');
        if(slot>(2147483647u-digit)/10u)return false;
        slot=slot*10u+digit;
    }
    return true;
}
}
