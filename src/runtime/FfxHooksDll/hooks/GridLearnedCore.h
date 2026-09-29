#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::GridLearned {
using Hash=std::array<std::uint8_t,32>;
inline constexpr unsigned kCharacters=7,kWords=18,kFirstCommand=96,kLastCommand=383;
using Learned=std::array<std::array<std::uint16_t,kWords>,kCharacters>;
struct Identity {
    Hash path{},image{};
    bool Valid() const noexcept {
        std::uint8_t p=0,i=0;
        for(auto x:path)p|=x;
        for(auto x:image)i|=x;
        return p!=0&&i!=0;
    }
    bool operator==(const Identity& other) const noexcept {return path==other.path&&image==other.image;}
};
enum class Change : std::uint8_t {Changed,Unchanged,Invalid,Unbound};

// No file I/O, global party writes or native pointers live in this state. The
// runtime serializes it on the owner thread and binds it only after actual load.
class State {
public:
    bool Bind(const Identity& identity,const Learned* learned=nullptr) noexcept {
        Clear();
        if(!identity.Valid())return false;
        identity_=identity;
        if(learned)learned_=*learned;
        ready_=true;return true;
    }
    void Clear() noexcept {ready_=false;identity_={};learned_={};}
    void BeginSession() noexcept {Clear();ready_=true;}
    bool Ready() const noexcept {return ready_;}
    const Identity& Key() const noexcept {return identity_;}
    const Learned& Words() const noexcept {return learned_;}
    static bool ValidCommand(unsigned character,unsigned command) noexcept {
        return character<kCharacters&&command>=kFirstCommand&&command<=kLastCommand;
    }
    bool Has(unsigned character,unsigned command) const noexcept {
        if(!ready_||!ValidCommand(character,command))return false;
        const unsigned index=command-kFirstCommand;
        return (learned_[character][index/16]&(1u<<(index%16)))!=0;
    }
    Change Set(unsigned character,unsigned command,bool on) noexcept {
        if(!ValidCommand(character,command))return Change::Invalid;
        if(!ready_)return Change::Unbound;
        const unsigned index=command-kFirstCommand;
        auto& word=learned_[character][index/16];
        const auto mask=static_cast<std::uint16_t>(1u<<(index%16));
        const auto next=static_cast<std::uint16_t>(on?word|mask:word&~mask);
        if(next==word)return Change::Unchanged;
        word=next;return Change::Changed;
    }
private:
    Identity identity_{};
    Learned learned_{};
    bool ready_=false;
};

// Byte-defined little-endian wire format, independent of C++ padding/alignment.
// Full path+image SHA-256 identities prevent accidental slot/content crossover;
// CRC detects torn/corrupt metadata, not malicious modification.
inline constexpr std::size_t kRecordBytes=16+64+kCharacters*kWords*2+4;
using Record=std::array<std::uint8_t,kRecordBytes>;
inline std::uint32_t Crc32(const std::uint8_t* data,std::size_t size) noexcept {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<size;++i){crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc&1)));}
    return ~crc;
}
inline void Put32(std::uint8_t* out,std::uint32_t value) noexcept {
    for(unsigned i=0;i<4;++i)out[i]=static_cast<std::uint8_t>(value>>(8*i));
}
inline std::uint32_t Get32(const std::uint8_t* in) noexcept {
    return std::uint32_t(in[0])|(std::uint32_t(in[1])<<8)|(std::uint32_t(in[2])<<16)|(std::uint32_t(in[3])<<24);
}
inline bool Encode(const State& state,Record& out) noexcept {
    out={};if(!state.Ready()||!state.Key().Valid())return false;
    constexpr std::uint8_t magic[8]={'F','F','X','G','L','R','2',0};
    std::memcpy(out.data(),magic,8);Put32(out.data()+8,2);Put32(out.data()+12,kCharacters*kWords*2);
    std::memcpy(out.data()+16,state.Key().path.data(),32);
    std::memcpy(out.data()+48,state.Key().image.data(),32);
    std::size_t at=80;
    for(const auto& character:state.Words())for(const auto word:character){out[at++]=static_cast<std::uint8_t>(word);out[at++]=static_cast<std::uint8_t>(word>>8);}
    Put32(out.data()+at,Crc32(out.data(),at));return true;
}
inline bool Decode(const Record& record,const Identity& expected,Learned& out) noexcept {
    out={};constexpr std::uint8_t magic[8]={'F','F','X','G','L','R','2',0};
    if(!expected.Valid()||std::memcmp(record.data(),magic,8)!=0||Get32(record.data()+8)!=2||
       Get32(record.data()+12)!=kCharacters*kWords*2||
       std::memcmp(record.data()+16,expected.path.data(),32)!=0||
       std::memcmp(record.data()+48,expected.image.data(),32)!=0||
       Get32(record.data()+kRecordBytes-4)!=Crc32(record.data(),kRecordBytes-4))return false;
    std::size_t at=80;
    for(auto& character:out)for(auto& word:character){word=static_cast<std::uint16_t>(record[at]|(unsigned(record[at+1])<<8));at+=2;}
    return true;
}
} // namespace FfxHooks::GridLearned
