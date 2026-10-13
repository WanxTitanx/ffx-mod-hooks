#include "OmegaT7Store.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace FfxHooks::OmegaT7 {
namespace {
constexpr unsigned char magic[]={'O','M','E','G','A','T','7','1'};
std::atomic<unsigned> temporary{0};
void Put(unsigned char* p,std::uint64_t n,unsigned width) noexcept {for(unsigned i=0;i<width;++i)p[i]=static_cast<unsigned char>(n>>(i*8));}
std::uint64_t Get(const unsigned char* p,unsigned width) noexcept {std::uint64_t n=0;for(unsigned i=0;i<width;++i)n|=std::uint64_t(p[i])<<(i*8);return n;}
std::uint32_t Crc(const unsigned char* p,unsigned length) noexcept {
    std::uint32_t crc=0xFFFFFFFFu;for(unsigned i=0;i<length;++i){crc^=p[i];for(unsigned n=0;n<8;++n)crc=(crc>>1)^(0xEDB88320u&std::uint32_t(-static_cast<int>(crc&1)));}return ~crc;
}
bool Bound(const Hash& value) noexcept {for(auto byte:value)if(byte)return true;return false;}
Bytes Header(unsigned kind) noexcept {Bytes b{};std::copy(std::begin(magic),std::end(magic),b.begin());Put(b.data()+8,1,4);Put(b.data()+12,b.size(),4);Put(b.data()+16,kind,4);return b;}
bool ValidHeader(const Bytes& b,unsigned kind) noexcept {
    return !std::memcmp(b.data(),magic,8)&&Get(b.data()+8,4)==1&&Get(b.data()+12,4)==b.size()&&Get(b.data()+16,4)==kind&&
        !Get(b.data()+20,4)&&Get(b.data()+124,4)==Crc(b.data(),124);
}
StoreCode Read(const std::filesystem::path& path,Bytes& bytes){
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StoreCode::Missing:StoreCode::IoFailure;}
    BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;
    const bool shape=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&!info.nFileSizeHigh&&info.nFileSizeLow==bytes.size();
    const bool ok=shape&&ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();CloseHandle(file);
#else
    const int file=open(path.c_str(),O_RDONLY|O_NOFOLLOW);
    if(file<0)return errno==ENOENT?StoreCode::Missing:StoreCode::IoFailure;
    struct stat info{};const bool shape=fstat(file,&info)==0&&S_ISREG(info.st_mode)&&info.st_size==static_cast<off_t>(bytes.size());std::size_t count=0;
    if(shape)while(count<bytes.size()){auto n=read(file,bytes.data()+count,bytes.size()-count);if(n<=0)break;count+=static_cast<std::size_t>(n);}
    close(file);const bool ok=shape&&count==bytes.size();
#endif
    return ok?StoreCode::Found:shape?StoreCode::IoFailure:StoreCode::Invalid;
}
bool Owned(const Bytes& bytes){Reference reference;Campaign campaign;return Decode(bytes,reference)||Decode(bytes,campaign);}
bool Atomic(const std::filesystem::path& path,const Bytes& bytes){
    Bytes previous{};const auto code=Read(path,previous);
    if(!Owned(bytes)||(code!=StoreCode::Missing&&(code!=StoreCode::Found||!Owned(previous))))return false;
    auto tmp=path;tmp+=".tmp-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(temporary.fetch_add(1));
#ifdef _WIN32
    HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file);if(!CloseHandle(file))ok=false;
    Bytes now{};const auto current=Read(path,now);if(ok&&(current!=code||(code==StoreCode::Found&&now!=previous)))ok=false;
    if(ok)ok=MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(tmp.c_str());return ok;
#else
    const int file=open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(file<0)return false;
    std::size_t count=0;while(count<bytes.size()){const auto n=write(file,bytes.data()+count,bytes.size()-count);if(n<=0)break;count+=static_cast<std::size_t>(n);}
    bool ok=count==bytes.size()&&fsync(file)==0;if(close(file))ok=false;
    Bytes now{};const auto current=Read(path,now);if(ok&&(current!=code||(code==StoreCode::Found&&now!=previous)))ok=false;
    if(ok)ok=rename(tmp.c_str(),path.c_str())==0;
    if(!ok)unlink(tmp.c_str());
    if(ok){const int directory=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(directory<0)return false;ok=fsync(directory)==0;close(directory);}return ok;
