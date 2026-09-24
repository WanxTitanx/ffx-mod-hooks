#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace PrivatePeFixture {
// Wine maps an EXE with DONT_RESOLVE_DLL_REFERENCES without applying HIGHLOW
// relocations. Windows applies them. Normalize only this private harness image;
// the runtime's actual loaded-image signature requirements stay unchanged.
inline bool NormalizeRelocations(HMODULE module) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    const auto address=reinterpret_cast<std::uintptr_t>(base);
    if(!base)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0xF00)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||
       nt->OptionalHeader.SizeOfImage!=0x237D000)return false;
    // The supported file's preferred base is fixed; the Windows loader may
    // rewrite ImageBase in the mapped header to the actual allocation base.
    constexpr std::uint32_t preferred=0x400000;
    if(address==preferred)return true;
    std::uint32_t anchor=0;
    std::memcpy(&anchor,base+0x394030+24,4);
    if(anchor==address+0xD334CC)return true;
    if(anchor!=preferred+0xD334CC)return false;
    const auto imageSize=nt->OptionalHeader.SizeOfImage;
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if(!directory.Size||directory.VirtualAddress>=imageSize||directory.Size>imageSize-directory.VirtualAddress)return false;
    const auto delta=static_cast<std::uint32_t>(address-preferred);
    for(std::uint32_t at=0;at<directory.Size;){
        if(directory.Size-at<sizeof(IMAGE_BASE_RELOCATION))return false;
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base+directory.VirtualAddress+at);
        if(block->SizeOfBlock<8||block->SizeOfBlock>directory.Size-at||(block->SizeOfBlock&1))return false;
        const auto* words=reinterpret_cast<const WORD*>(block+1);
        for(unsigned i=0;i<(block->SizeOfBlock-8)/2;++i){
            const unsigned kind=words[i]>>12;
            if(kind==IMAGE_REL_BASED_ABSOLUTE)continue;
            if(kind!=IMAGE_REL_BASED_HIGHLOW||block->VirtualAddress>imageSize-4-(words[i]&0xFFF))return false;
            auto* target=base+block->VirtualAddress+(words[i]&0xFFF);
            std::uint32_t value=0;std::memcpy(&value,target,4);value+=delta;
            DWORD previous=0,ignored=0;
            if(!VirtualProtect(target,4,PAGE_EXECUTE_READWRITE,&previous))return false;
            std::memcpy(target,&value,4);
            if(!VirtualProtect(target,4,previous,&ignored))return false;
        }
        at+=block->SizeOfBlock;
    }
    std::memcpy(&anchor,base+0x394030+24,4);
    return anchor==address+0xD334CC&&FlushInstructionCache(GetCurrentProcess(),base,imageSize)!=FALSE;
}
}
