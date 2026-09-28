#pragma once
#include "ElementPackCore.h"
#include <array>
#include <cstring>
#include <string_view>

namespace FfxHooks::ElementalDominion {
struct LoadedBank {
    BankKind kind=BankKind::Command;
    std::string_view locale;
    const unsigned char* data=nullptr;
    std::size_t size=0;
    const unsigned char* identity=nullptr;
};
using BankDigest=bool(*)(const unsigned char*,std::size_t,std::array<unsigned char,32>&) noexcept;
enum class AdmissionError {None,Invalid,Missing,Duplicate,Locale,Size,Layout,BankHash,RowHash,Capacity};
struct AdmissionProblem {AdmissionError code=AdmissionError::None;unsigned bank=0,binding=0;};
struct AdmittedRow {
    unsigned encoded=0,bindingIndex=0,bankIndex=0,width=0;
    const unsigned char* address=nullptr;
    std::array<unsigned char,108> row{};
};

// Immutable copies are prepared outside hit callbacks. Native consumers compare
// a bounded safe copy, so a cached pointer never authorizes a changed command.
class PackAdmission {
public:
    bool Prepare(const Pack& pack,const LoadedBank* loaded,unsigned count,
                 BankDigest digest,AdmissionProblem& problem) noexcept {
        problem={};
        try {
            PackAdmission candidate;
            if(!digest||(count&&!loaded)||count>5||pack.banks.size()>5||
               pack.commands.size()>2048||pack.equipment.size()>1024)
                return Fail(problem,AdmissionError::Invalid);
            for(unsigned i=0;i<count;++i)for(unsigned j=0;j<i;++j)
                if(loaded[i].kind==loaded[j].kind)return Fail(problem,AdmissionError::Duplicate,i);
            if(count!=pack.banks.size())return Fail(problem,AdmissionError::Missing);
            std::array<const LoadedBank*,5> banks{};
            for(unsigned i=0;i<pack.banks.size();++i){
                const auto& bank=pack.banks[i];
                for(unsigned j=0;j<count;++j)if(loaded[j].kind==bank.kind)banks[i]=&loaded[j];
                if(!banks[i]||!banks[i]->data)return Fail(problem,AdmissionError::Missing,i);
                const auto& bytes=*banks[i];
                if(bytes.locale!=bank.locale)return Fail(problem,AdmissionError::Locale,i);
                if(bytes.size!=bank.bytes||bytes.size<20||bytes.size>8u*1024u*1024u||
                   reinterpret_cast<std::uintptr_t>(bytes.data)>(std::numeric_limits<std::uintptr_t>::max)()-bytes.size||
                   (bytes.identity&&reinterpret_cast<std::uintptr_t>(bytes.identity)>
                       (std::numeric_limits<std::uintptr_t>::max)()-bytes.size))
                    return Fail(problem,AdmissionError::Size,i);
                if(!Layout(bank,bytes.data,bytes.size))return Fail(problem,AdmissionError::Layout,i);
                if(!Fingerprint(bytes.data,bytes.size,bank.sha256,digest))return Fail(problem,AdmissionError::BankHash,i);
            }
            candidate.commands_.reserve(pack.commands.size());candidate.equipment_.reserve(pack.equipment.size());
            for(unsigned i=0;i<pack.commands.size();++i){
                const auto& binding=pack.commands[i];AdmittedRow row{};
                if(!Bind(pack,banks,binding.bank,binding.index,binding.encoded,binding.rowSha256,false,i,digest,row,problem))return false;
                for(const auto& prior:candidate.commands_)if(prior.encoded==row.encoded)return Fail(problem,AdmissionError::Duplicate,binding.bank,i);
                candidate.commands_.push_back(row);
            }
            for(unsigned i=0;i<pack.equipment.size();++i){
                const auto& binding=pack.equipment[i];AdmittedRow row{};
                if(!Bind(pack,banks,binding.bank,binding.index,binding.encoded,binding.rowSha256,true,i,digest,row,problem))return false;
                for(const auto& prior:candidate.equipment_)if(prior.encoded==row.encoded)return Fail(problem,AdmissionError::Duplicate,binding.bank,i);
                candidate.equipment_.push_back(row);
            }
            *this=std::move(candidate);return true;
        }catch(const std::bad_alloc&){return Fail(problem,AdmissionError::Capacity);}
         catch(const std::length_error&){return Fail(problem,AdmissionError::Capacity);}
    }
    void Clear() noexcept {commands_.clear();equipment_.clear();}
    std::size_t Size() const noexcept {return commands_.size();}
    const AdmittedRow* Command(unsigned id,const void* address,std::size_t size) const noexcept {
        return Match(commands_,id,address,address,size);
    }
    const AdmittedRow* Command(unsigned id,const void* address,const void* copy,std::size_t size) const noexcept {
        return Match(commands_,id,address,copy,size);
    }
    const AdmittedRow* Equipment(unsigned id,const void* address,const void* copy,std::size_t size) const noexcept {
        return Match(equipment_,id,address,copy,size);
    }
    const AdmittedRow* ExpectedCommand(unsigned id) const noexcept {
        for(const auto& row:commands_)if(row.encoded==id)return &row;
        return nullptr;
    }
    const AdmittedRow* ExpectedEquipment(unsigned id) const noexcept {
        for(const auto& row:equipment_)if(row.encoded==id)return &row;
        return nullptr;
    }
private:
    static unsigned Word(const unsigned char* p) noexcept {return p[0]|(unsigned(p[1])<<8);}
    static std::uint32_t Dword(const unsigned char* p) noexcept {
        return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
    }
    static bool Fail(AdmissionProblem& out,AdmissionError code,unsigned bank=0,unsigned binding=0) noexcept {
        out={code,bank,binding};return false;
    }
    static bool Layout(const PackBank& bank,const unsigned char* data,std::size_t size) noexcept {
        if(bank.sections.empty()||bank.sections.size()>16||size<8+12*bank.sections.size()||Word(data)!=bank.sections.size())return false;
        for(unsigned i=0;i<bank.sections.size();++i){
            const auto& section=bank.sections[i];const auto* header=data+8+12*i;
            if(section.last<section.first||section.last>4095||section.width!=RowWidth(bank.kind)||
               section.offset<8+12*bank.sections.size()||section.offset>size)return false;
            const std::uint64_t length=std::uint64_t(section.last-section.first+1)*section.width;
            if(length>65535||length>size-section.offset||Word(header)!=section.first||Word(header+2)!=section.last||
               Word(header+4)!=section.width||Word(header+6)!=length||Dword(header+8)!=section.offset)return false;
            for(unsigned j=0;j<i;++j){
                const auto& previous=bank.sections[j];
                const auto end=std::uint64_t(previous.offset)+std::uint64_t(previous.last-previous.first+1)*previous.width;
                if(!(previous.last<section.first||section.last<previous.first)||
                   !(end<=section.offset||std::uint64_t(section.offset)+length<=previous.offset))return false;
            }
        }
        return true;
    }
    static bool Fingerprint(const unsigned char* data,std::size_t size,std::string_view expected,BankDigest hash) noexcept {
        if(!ValidFingerprint(expected))return false;
        std::array<unsigned char,32> digest{};if(!hash(data,size,digest))return false;
        constexpr char hex[]="0123456789abcdef";
        for(unsigned i=0;i<digest.size();++i)if(expected[i*2]!=hex[digest[i]>>4]||expected[i*2+1]!=hex[digest[i]&15])return false;
        return true;
    }
    static bool Bind(const Pack& pack,const std::array<const LoadedBank*,5>& loaded,unsigned bankIndex,
                     unsigned index,unsigned encoded,std::string_view hash,bool equipment,unsigned bindingIndex,
                     BankDigest digest,AdmittedRow& out,AdmissionProblem& problem) noexcept {
        if(bankIndex>=pack.banks.size()||index>4095)return Fail(problem,AdmissionError::Invalid,bankIndex,bindingIndex);
        const auto& bank=pack.banks[bankIndex];const auto* bytes=loaded[bankIndex];
        const auto offset=bank.Row(index),width=RowWidth(bank.kind);
        if(!bytes||((bank.kind==BankKind::AutoAbility)!=equipment)||encoded!=((static_cast<unsigned>(bank.kind)<<12)|index)||
           offset==InvalidRow||offset>bytes->size||width>bytes->size-offset||width>out.row.size())
            return Fail(problem,AdmissionError::Layout,bankIndex,bindingIndex);
        const auto* address=bytes->data+offset;
        if(!Fingerprint(address,width,hash,digest))return Fail(problem,AdmissionError::RowHash,bankIndex,bindingIndex);
        out.encoded=encoded;out.bindingIndex=bindingIndex;out.bankIndex=bankIndex;out.width=width;
        out.address=bytes->identity?bytes->identity+offset:address;
        std::memcpy(out.row.data(),address,width);return true;
    }
    static const AdmittedRow* Match(const std::vector<AdmittedRow>& entries,unsigned id,
                                   const void* address,const void* bytes,std::size_t size) noexcept {
        if(!address||!bytes)return nullptr;
        for(const auto& row:entries)if(row.encoded==id&&row.address==address&&size>=row.width)
            return std::memcmp(bytes,row.row.data(),row.width)==0?&row:nullptr;
        return nullptr;
    }
    std::vector<AdmittedRow> commands_,equipment_;
};
} // namespace FfxHooks::ElementalDominion
