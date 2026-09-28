#include "ArcanaStore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace FfxHooks::Arcana {
namespace {
constexpr unsigned char magic[8]={'A','R','C','A','N','A','0','1'};
constexpr const char* finalSuffix=".arcana.v1";
constexpr const char* pendingSuffix=".arcana.pending.v1";
constexpr const char* previousSuffix=".arcana.previous.v1";
std::atomic<unsigned> temporarySequence{0};
void Put(unsigned char* p,std::uint64_t value,unsigned bytes) noexcept {
    for(unsigned i=0;i<bytes;++i)p[i]=static_cast<unsigned char>(value>>(i*8));
}
std::uint64_t Get(const unsigned char* p,unsigned bytes) noexcept {
    std::uint64_t value=0;for(unsigned i=0;i<bytes;++i)value|=std::uint64_t(p[i])<<(i*8);return value;
}
std::uint32_t Crc(const unsigned char* p,std::size_t size) noexcept {
    std::uint32_t crc=0xFFFFFFFFu;
    for(std::size_t i=0;i<size;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xEDB88320u&std::uint32_t(-static_cast<int>(crc&1)));}
    return ~crc;
}
bool Nonzero(const Hash& hash) noexcept {for(auto b:hash)if(b)return true;return false;}
bool ValidResources(const Resources& r) noexcept {
    if(r.valid&0x80)return false;
    for(unsigned i=0;i<kActorCount;++i)
        if(!(r.valid&(1u<<i))&&(r.hp[i]||r.mp[i]))return false;
    return true;
}
StoreCode ReadBytes(const std::filesystem::path& path,RecordBytes& bytes) {
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StoreCode::Missing:StoreCode::IoFailure;}
    BY_HANDLE_FILE_INFORMATION info{};DWORD read=0;
    const bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
        !info.nFileSizeHigh&&info.nFileSizeLow==bytes.size();
    const bool ok=valid&&ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();
    CloseHandle(file);return ok?StoreCode::Found:valid?StoreCode::IoFailure:StoreCode::Invalid;
#else
    const int file=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(file<0)return errno==ENOENT?StoreCode::Missing:StoreCode::IoFailure;
    struct stat st{};const bool valid=fstat(file,&st)==0&&S_ISREG(st.st_mode)&&st.st_size==static_cast<off_t>(bytes.size());
    std::size_t total=0;
    if(valid)while(total<bytes.size()){const auto n=read(file,bytes.data()+total,bytes.size()-total);if(n<=0)break;total+=static_cast<std::size_t>(n);}
    close(file);return valid&&total==bytes.size()?StoreCode::Found:valid?StoreCode::IoFailure:StoreCode::Invalid;
#endif
}
bool AtomicWrite(const std::filesystem::path& path,const RecordBytes& bytes) {
    // Refuse to overwrite a foreign/corrupt file just because it uses our suffix.
    RecordBytes old{};const auto existing=ReadBytes(path,old);Record decoded;
    if(existing!=StoreCode::Missing&&(existing!=StoreCode::Found||!Decode(old.data(),old.size(),decoded)))return false;
    auto temporary=path;temporary+=(".tmp-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+
        "-"+std::to_string(temporarySequence.fetch_add(1)));
#ifdef _WIN32
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary.c_str());
    return ok;
#else
    const int file=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(file<0)return false;
    std::size_t total=0;
    while(total<bytes.size()){const auto n=write(file,bytes.data()+total,bytes.size()-total);if(n<=0)break;total+=static_cast<std::size_t>(n);}
    bool ok=total==bytes.size()&&fsync(file)==0;
    if(close(file))ok=false;
    if(ok)ok=rename(temporary.c_str(),path.c_str())==0;
    if(!ok)unlink(temporary.c_str());
    if(ok){const int directory=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(directory<0)return false;ok=fsync(directory)==0;close(directory);}
    return ok;
