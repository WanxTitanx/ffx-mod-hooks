#pragma once
#include "AutoAbilitySlots.h"
#include "../../../../research/equipment_workshop/include/workshop.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace FfxHooks::AeonAscension {
// Jarvis-HOOK: additive paid metadata, never stored in native save padding,
// Piece ranks, or a fifth WORD outside the native 22-byte equipment record.
using SaveId=std::array<unsigned char,32>;
inline constexpr unsigned MaximumReceipts=20,RecipeVersion=1;
inline constexpr const char* EffectKeys[]={"aeon_break_hp_mp_limit","aeon_break_damage_limit"};
#pragma pack(push,1)
struct Receipt {
    std::uint64_t pieceId=0,abilityId=0;
    std::uint32_t owner=0,effect=0,recipeVersion=0;
    std::uint16_t word=0,reserved=0;
};
struct Ledger {
    std::uint32_t version=1,count=0;
    SaveId saveId{};
    std::array<Receipt,MaximumReceipts> entries{};
};
#pragma pack(pop)
static_assert(sizeof(Receipt)==32&&sizeof(Ledger)==680,"Ascension receipt wire v1");
struct Mapping {
    bool enabled=false;
    std::uint64_t proof=0;
    std::array<std::uint16_t,2> words{0x8094,0x8095};
    // The host resolves corresponding native Break rows from its verified
    // loaded kernel. Historical numeric IDs are not an admission mechanism.
    std::array<std::uint16_t,3> replacements{}; // BHP, BMP, BDL
};
struct Request {
    std::uint64_t revision=0,pieceId=0;
    unsigned slot=workshop::GearCount,position=5,effect=2;
    bool replace=false,remove=false;
};
struct Plan {workshop::Plan inventory{};Ledger receipts{};std::uint64_t mappingProof=0;};

