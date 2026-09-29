#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace FfxHooks::NulWard {
inline constexpr std::uint8_t kHoly=0x10,kDark=0x80,kElements=kHoly|kDark;
struct Actor {
    unsigned slot=64;
    std::uintptr_t pointer=0;
    std::uint32_t identity=0;
    bool Valid() const noexcept {return slot<64&&pointer!=0;}
    bool operator==(const Actor& other) const noexcept {
        return slot==other.slot&&pointer==other.pointer&&identity==other.identity;
    }
};
class Bank {
public:
    void Clear() noexcept {generation_=0;entries_={};}
    bool Cast(std::uint64_t generation,const Actor& actor,std::uint8_t elements) noexcept {
        if(!Admit(generation,actor)||!elements||(elements&~kElements))return false;
        auto& entry=entries_[actor.slot];
        if(!(entry.actor==actor))entry={actor,0};
        entry.blocks=static_cast<std::uint8_t>(entry.blocks|elements);return true;
    }
    std::uint8_t Blocks(std::uint64_t generation,const Actor& actor) noexcept {
        if(!Admit(generation,actor))return 0;
        auto& entry=entries_[actor.slot];
        if(!(entry.actor==actor)){entry={};return 0;}
        return entry.blocks;
    }
    std::int32_t Filter(std::uint64_t generation,const Actor& actor,std::uint32_t elements,
                        std::int32_t damage,unsigned component,bool apply) noexcept {
        // HP-only, damaging hits. Observe-only never changes a ward or native output.
        if(!apply||component!=0||damage<=0)return damage;
        const auto matching=static_cast<std::uint8_t>(Blocks(generation,actor)&elements&kElements);
        if(!matching)return damage;
        // A mixed hit consumes each matching available protection, never an
        // unrelated one. Multiple matching bits still block one HP component.
        entries_[actor.slot].blocks=static_cast<std::uint8_t>(entries_[actor.slot].blocks&~matching);
        return 0;
    }
private:
    bool Admit(std::uint64_t generation,const Actor& actor) noexcept {
        if(!generation||generation<generation_||!actor.Valid())return false;
        if(generation!=generation_){entries_={};generation_=generation;}
        return true;
    }
    struct Entry {Actor actor{};std::uint8_t blocks=0;};
    std::array<Entry,64> entries_{};
    std::uint64_t generation_=0;
};

struct Gateway {
    std::array<std::uint8_t,192> bytes{};
    std::size_t size=0;
};
// Native frame: [EBP-78] is remaining HP/MP/CTB components, [EBP+1C]
// encoded command, [EBP+2C] element mask. Saved ECX points to the current
// target component; only remaining==3 may interpret ECX-6E4 as the actor.
// Callback: int cdecl(damage, componentPointer, command, elements, remaining).
inline Gateway BuildWritebackGateway(std::uint32_t gatewayAddress,std::uint32_t callback,
                                     std::uint32_t originalPointerAddress) noexcept {
    Gateway out{};
    if(!gatewayAddress||!callback||!originalPointerAddress)return out;
    const auto emit=[&](std::initializer_list<std::uint8_t> values){for(auto v:values)out.bytes[out.size++]=v;};
    const auto dword=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)out.bytes[out.size++]=static_cast<std::uint8_t>(value>>(8*i));};
    emit({0x9c,0x60,0x8b,0xd4});                         // pushfd; pushad; mov edx,esp
    emit({0x81,0xec,0x20,0x02,0,0,0x83,0xe4,0xf0});      // private aligned stack
    emit({0x89,0x94,0x24,0x00,0x02,0,0});                // saved GPR-frame pointer
    emit({0x0f,0xae,0x04,0x24,0xdb,0xe3,0xfc});          // fxsave; fninit; cld
    emit({0xc7,0x84,0x24,0x04,0x02,0,0,0x80,0x1f,0,0});
    emit({0x0f,0xae,0x94,0x24,0x04,0x02,0,0});           // default MXCSR for C++
    emit({0xff,0x75,0x88,0xff,0x75,0x2c,0xff,0x75,0x1c});
    emit({0xff,0x72,0x18,0xff,0x72,0x1c,0xe8});          // saved ECX; saved EAX; call
    const auto afterCall=gatewayAddress+static_cast<std::uint32_t>(out.size)+4;
    dword(callback-afterCall);
    emit({0x83,0xc4,0x14,0x8b,0x94,0x24,0,0x02,0,0});
    emit({0x89,0x42,0x1c});                             // callback result replaces saved EAX only
    emit({0x0f,0xae,0x0c,0x24});                         // restore x87/SSE/MXCSR
    emit({0x8b,0xa4,0x24,0,0x02,0,0,0x61,0x9d});       // restore original stack, GPRs, flags
    emit({0xff,0x25});dword(originalPointerAddress);     // retained MinHook trampoline
    return out;
}
} // namespace FfxHooks::NulWard
