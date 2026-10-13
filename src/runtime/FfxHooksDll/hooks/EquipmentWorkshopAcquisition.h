#pragma once
#include "../../../../research/equipment_workshop/include/acquisition.h"
#include "NativeUiHookSupport.h"
#include "../shared/ExecutableProfile.h"
#include <array>
#include <cstring>

namespace FfxHooks::EquipmentWorkshop::Acquisition {
// Steam 0537b2a1...: getter RVA390A10 and Customize caller RVA4C2861
// consume this native table. No global ability definitions or save bytes change.
inline constexpr auto PointerRva=ExecutableProfile::Rva<0xD2A964>();
inline bool Read(std::uintptr_t image,workshop::AcquisitionCatalog& out) noexcept {
    out={};if(!image)return false;
    std::uint32_t pointer=0,current=0;std::array<unsigned char,20> header{};
    if(!NativeUiSupport::Copy(&pointer,reinterpret_cast<void*>(image+PointerRva),4)||pointer<0x10000||pointer>UINT32_MAX-32788||
       !NativeUiSupport::Copy(header.data(),reinterpret_cast<void*>(pointer),header.size()))return false;
    const unsigned size=20u+unsigned(header[14])+(unsigned(header[15])<<8);
    if(size<=20||size>32788)return false;
    std::array<unsigned char,32788> bytes{};
    if(!NativeUiSupport::Copy(bytes.data(),reinterpret_cast<void*>(pointer),size)||
       std::memcmp(bytes.data(),header.data(),header.size())||
       !NativeUiSupport::Copy(&current,reinterpret_cast<void*>(image+PointerRva),4)||current!=pointer)return false;
    return workshop::ReadAcquisition(bytes.data(),size,out);
}
}