#endif
}
}
bool Encode(const Reference& r,Bytes& out) noexcept {
    if(!Bound(r.native)||!Bound(r.campaign))return false;
    auto b=Header(1);std::copy(r.campaign.begin(),r.campaign.end(),b.begin()+24);std::copy(r.native.begin(),r.native.end(),b.begin()+56);
    Put(b.data()+124,Crc(b.data(),124),4);out=b;return true;
}
bool Encode(const Campaign& r,Bytes& out) noexcept {
    if(!Bound(r.id)||!Seed::Valid(r.seed)||!r.revision)return false;
    auto b=Header(2);std::copy(r.id.begin(),r.id.end(),b.begin()+24);Put(b.data()+88,r.seed.seed,8);Put(b.data()+96,r.seed.epoch,8);
    Put(b.data()+104,r.seed.duration,4);Put(b.data()+108,r.seed.elapsed,4);Put(b.data()+112,(r.seed.due?1:0)|(r.seed.loreSeen?2:0)|(r.seed.renewalPending?4:0),4);
    Put(b.data()+116,r.revision,8);Put(b.data()+124,Crc(b.data(),124),4);out=b;return true;
}
bool Decode(const Bytes& b,Reference& out) noexcept {
    if(!ValidHeader(b,1))return false;
    for(unsigned i=88;i<124;++i)if(b[i])return false;
    Reference r;std::copy(b.begin()+24,b.begin()+56,r.campaign.begin());std::copy(b.begin()+56,b.begin()+88,r.native.begin());
    if(!Bound(r.native)||!Bound(r.campaign))return false;
    out=r;return true;
}
bool Decode(const Bytes& b,Campaign& out) noexcept {
    if(!ValidHeader(b,2)||Get(b.data()+112,4)>7)return false;
    for(unsigned i=56;i<88;++i)if(b[i])return false;
    Campaign r;std::copy(b.begin()+24,b.begin()+56,r.id.begin());r.seed.seed=Get(b.data()+88,8);r.seed.epoch=Get(b.data()+96,8);
    r.seed.duration=static_cast<unsigned>(Get(b.data()+104,4));r.seed.elapsed=static_cast<unsigned>(Get(b.data()+108,4));
    r.seed.due=(b[112]&1)!=0;r.seed.loreSeen=(b[112]&2)!=0;r.seed.renewalPending=(b[112]&4)!=0;r.revision=Get(b.data()+116,8);
    if(!Bound(r.id)||!r.revision||!Seed::Valid(r.seed))return false;
    out=r;return true;
}
Campaign AdoptCampaign(const Hash& id) noexcept {
    Campaign r;r.id=id;std::uint64_t entropy=0;for(unsigned i=0;i<32;i+=8)entropy=Mix(entropy^Get(id.data()+i,8));r.seed=Seed::Adopt(entropy);r.revision=1;return r;
}
std::filesystem::path Store::CampaignPath(const Hash& hash) const {
    constexpr char hex[]="0123456789abcdef";std::string name;for(auto b:hash){name+=hex[b>>4];name+=hex[b&15];}return root_/(name+".omega-t7.v1");
}
std::filesystem::path Store::ReferencePath(const std::filesystem::path& path,const char* suffix){auto out=path;out+=suffix;return out;}
StoreCode Store::ReadCampaign(const Hash& id,Campaign& out) const {
    try{Bytes b{};const auto code=Read(CampaignPath(id),b);if(code!=StoreCode::Found)return code;
        Campaign r;if(!Decode(b,r))return StoreCode::Invalid;if(r.id!=id)return StoreCode::Foreign;out=r;return StoreCode::Found;
    }catch(...){return StoreCode::IoFailure;}
}
bool Store::SaveCampaign(const Campaign& record) const {
    try{Bytes bytes{};if(root_.empty()||!Encode(record,bytes))return false;Campaign old;const auto code=ReadCampaign(record.id,old);
        if(code!=StoreCode::Missing&&code!=StoreCode::Found)return false;
        if(code==StoreCode::Found){Bytes previous{};Encode(old,previous);if(old.revision>=record.revision)return previous==bytes;}
        std::filesystem::create_directories(root_);return Atomic(CampaignPath(record.id),bytes);
    }catch(...){return false;}
}
StoreCode Store::ReadReference(const std::filesystem::path& path,const Hash& hash,Reference& out) const {
    try{bool present=false;StoreCode failure=StoreCode::Foreign;
        for(const char* suffix:{".omega-t7.v1",".omega-t7.pending.v1",".omega-t7.previous.v1"}){
            Bytes b{};const auto code=Read(ReferencePath(path,suffix),b);if(code==StoreCode::Missing)continue;present=true;
            if(code!=StoreCode::Found){failure=code;continue;}Reference r;if(!Decode(b,r)){failure=StoreCode::Invalid;continue;}
            if(r.native==hash){out=r;return suffix==std::string(".omega-t7.v1")?StoreCode::Found:StoreCode::Recovered;}
        }return present?failure:StoreCode::Missing;
    }catch(...){return StoreCode::IoFailure;}
}
bool Store::AdoptReference(const std::filesystem::path& path,const Reference& reference) const {
    try{Bytes b{},old{};if(!Encode(reference,b))return false;const auto target=ReferencePath(path);const auto code=Read(target,old);
        return code==StoreCode::Found?old==b:code==StoreCode::Missing&&Atomic(target,b);
    }catch(...){return false;}
}
bool Store::PrepareReference(const std::filesystem::path& path,const Reference& reference) const {
    try{Bytes b{};return Encode(reference,b)&&Atomic(ReferencePath(path,".omega-t7.pending.v1"),b);}catch(...){return false;}
}
bool Store::CommitReference(const std::filesystem::path& path,const Hash& hash) const {
    try{Bytes pending{},old{};Reference reference;const auto file=ReferencePath(path,".omega-t7.pending.v1");
        if(Read(file,pending)!=StoreCode::Found||!Decode(pending,reference)||reference.native!=hash)return false;
        Campaign campaign;if(ReadCampaign(reference.campaign,campaign)!=StoreCode::Found)return false;
        const auto target=ReferencePath(path);const auto code=Read(target,old);
        if(code!=StoreCode::Missing&&(code!=StoreCode::Found||!Owned(old)||!Atomic(ReferencePath(path,".omega-t7.previous.v1"),old)))return false;
        if(!Atomic(target,pending))return false;
        Bytes current{};if(Read(file,current)==StoreCode::Found&&current==pending){std::error_code error;std::filesystem::remove(file,error);}return true;
    }catch(...){return false;}
}
bool Store::AbortReference(const std::filesystem::path& path,const Reference& reference) const {
    try{Bytes wanted{},actual{};const auto file=ReferencePath(path,".omega-t7.pending.v1");
        if(!Encode(reference,wanted)||Read(file,actual)!=StoreCode::Found||actual!=wanted)return false;
        std::error_code error;return std::filesystem::remove(file,error)&&!error;
    }catch(...){return false;}
}
}
