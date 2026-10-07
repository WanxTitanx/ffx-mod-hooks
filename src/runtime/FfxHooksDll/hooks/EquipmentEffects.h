#include "../shared/ExecutableProfile.h"
#pragma once
#include "EquipmentWorkshopRuntime.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
namespace FfxHooks::EquipmentEffects {
using ReadMemory=bool(*)(void*,const void*,std::size_t) noexcept;
struct Entry {
    unsigned word=255,kind=0,position=0,slot=200,rank=0;
    std::uint64_t pieceId=0,abilityId=0;
    bool managed=false;
};
struct View {std::array<Entry,10> entries{};unsigned count=0;};
inline unsigned Word(const unsigned char* bytes) noexcept {return bytes[0]|(unsigned(bytes[1])<<8);}
inline bool Capture(std::uintptr_t base,unsigned owner,const void* actor,ReadMemory read,View& out) noexcept {
    out={};
    if(owner>=18||!read||!actor||base<0x10000||
       base>UINT32_MAX-(::FfxHooks::ExecutableProfile::Rva<0xD334CC>()+sizeof(std::uint32_t)))return false;
    std::uint32_t actors=0;
    if(!read(&actors,reinterpret_cast<const void*>(base + (::FfxHooks::ExecutableProfile::Rva<0xD334CC>())),4)||actors<0x10000||actors>UINT32_MAX-31u*0xF90u||
       actor!=reinterpret_cast<const void*>(std::uintptr_t(actors)+owner*0xF90u))return false;
    std::array<unsigned char,4> identity{};std::array<unsigned char,2> slots{};
    const auto* native=static_cast<const unsigned char*>(actor);
    if(!read(identity.data(),native+0xC,identity.size())||Word(identity.data())!=owner||Word(identity.data()+2)!=owner||
       !read(slots.data(),native+0x592,slots.size()))return false;
    for(unsigned kind=0;kind<2;++kind){
        const unsigned slot=slots[kind];if(slot>=200)continue;
        const auto address=base + (::FfxHooks::ExecutableProfile::Rva<0xD30F2Cu>())+22u*slot;
        std::array<unsigned char,22> gear{};
        if(!read(gear.data(),reinterpret_cast<const void*>(address),gear.size()))return false;
        if(!gear[2]||gear[4]!=owner||gear[5]!=kind||gear[6]!=owner||gear[11]>4)continue;
        workshop::Piece piece{};
        const bool managed=EquipmentWorkshop::ReadPresentation(reinterpret_cast<const void*>(address),piece)&&
            piece.id&&std::memcmp(piece.native,gear.data(),gear.size())==0;
        for(unsigned position=0;position<gear[11];++position){
            const unsigned word=Word(gear.data()+14+2*position);if((word&0xF000u)!=0x8000u)continue;
            auto& entry=out.entries[out.count++];entry={word,kind,position,slot,0,0,0,false};
            if(managed){entry.pieceId=piece.id;entry.abilityId=piece.abilities[position];
                entry.rank=piece.ranks[position];entry.managed=true;}
        }
        // The fifth is a validated logical slot. Never read a fifth WORD after
        // the four native words or infer ownership from the ID by itself.
        if(managed&&piece.fifthUnlocked&&piece.abilities[4]&&workshop::NativeSlotsFilled(piece)&&
           (piece.fifth&0xF000u)==0x8000u){
            out.entries[out.count++]={piece.fifth,kind,4,slot,piece.ranks[4],piece.id,piece.abilities[4],true};
        }
    }
    return true;
}
} // namespace FfxHooks::EquipmentEffects