inline bool Nonzero(const SaveId& value) noexcept {
    for(const auto b:value)if(b)return true;
    return false;
}
inline bool ValidMapping(const Mapping& mapping) noexcept {
    if(!mapping.enabled||!mapping.proof||mapping.words[0]==mapping.words[1])return false;
    for(unsigned i=0;i<2;++i)if((mapping.words[i]&0xF000u)!=0x8000u||
        !AutoAbilitySlots::Ascension(i,mapping.words[i]&0xFFFu))return false;
    return true;
}
inline bool ReceiptShape(const Receipt& receipt) noexcept {
    return receipt.pieceId&&receipt.abilityId&&receipt.owner>=8&&receipt.owner<18&&
        receipt.effect<2&&receipt.recipeVersion==RecipeVersion&&!receipt.reserved&&
        (receipt.word&0xF000u)==0x8000u&&AutoAbilitySlots::Ascension(receipt.effect,receipt.word&0xFFFu);
}
inline bool Matches(const Receipt& receipt,const workshop::Piece& piece,unsigned position) noexcept {
    return ReceiptShape(receipt)&&piece.id==receipt.pieceId&&piece.native[2]&&
        piece.native[4]==receipt.owner&&piece.native[5]==(receipt.effect?0:1)&&position<5&&
        (position<4?position<piece.native[11]:piece.fifthUnlocked!=0)&&
        piece.abilities[position]==receipt.abilityId&&workshop::Ability(piece,position)==receipt.word&&
        workshop::AbilityRank(piece,position)==0;
}
inline bool Validate(const Ledger& ledger,const SaveId& expected,const workshop::State& state){
    if(ledger.version!=1||ledger.count>MaximumReceipts||workshop::Validate(state)!=workshop::Error::Ok)return false;
    if(ledger.count&&(!Nonzero(expected)||ledger.saveId!=expected))return false;
    if(Nonzero(ledger.saveId)&&ledger.saveId!=expected)return false;
    for(unsigned i=0;i<ledger.count;++i){
        const auto& receipt=ledger.entries[i];if(!ReceiptShape(receipt))return false;
        for(unsigned j=0;j<i;++j){const auto& previous=ledger.entries[j];
            if(previous.abilityId==receipt.abilityId||
               (previous.pieceId==receipt.pieceId&&previous.effect==receipt.effect)||
               (previous.owner==receipt.owner&&previous.effect==receipt.effect))return false;
        }
        bool matched=false;
        for(const auto& piece:state.pieces)if(piece.id==receipt.pieceId)
            for(unsigned position=0;position<5;++position)matched=matched||Matches(receipt,piece,position);
        if(!matched)return false;
    }
    const Receipt zero{};
    for(unsigned i=ledger.count;i<MaximumReceipts;++i)
        if(std::memcmp(&ledger.entries[i],&zero,sizeof(zero)))return false;
    return true;
}
inline bool Authorized(const Ledger& ledger,const SaveId& save,const workshop::Piece& piece,
                       unsigned position,unsigned effect,const Mapping& mapping) noexcept {
    if(!ValidMapping(mapping)||ledger.version!=1||ledger.count>MaximumReceipts||effect>=2||
       !Nonzero(save)||ledger.saveId!=save||piece.native[6]!=piece.native[4]||
       (position==4&&!workshop::NativeSlotsFilled(piece)))return false;
    for(unsigned i=0;i<ledger.count;++i){const auto& receipt=ledger.entries[i];
        if(receipt.effect==effect&&receipt.word==mapping.words[effect]&&Matches(receipt,piece,position))return true;
    }
    return false;
}
inline bool HasPaidPiece(const Ledger& ledger,std::uint64_t piece) noexcept {
    if(!piece||ledger.version!=1||ledger.count>MaximumReceipts)return false;
    for(unsigned i=0;i<ledger.count;++i)if(ledger.entries[i].pieceId==piece)return true;
    return false;
}
inline bool PaidPosition(const Ledger& ledger,const workshop::Piece& piece,unsigned position) noexcept {
    if(ledger.count>MaximumReceipts)return false;
    for(unsigned i=0;i<ledger.count;++i)if(Matches(ledger.entries[i],piece,position))return true;
    return false;
}
inline void SetWord(workshop::Piece& piece,unsigned position,std::uint16_t word) noexcept {
    if(position==4)piece.fifth=word;
    else{piece.native[14+position*2]=static_cast<unsigned char>(word);piece.native[15+position*2]=static_cast<unsigned char>(word>>8);}
}
inline void PreserveRanks(workshop::Piece& piece) noexcept {
    if(piece.mode==1)for(unsigned i=0;i<5;++i)piece.ranks[i]=workshop::Ability(piece,i)==workshop::Empty?0:piece.rank;
    piece.rank=0;piece.mode=2;
}
inline bool QuoteRecipe(unsigned effect,workshop::Plan& plan) noexcept {
    if(effect>=2)return false;
    std::memset(plan.costs,0,sizeof(plan.costs));
    if(effect){plan.costs[53]=99;plan.costs[111]=30;plan.costs[109]=20;plan.costs[80]=3;plan.gilCost=15000000;}
    else{plan.costs[108]=60;plan.costs[69]=60;plan.costs[110]=30;plan.costs[80]=2;plan.gilCost=10000000;}
    std::memcpy(plan.requirements,plan.costs,sizeof(plan.costs));plan.gilDebit=plan.gilCost;return true;
}
inline workshop::Error Preview(const workshop::State& source,const Ledger& owned,const SaveId& save,
                               const Mapping& mapping,const workshop::Economy& economy,
                               const Request& request,Plan& output){
    using Error=workshop::Error;
    // Candidate storage makes aliasing safe and makes rejection transactional.
    Plan candidate{};candidate.inventory.after=source;candidate.receipts=owned;
    if(!ValidMapping(mapping))return Error::UnsupportedAbility;
    candidate.mappingProof=mapping.proof;
    if(!Nonzero(save)||!Validate(owned,save,source))return Error::InvalidState;
    if(!workshop::ValidPolicy(economy.policy)||!workshop::ValidCatalog(economy.catalog)||
       economy.policy.devFreeMaterials||economy.policy.devFreeGil)return Error::InvalidPolicy;
    if(request.slot>=workshop::GearCount||request.position>=5||request.effect>=2)return Error::InvalidRequest;
    const auto& original=source.pieces[request.slot];
    if(request.revision!=source.revision||request.pieceId!=original.id)return Error::Stale;
    if(!workshop::IsAeon(original)||original.native[5]!=(request.effect?0:1))return Error::Protected;
    if(!economy.customizeUnlocked)return Error::Locked;
    const auto access=workshop::AeonAccess(original,request.slot,economy.aeons);if(access!=Error::Ok)return access;
    if(request.position<4?request.position>=original.native[11]:!original.fifthUnlocked)return Error::InvalidRequest;
    if(!request.remove&&request.position==4&&!workshop::NativeSlotsFilled(original))return Error::InvalidRequest;
    const auto oldWord=workshop::Ability(original,request.position);
    if(oldWord==0x807B)return Error::Protected;
    auto& plan=candidate.inventory;plan.policy=economy.policy;plan.gilBefore=economy.gil;
    plan.customizeUnlocked=economy.customizeUnlocked;plan.aeons=economy.aeons;plan.catalog=economy.catalog;
    auto& piece=plan.after.pieces[request.slot];auto& receipts=candidate.receipts;
    if(request.remove){
        unsigned found=MaximumReceipts;
        for(unsigned i=0;i<receipts.count;++i)if(receipts.entries[i].effect==request.effect&&
            Matches(receipts.entries[i],original,request.position))found=i;
        if(found==MaximumReceipts)return Error::Protected;
        PreserveRanks(piece);SetWord(piece,request.position,workshop::Empty);
        piece.abilities[request.position]=0;piece.ranks[request.position]=0;
        for(unsigned i=found+1;i<receipts.count;++i)receipts.entries[i-1]=receipts.entries[i];
        receipts.entries[--receipts.count]={};
    }else{
        for(unsigned i=0;i<5;++i)if(workshop::Ability(original,i)==mapping.words[request.effect])return Error::Duplicate;
        for(unsigned i=0;i<receipts.count;++i)if(receipts.entries[i].owner==original.native[4]&&
            receipts.entries[i].effect==request.effect)return Error::Duplicate;
        if(oldWord!=workshop::Empty){
            const bool corresponding=request.effect?oldWord==mapping.replacements[2]:
                oldWord==mapping.replacements[0]||oldWord==mapping.replacements[1];
            if(!request.replace||!corresponding||!oldWord)return Error::Protected;
        }
        if(receipts.count==MaximumReceipts)return Error::Maximum;
        (void)QuoteRecipe(request.effect,plan);
        if(plan.gilDebit>economy.gil)return Error::Gil;
        for(unsigned item=0;item<workshop::ItemCount;++item){
            if(source.items[item]<plan.costs[item])return Error::Materials;
            plan.after.items[item]=static_cast<std::uint16_t>(source.items[item]-plan.costs[item]);
            plan.requirements[item]=plan.costs[item];
        }
        PreserveRanks(piece);SetWord(piece,request.position,mapping.words[request.effect]);
        piece.abilities[request.position]=plan.after.nextId++;piece.ranks[request.position]=0;
        receipts.saveId=save;
        receipts.entries[receipts.count++]={piece.id,piece.abilities[request.position],piece.native[4],
            request.effect,RecipeVersion,mapping.words[request.effect],0};
    }
    ++plan.after.revision;
    if(!Validate(receipts,save,plan.after))return Error::InvalidState;
    output=std::move(candidate);return Error::Ok;
}
inline workshop::Error PreviewGeneric(const workshop::State& source,const Ledger& receipts,const SaveId& save,
                                      const Mapping&,const workshop::Economy& economy,
                                      const workshop::Request& request,workshop::Plan& output){
    using Error=workshop::Error;
    if(!Validate(receipts,save,source))return Error::InvalidState;
    if(request.slot>=workshop::GearCount)return Error::InvalidRequest;
    const auto& piece=source.pieces[request.slot];
    if((request.op==workshop::Op::Clear&&PaidPosition(receipts,piece,request.value))||
       (request.op==workshop::Op::SetFifth&&PaidPosition(receipts,piece,4))||
       ((request.op==workshop::Op::Reforge||request.op==workshop::Op::Retire)&&HasPaidPiece(receipts,piece.id))||
       (request.op==workshop::Op::Fuse&&request.other<workshop::GearCount&&
        HasPaidPiece(receipts,source.pieces[request.other].id)))return Error::Protected;
    workshop::State masked=source;
    // Reuse the existing protected-Aeon-slot planner on a private value copy.
    // The actual ID is restored in the candidate before its validation or I/O.
    // This lets global/random refinement skip paid binary permissions without
    // inventing a +rank scale or modifying the persisted State-v1 ABI.
    for(auto& entry:masked.pieces)if(HasPaidPiece(receipts,entry.id))
        for(unsigned position=0;position<5;++position)if(PaidPosition(receipts,entry,position))SetWord(entry,position,0x807B);
    workshop::Plan candidate{};const auto result=workshop::Preview(masked,request,candidate,economy);
    if(result!=Error::Ok){
        // The existing UI consumes cost/requirement diagnostics on a rejected
        // quote. Never expose the temporary protected-word substitutions.
        candidate.after=source;output=std::move(candidate);return result;
    }
    for(unsigned i=0;i<receipts.count;++i){const auto& receipt=receipts.entries[i];bool restored=false;
        for(auto& entry:candidate.after.pieces)if(entry.id==receipt.pieceId)
            for(unsigned position=0;position<5;++position)if(entry.abilities[position]==receipt.abilityId){
                SetWord(entry,position,receipt.word);restored=true;
            }
        if(!restored)return Error::Protected;
    }
    if(!Validate(receipts,save,candidate.after))return Error::Protected;
    output=std::move(candidate);return Error::Ok;
}
} // namespace FfxHooks::AeonAscension
