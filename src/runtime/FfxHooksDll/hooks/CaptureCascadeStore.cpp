#include "CaptureCascadeStore.h"
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
namespace FfxHooks::CaptureCascade {
namespace {
constexpr unsigned char magic[]={'C','A','S','C','A','D','E','1'};
constexpr const char* finalSuffix=".cascade.v1";
constexpr const char* pendingSuffix=".cascade.pending.v1";
constexpr const char* previousSuffix=".cascade.previous.v1";
std::atomic<unsigned> temporarySequence{0};
void Put(unsigned char* p,std::uint64_t value,unsigned size) noexcept {for(unsigned i=0;i<size;++i)p[i]=static_cast<unsigned char>(value>>(i*8));}
std::uint64_t Get(const unsigned char* p,unsigned size) noexcept {std::uint64_t value=0;for(unsigned i=0;i<size;++i)value|=std::uint64_t(p[i])<<(i*8);return value;}
std::uint32_t Crc(const unsigned char* p,std::size_t size) noexcept {
    std::uint32_t value=0xFFFFFFFFu;
    for(std::size_t i=0;i<size;++i){value^=p[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xEDB88320u&std::uint32_t(-static_cast<int>(value&1)));}
    return ~value;
}
bool Bound(const Hash& hash) noexcept {for(auto byte:hash)if(byte)return true;return false;}
StoreCode ReadBytes(const std::filesystem::path& path,RecordBytes& bytes) {
#ifdef _WIN32
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StoreCode::Missing:StoreCode::IoFailure;}
    BY_HANDLE_FILE_INFORMATION info{};DWORD size=0;
    const bool shape=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
        !info.nFileSizeHigh&&info.nFileSizeLow==bytes.size();
    const bool ok=shape&&ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&size,nullptr)&&size==bytes.size();
    CloseHandle(file);return ok?StoreCode::Found:shape?StoreCode::IoFailure:StoreCode::Invalid;
#else
    const int file=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(file<0)return errno==ENOENT?StoreCode::Missing:StoreCode::IoFailure;
    struct stat st{};const bool shape=fstat(file,&st)==0&&S_ISREG(st.st_mode)&&st.st_size==static_cast<off_t>(bytes.size());
    std::size_t total=0;
    if(shape)while(total<bytes.size()){const auto n=read(file,bytes.data()+total,bytes.size()-total);if(n<=0)break;total+=static_cast<std::size_t>(n);}
    close(file);return shape&&total==bytes.size()?StoreCode::Found:shape?StoreCode::IoFailure:StoreCode::Invalid;
#endif
}
// Same owned-file durability policy as ArcanaStore: bounded decoding, no reparse
// targets, private temporary files, fsync/flush, then an atomic replacement.
bool AtomicWrite(const std::filesystem::path& path,const RecordBytes& bytes) {
    Record decoded;RecordBytes previous{};const auto code=ReadBytes(path,previous);
    if(!Decode(bytes.data(),bytes.size(),decoded)||
       (code!=StoreCode::Missing&&(code!=StoreCode::Found||!Decode(previous.data(),previous.size(),decoded))))return false;
    auto temporary=path;temporary+=".tmp-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(temporarySequence.fetch_add(1));
#ifdef _WIN32
    const auto file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
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
    bool ok=total==bytes.size()&&fsync(file)==0;if(close(file))ok=false;
    if(ok)ok=rename(temporary.c_str(),path.c_str())==0;
    if(!ok)unlink(temporary.c_str());
    if(ok){const int directory=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(directory<0)return false;ok=fsync(directory)==0;close(directory);}
    return ok;
#endif
}
}
bool Encode(const Record& record,RecordBytes& output) noexcept {
    if(!Bound(record.nativeHash)||!Valid(record.state))return false;
    RecordBytes bytes{};std::copy(std::begin(magic),std::end(magic),bytes.begin());Put(bytes.data()+8,1,4);Put(bytes.data()+12,bytes.size(),4);
    std::copy(record.nativeHash.begin(),record.nativeHash.end(),bytes.begin()+16);
    const auto& s=record.state;
    bytes[48]=s.nativeSeen;bytes[49]=s.defeated;bytes[50]=s.captured;bytes[51]=s.regionPending;bytes[52]=s.regionSeen;bytes[53]=s.first;
    Put(bytes.data()+56,s.sequence,8);Put(bytes.data()+64,s.encounters,8);Put(bytes.data()+72,s.revision,8);
    Put(bytes.data()+80,s.sin.queued,8);bytes[88]=s.sin.page;bytes[89]=s.sin.complete?1:0;
    Put(bytes.data()+96,s.cascade.queued,8);bytes[104]=s.cascade.page;bytes[105]=s.cascade.complete?1:0;
    unsigned at=112;
    for(const auto* stamps:{&s.defeats,&s.captures})for(const auto& stamp:*stamps){
        Put(bytes.data()+at,stamp.utc,8);Put(bytes.data()+at+8,stamp.playSeconds,4);
        std::memcpy(bytes.data()+at+12,stamp.build.data(),stamp.build.size());at+=44;
    }
    Put(bytes.data()+816,Crc(bytes.data(),816),4);output=bytes;return true;
}
bool Decode(const unsigned char* bytes,std::size_t size,Record& output) noexcept {
    if(!bytes||size!=kRecordBytes||std::memcmp(bytes,magic,8)||Get(bytes+8,4)!=1||Get(bytes+12,4)!=size||Get(bytes+816,4)!=Crc(bytes,816))return false;
    for(unsigned i:{54u,55u,90u,91u,92u,93u,94u,95u,106u,107u,108u,109u,110u,111u})if(bytes[i])return false;
    if(bytes[89]>1||bytes[105]>1)return false;
    Record result;std::copy(bytes+16,bytes+48,result.nativeHash.begin());auto& s=result.state;
    s.nativeSeen=bytes[48];s.defeated=bytes[49];s.captured=bytes[50];s.regionPending=bytes[51];s.regionSeen=bytes[52];s.first=bytes[53];
    s.sequence=Get(bytes+56,8);s.encounters=Get(bytes+64,8);s.revision=Get(bytes+72,8);
    s.sin={Get(bytes+80,8),bytes[88],bytes[89]!=0};s.cascade={Get(bytes+96,8),bytes[104],bytes[105]!=0};
    unsigned at=112;
    for(auto* stamps:{&s.defeats,&s.captures})for(auto& stamp:*stamps){
        stamp.utc=Get(bytes+at,8);stamp.playSeconds=static_cast<std::uint32_t>(Get(bytes+at+8,4));
        std::memcpy(stamp.build.data(),bytes+at+12,stamp.build.size());at+=44;
    }
    if(!Bound(result.nativeHash)||!Valid(s))return false;
    output=result;return true;
}
StoreCode Store::Read(const std::filesystem::path& native,const Hash& hash,Record& output) const {
    try{
        if(!Bound(hash))return StoreCode::Foreign;
        bool present=false,invalid=false,io=false;
        for(const auto* suffix:{finalSuffix,pendingSuffix,previousSuffix}){
            RecordBytes bytes{};const auto code=ReadBytes(Extension(native,suffix),bytes);
            if(code==StoreCode::Missing)continue;
            present=true;if(code==StoreCode::IoFailure){io=true;continue;}
            Record record;if(code!=StoreCode::Found||!Decode(bytes.data(),bytes.size(),record)){invalid=true;continue;}
            if(record.nativeHash==hash){output=record;return suffix==finalSuffix?StoreCode::Found:StoreCode::Recovered;}
        }
        return io?StoreCode::IoFailure:invalid?StoreCode::Invalid:present?StoreCode::Foreign:StoreCode::Missing;
    }catch(...){return StoreCode::IoFailure;}
}
bool Store::Prepare(const std::filesystem::path& native,const Record& record) const {
    try{RecordBytes bytes{};return Encode(record,bytes)&&AtomicWrite(Extension(native,pendingSuffix),bytes);}catch(...){return false;}
}
bool Store::Commit(const std::filesystem::path& native,const Hash& hash) const {
    try{
        RecordBytes pending{},previous{};Record record;
        const auto path=Extension(native,pendingSuffix);
        if(ReadBytes(path,pending)!=StoreCode::Found||!Decode(pending.data(),pending.size(),record)||record.nativeHash!=hash)return false;
        const auto final=Extension(native,finalSuffix);const auto code=ReadBytes(final,previous);
        if(code!=StoreCode::Missing){
            if(code!=StoreCode::Found||!Decode(previous.data(),previous.size(),record)||!AtomicWrite(Extension(native,previousSuffix),previous))return false;
        }
        if(!AtomicWrite(final,pending))return false;
        RecordBytes now{};
        if(ReadBytes(path,now)==StoreCode::Found&&now==pending){std::error_code error;std::filesystem::remove(path,error);}
        return true;
    }catch(...){return false;}
}
bool Store::Abort(const std::filesystem::path& native,const Record& record) const {
    try{
        RecordBytes expected{},stored{};if(!Encode(record,expected))return false;
        const auto path=Extension(native,pendingSuffix);
        if(ReadBytes(path,stored)!=StoreCode::Found||stored!=expected)return false;
        std::error_code error;return std::filesystem::remove(path,error)&&!error;
    }catch(...){return false;}
}
std::filesystem::path Store::Extension(const std::filesystem::path& path,const char* suffix){auto result=path;result+=suffix;return result;}
}
