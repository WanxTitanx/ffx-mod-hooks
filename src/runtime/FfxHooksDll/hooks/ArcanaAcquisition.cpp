#include "ArcanaAcquisition.h"
#include "ArcanaWorldCore.h"

namespace FfxHooks::Arcana::Acquisition {
const char* Requirement(unsigned card) noexcept {
    if(card==kOmegaWeaponCard||card==kUltimaWeaponCard)return "Defeat Omega Weapon in Omega Ruins. Both Weapon cards are awarded only for that victory.";
    if(card>=kDarkAeonFirstCard&&card<kCardCount){
        static constexpr const char* hints[]={
            "Defeat Dark Valefor in its original challenge.","Defeat Dark Ifrit in its original challenge.",
            "Complete both Dark Ixion battles.","Defeat Dark Shiva in her original challenge.",
            "Defeat Dark Bahamut in its original challenge.","Defeat Dark Anima in her original challenge.",
            "Complete all five Dark Yojimbo duels.","Defeat Dark Cindy in her original challenge.",
            "Defeat Dark Sandy in her original challenge.","Defeat Dark Mindy in her original challenge."};
        return hints[card-kDarkAeonFirstCard];
    }
    return card<kStandardCardCount?Catalog()[card].hint:"Unavailable";
}
bool ReadFacts(const unsigned char* payload,std::size_t size,Facts& output) noexcept {
    if(!payload||size<kPayloadBytes)return false;
    Facts facts;
    // Native save payload excludes its 64-byte file header. The event VM's
    // GameMoment WORD and terminal completion masks are pinned in the catalog's
    // production EBP evidence, independently of editor-only fight counters.
    facts.story=std::uint16_t(payload[0xBEC])|(std::uint16_t(payload[0xBED])<<8);
    facts.freeTravel=facts.story>=3000;
    for(unsigned actor=8;actor<=17;++actor)
        if(payload[0x55CC+actor*0x94+0x2C]&0x10)facts.aeons|=1u<<actor;
    facts.mirror=payload[0xC38]==1;
    facts.omega=(payload[0x3BE]&4)!=0;
    facts.wobbly=(payload[0x27B]&2)!=0;
    facts.riding=(payload[0x26C]&1)!=0;
    for(unsigned aeon=0;aeon<8;++aeon)if(payload[0xC89+aeon]&0x80)facts.darkAeons|=static_cast<std::uint8_t>(1u<<aeon);
    output=facts;return true;
}
}
