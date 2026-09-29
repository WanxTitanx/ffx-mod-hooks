#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace FfxHooks::FieldScout {

enum class RememberResult : std::uint8_t {
    Inserted, Duplicate, Full, TooLong, Invalid, Unavailable
};

// Caller owns synchronization. All storage is reserved before capture admission;
// Remember/Contains allocate nothing and always terminate within the table bound.
class BoundedDedupe {
public:
    static constexpr std::size_t kMaximumEntries=16384;
    static constexpr std::size_t kKeyBytes=384;

    bool Initialize(std::size_t requested) noexcept {
        if(entries_ || requested==0)return false;
        const std::size_t capacity=std::min(requested,kMaximumEntries);
        std::size_t buckets=2;
        while(buckets<capacity*2)buckets*=2;
        std::unique_ptr<Entry[]> entries(new(std::nothrow) Entry[capacity]);
        std::unique_ptr<std::uint32_t[]> slots(new(std::nothrow) std::uint32_t[buckets]{});
        if(!entries || !slots)return false;
        entries_=std::move(entries);slots_=std::move(slots);
        capacity_=capacity;bucketCount_=buckets;count_=0;
        return true;
    }
    void Reset() noexcept {
        if(slots_)std::memset(slots_.get(),0,bucketCount_*sizeof(std::uint32_t));
        count_=0;
    }
    void Release() noexcept {
        entries_.reset();slots_.reset();count_=capacity_=bucketCount_=0;
    }
    std::size_t Size() const noexcept {return count_;}
    std::size_t Capacity() const noexcept {return capacity_;}
    std::size_t AllocatedBytes() const noexcept {
        return capacity_*sizeof(Entry)+bucketCount_*sizeof(std::uint32_t);
    }
    bool Contains(const char* key) const noexcept {
        if(!entries_)return false;
        Encoded encoded{};
        if(Encode(key,&encoded)!=RememberResult::Inserted)return false;
        bool found=false;
        Find(encoded,&found);
        return found;
    }
    RememberResult Remember(const char* key) noexcept {
        if(!entries_)return RememberResult::Unavailable;
        Encoded encoded{};
        const auto valid=Encode(key,&encoded);
        if(valid!=RememberResult::Inserted)return valid;
        bool found=false;
        const std::size_t slot=Find(encoded,&found);
        if(found)return RememberResult::Duplicate;
        if(count_==capacity_ || slot==bucketCount_)return RememberResult::Full;
        auto& entry=entries_[count_];
        entry.hash=encoded.hash;entry.length=encoded.length;
        std::memcpy(entry.key,encoded.key,encoded.length+1);
        slots_[slot]=static_cast<std::uint32_t>(++count_);
        return RememberResult::Inserted;
    }

private:
    struct Entry {
        std::uint64_t hash;
        std::uint16_t length;
        char key[kKeyBytes];
    };
    using Encoded=Entry;
    static RememberResult Encode(const char* key,Encoded* out) noexcept {
        if(!key || !out)return RememberResult::Invalid;
        std::uint64_t hash=14695981039346656037ull;
        for(std::size_t n=0;n<kKeyBytes;++n) {
            unsigned char value=static_cast<unsigned char>(key[n]);
            if(value==0) {
                if(n==0)return RememberResult::Invalid;
                out->key[n]=0;out->length=static_cast<std::uint16_t>(n);out->hash=hash;
                return RememberResult::Inserted;
            }
            if(n==kKeyBytes-1)return RememberResult::TooLong;
            // Asset keys are ASCII paths and numeric metadata. Non-ASCII bytes
            // remain exact; locale-dependent case conversion cannot merge them.
            if(value>='A' && value<='Z')value=static_cast<unsigned char>(value+'a'-'A');
            out->key[n]=static_cast<char>(value);
            hash=(hash^value)*1099511628211ull;
        }
        return RememberResult::TooLong;
    }
    std::size_t Find(const Encoded& key,bool* found) const noexcept {
        *found=false;
        const std::size_t mask=bucketCount_-1;
        std::size_t slot=static_cast<std::size_t>(key.hash)&mask;
        for(std::size_t probe=0;probe<bucketCount_;++probe,slot=(slot+1)&mask) {
            const std::uint32_t index=slots_[slot];
            if(index==0)return slot;
            const auto& entry=entries_[index-1];
            if(entry.hash==key.hash && entry.length==key.length &&
               std::memcmp(entry.key,key.key,key.length)==0) {
                *found=true;return slot;
            }
        }
        return bucketCount_;
    }
    std::unique_ptr<Entry[]> entries_;
    std::unique_ptr<std::uint32_t[]> slots_;
    std::size_t count_=0,capacity_=0,bucketCount_=0;
};
} // namespace FfxHooks::FieldScout