#endif
}
}
bool Encode(const Record& record,RecordBytes& output) noexcept {
    if(Validate(record.state)!=Error::None||!Nonzero(record.nativeHash)||!Nonzero(record.packHash)||!ValidResources(record.resources))return false;
    RecordBytes result{};std::memcpy(result.data(),magic,8);Put(result.data()+8,1,4);Put(result.data()+12,result.size(),4);
    std::copy(record.nativeHash.begin(),record.nativeHash.end(),result.begin()+16);
    std::copy(record.packHash.begin(),record.packHash.end(),result.begin()+48);
    Put(result.data()+80,record.state.revision,8);result[88]=static_cast<unsigned char>(record.state.mode);result[89]=record.resources.valid;
    std::copy(record.state.acquired.begin(),record.state.acquired.end(),result.begin()+96);
    unsigned at=174;
    for(const auto& actor:record.state.slots)for(auto slot:actor){Put(result.data()+at,static_cast<std::uint16_t>(slot),2);at+=2;}
    for(auto hp:record.resources.hp){Put(result.data()+at,hp,4);at+=4;}
    for(auto mp:record.resources.mp){Put(result.data()+at,mp,4);at+=4;}
    Put(result.data()+272,Crc(result.data(),272),4);output=result;return true;
}
bool Decode(const unsigned char* bytes,std::size_t size,Record& output) noexcept {
    if(!bytes||size!=kRecordBytes||std::memcmp(bytes,magic,8)||Get(bytes+8,4)!=1||Get(bytes+12,4)!=size||Get(bytes+272,4)!=Crc(bytes,272))return false;
    for(unsigned i=90;i<96;++i)if(bytes[i])return false;
    Record result;std::copy(bytes+16,bytes+48,result.nativeHash.begin());std::copy(bytes+48,bytes+80,result.packHash.begin());
    result.state.revision=Get(bytes+80,8);result.state.mode=static_cast<Mode>(bytes[88]);result.resources.valid=bytes[89];
    std::copy(bytes+96,bytes+174,result.state.acquired.begin());unsigned at=174;
    for(auto& actor:result.state.slots)for(auto& slot:actor){
        const auto word=Get(bytes+at,2);at+=2;
        if(word!=0xFFFF&&word>=kCardCount)return false;
        slot=word==0xFFFF?kEmpty:static_cast<std::int16_t>(word);
    }
    for(auto& hp:result.resources.hp){hp=static_cast<std::uint32_t>(Get(bytes+at,4));at+=4;}
    for(auto& mp:result.resources.mp){mp=static_cast<std::uint32_t>(Get(bytes+at,4));at+=4;}
    if(Validate(result.state)!=Error::None||!Nonzero(result.nativeHash)||!Nonzero(result.packHash)||!ValidResources(result.resources))return false;
    output=result;return true;
}
std::filesystem::path Store::Extension(const std::filesystem::path& path,const char* suffix){auto result=path;result+=suffix;return result;}
StoreCode Store::Read(const std::filesystem::path& native,const Hash& hash,const Hash& pack,Record& out) const {
    try {
        bool present=false,invalid=false,ioFailure=false;
        for(const char* suffix:{finalSuffix,pendingSuffix,previousSuffix}){
            RecordBytes bytes{};const auto found=ReadBytes(Extension(native,suffix),bytes);
            if(found==StoreCode::Missing)continue;
            present=true;
            if(found==StoreCode::IoFailure){ioFailure=true;continue;}
            Record record;
            if(found!=StoreCode::Found||!Decode(bytes.data(),bytes.size(),record)){invalid=true;continue;}
            if(record.nativeHash==hash&&record.packHash==pack){out=record;return suffix==finalSuffix?StoreCode::Found:StoreCode::Recovered;}
        }
        return ioFailure?StoreCode::IoFailure:invalid?StoreCode::Invalid:present?StoreCode::Foreign:StoreCode::Missing;
    }catch(...){return StoreCode::IoFailure;}
}
bool Store::Prepare(const std::filesystem::path& native,const Record& record) const {
    try {RecordBytes bytes{};return Encode(record,bytes)&&AtomicWrite(Extension(native,pendingSuffix),bytes);}catch(...){return false;}
}
StoreCode Store::ReadCompatible(const std::filesystem::path& native,const Hash& hash,const Hash& pack,
                                const Hash* previous,std::size_t count,Record& out,bool& migrated) const {
    migrated=false;Record candidate;
    const auto current=Read(native,hash,pack,candidate);
    if(current==StoreCode::Found||current==StoreCode::Recovered){out=candidate;return current;}
    if(current==StoreCode::Missing||!previous||count>8)return current;
    for(std::size_t i=0;i<count;++i){
        const auto result=Read(native,hash,previous[i],candidate);
        if(result==StoreCode::Found||result==StoreCode::Recovered){
            // Only compiled, identity-preserving balance revisions are admitted.
            // Native hash, unique ownership, resource bounds and CRC still pass
            // the ordinary reader; no file is changed before a native save.
            candidate.packHash=pack;out=candidate;migrated=true;return StoreCode::Recovered;
        }
    }
    return current;
}
bool Store::Commit(const std::filesystem::path& native,const Hash& written) const {
    try {
        RecordBytes pending{};Record record;
        if(ReadBytes(Extension(native,pendingSuffix),pending)!=StoreCode::Found||!Decode(pending.data(),pending.size(),record)||record.nativeHash!=written)return false;
        RecordBytes previous{};const auto old=ReadBytes(Extension(native,finalSuffix),previous);
        Record previousRecord;
        if(old!=StoreCode::Missing&&(old!=StoreCode::Found||!Decode(previous.data(),previous.size(),previousRecord)||
            !AtomicWrite(Extension(native,previousSuffix),previous)))return false;
        if(!AtomicWrite(Extension(native,finalSuffix),pending))return false;
        std::error_code ignored;std::filesystem::remove(Extension(native,pendingSuffix),ignored);
        return true;
    }catch(...){return false;}
}
bool Store::Abort(const std::filesystem::path& native,const Record& prepared) const {
    try {
        RecordBytes expected{},actual{};
        if(!Encode(prepared,expected))return false;
        const auto path=Extension(native,pendingSuffix);
        const auto found=ReadBytes(path,actual);
        if(found==StoreCode::Missing)return true;
        if(found!=StoreCode::Found||actual!=expected)return false;
        std::error_code error;
        return std::filesystem::remove(path,error)&&!error;
    }catch(...){return false;}
}
}
