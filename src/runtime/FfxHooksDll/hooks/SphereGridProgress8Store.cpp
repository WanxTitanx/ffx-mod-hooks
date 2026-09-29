#include "SphereGridProgress8Store.h"
#include <utility>

namespace FfxHooks::SphereGridProgress8Disk {
namespace {
namespace Codec=SphereGridProgress8;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){Close();}
    bool Close() noexcept {
        if(value==INVALID_HANDLE_VALUE)return true;
        const HANDLE closing=value;value=INVALID_HANDLE_VALUE;
        return CloseHandle(closing)!=FALSE;
    }
};
struct Temporary {
    std::wstring path;
    Handle handle;
    bool published=false;
    ~Temporary(){
        handle.Close();
        // CREATE_NEW established ownership of this temporary leaf. This is not
        // a cleanup scan and never removes files from an earlier process.
        if(!published&&!path.empty())DeleteFileW(path.c_str());
    }
};
std::atomic<std::uint64_t> temporarySerial{0};
std::uint64_t NextTemporary() noexcept {
    auto previous=temporarySerial.load(std::memory_order_acquire);
    while(previous!=UINT64_MAX){
        if(temporarySerial.compare_exchange_weak(previous,previous+1,std::memory_order_acq_rel,
                                                  std::memory_order_acquire))return previous+1;
    }
    return 0;
}
bool SameDirectory(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b) noexcept {
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&
        a.nFileIndexLow==b.nFileIndexLow&&(b.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
        !(b.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
HANDLE OpenDirectory(const std::wstring& directory) noexcept {
    // Do not share DELETE: a live store pins the directory instead of relying
    // solely on a pathname that can later refer to another filesystem object.
    // Attribute-only access is exempt from normal sharing checks. Include the
    // directory data right so denial of DELETE actually prevents path recycling.
    return CreateFileW(directory.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
}
bool Normalize(const std::wstring& input,std::wstring& output){
    if(input.empty()||input.size()>4096||input.find(L'\0')!=std::wstring::npos)return false;
    wchar_t buffer[4097]{};
    const auto count=GetFullPathNameW(input.c_str(),4097,buffer,nullptr);
    if(!count||count>=4097)return false;
    std::wstring path(buffer,count);
    if(path.rfind(L"\\\\.\\",0)==0)return false;
    if(path.rfind(L"\\\\?\\",0)!=0){
        if(path.rfind(L"\\\\",0)==0)path=L"\\\\?\\UNC\\"+path.substr(2);
        else if(path.size()>3&&path[1]==L':'&&path[2]==L'\\')path=L"\\\\?\\"+path;
        else return false;
    }
    while(path.size()>7&&path.back()==L'\\')path.pop_back();
    output=std::move(path);return true;
}
std::wstring ShortHex(const SphereGridProgress::Hash& hash){
    constexpr wchar_t digits[]=L"0123456789abcdef";
    std::wstring output;output.reserve(32);
    for(std::size_t i=0;i<16;++i){output.push_back(digits[hash[i]>>4]);output.push_back(digits[hash[i]&15]);}
    return output;
}
}

Store::~Store(){
    if(writerHandle_!=INVALID_HANDLE_VALUE)CloseHandle(writerHandle_);
    if(directoryHandle_!=INVALID_HANDLE_VALUE)CloseHandle(directoryHandle_);
}

bool Store::Initialize(const std::wstring& directory,bool create) noexcept {
    try {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return false;
        std::wstring normalized;if(!Normalize(directory,normalized))return false;
        if(initialized_.load(std::memory_order_acquire))return directory_==normalized&&Healthy();
        if(create&&!CreateDirectoryW(normalized.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        Handle handle;handle.value=OpenDirectory(normalized);
        if(handle.value==INVALID_HANDLE_VALUE)return false;
        BY_HANDLE_FILE_INFORMATION information{};
        if(!GetFileInformationByHandle(handle.value,&information)||
           !(information.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
           (information.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||GetFileType(handle.value)!=FILE_TYPE_DISK)return false;
        // OPEN_ALWAYS leaves a stale empty lock leaf reusable after a crash.
        // The exclusive WRITE handle, not the file's existence, owns the root.
        // No native save or progress record is created during initialization.
        Handle writer;
        writer.value=CreateFileW((normalized+L"\\.sgp2-owner.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(writer.value==INVALID_HANDLE_VALUE)return false;
        BY_HANDLE_FILE_INFORMATION lockInfo{};LARGE_INTEGER lockSize{};
        if(!GetFileInformationByHandle(writer.value,&lockInfo)||!GetFileSizeEx(writer.value,&lockSize)||
           lockSize.QuadPart!=0||lockInfo.nNumberOfLinks!=1||GetFileType(writer.value)!=FILE_TYPE_DISK||
           (lockInfo.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
        directory_=std::move(normalized);identity_=information;
        directoryHandle_=handle.value;handle.value=INVALID_HANDLE_VALUE;
        writerHandle_=writer.value;writer.value=INVALID_HANDLE_VALUE;
        initialized_.store(true,std::memory_order_release);return true;
    }catch(...){return false;}
}

bool Store::Healthy() const noexcept {
    if(!initialized_.load(std::memory_order_acquire)||directoryHandle_==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION held{},current{};
    if(!GetFileInformationByHandle(directoryHandle_,&held)||!SameDirectory(identity_,held))return false;
    Handle check;check.value=OpenDirectory(directory_);
    return check.value!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(check.value,&current)&&
        SameDirectory(identity_,current);
}

std::wstring Store::RecordPath(const Key& key) const {
    if(!initialized_.load(std::memory_order_acquire)||!key.Valid())return {};
    // Four 128-bit filename components keep the leaf below NTFS's 255-character
    // limit. All four FULL 256-bit identities are still verified by the codec.
    return directory_+L"\\"+ShortHex(key.path)+L"-"+ShortHex(key.image)+L"-"+
        ShortHex(key.layout)+L"-"+ShortHex(key.contents)+L".sgp2";
}

ReadResult Store::ReadRaw(const std::wstring& path,Bytes& output) const noexcept {
    try {
        if(path.empty()||!Healthy())return ReadResult::Unavailable;
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return ReadResult::Invalid;
        Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file.value==INVALID_HANDLE_VALUE){
            const auto error=GetLastError();
            return error==ERROR_FILE_NOT_FOUND&&Healthy()?ReadResult::Missing:ReadResult::Unavailable;
        }
        BY_HANDLE_FILE_INFORMATION information{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(file.value,&information)||!GetFileSizeEx(file.value,&size))return ReadResult::Unavailable;
        if((information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
           information.nNumberOfLinks!=1||GetFileType(file.value)!=FILE_TYPE_DISK||
           size.QuadPart<static_cast<LONGLONG>(Codec::kHeader+2+Codec::kTail+Codec::kChecksum)||
           size.QuadPart>static_cast<LONGLONG>(Codec::kMaxBytes))return ReadResult::Invalid;
        Bytes bytes(static_cast<std::size_t>(size.QuadPart));DWORD count=0;
        if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)||count!=bytes.size())return ReadResult::Unavailable;
        if(!file.Close()||!Healthy())return ReadResult::Unavailable;
        output.swap(bytes);return ReadResult::Found;
    }catch(...){return ReadResult::Unavailable;}
}

ReadResult Store::Read(const Key& key,Snapshot& output) noexcept {
    try {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||!initialized_.load(std::memory_order_acquire))return ReadResult::Unavailable;
        if(!key.Valid())return ReadResult::Invalid;
        Bytes bytes;const auto result=ReadRaw(RecordPath(key),bytes);
        if(result!=ReadResult::Found)return result;
        return Codec::Decode(bytes,key,output)?ReadResult::Found:ReadResult::Invalid;
    }catch(...){return ReadResult::Unavailable;}
}

bool Store::Admitted(const Admission& admission,const Key& key) noexcept {
    try {return admission.current&&admission.current(admission.context,key);}
    catch(...){return false;}
}

WriteResult Store::Publish(const Key& key,const Snapshot& state,const Admission& admission) noexcept {
    try {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||!Healthy())return WriteResult::Unavailable;
        if(!key.Valid()||!state.Valid()||!Admitted(admission,key))return WriteResult::Rejected;
        Bytes encoded;if(!Codec::Encode(key,state,encoded))return WriteResult::Rejected;
        const auto path=RecordPath(key);Bytes previous;
        const auto prior=ReadRaw(path,previous);
        if(prior==ReadResult::Unavailable)return WriteResult::Unavailable;
        if(prior==ReadResult::Invalid)return WriteResult::Rejected;
        if(prior==ReadResult::Found){
            Snapshot ignored;if(!Codec::Decode(previous,key,ignored))return WriteResult::Rejected;
            if(previous==encoded)return Admitted(admission,key)?WriteResult::Unchanged:WriteResult::Rejected;
        }
        if(!Admitted(admission,key)||!Healthy())return WriteResult::Rejected;
        Temporary temporary;
        for(unsigned attempt=0;attempt<8;++attempt){
            const auto serial=NextTemporary();if(!serial)return WriteResult::Unavailable;
            auto candidate=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial);
            temporary.handle.value=CreateFileW(candidate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            // Ownership bookkeeping must not allocate after CREATE_NEW: an
            // allocation failure there would lose the name of an owned file.
            if(temporary.handle.value!=INVALID_HANDLE_VALUE){temporary.path.swap(candidate);break;}
            const auto error=GetLastError();
            if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)return WriteResult::Unavailable;
        }
        if(temporary.handle.value==INVALID_HANDLE_VALUE)return WriteResult::Unavailable;
        if(!Admitted(admission,key)||!Healthy())return WriteResult::Rejected;
        DWORD count=0;
        if(!WriteFile(temporary.handle.value,encoded.data(),static_cast<DWORD>(encoded.size()),&count,nullptr)||
           count!=encoded.size()||!FlushFileBuffers(temporary.handle.value)||!temporary.handle.Close())return WriteResult::Unavailable;
        Bytes readback;
        if(ReadRaw(temporary.path,readback)!=ReadResult::Found||readback!=encoded)return WriteResult::Unavailable;
        Bytes current;const auto now=ReadRaw(path,current);
        if(now!=prior||(now==ReadResult::Found&&current!=previous))return WriteResult::Rejected;
        if(!Admitted(admission,key)||!Healthy())return WriteResult::Rejected;
        if(!MoveFileExW(temporary.path.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return WriteResult::Unavailable;
        temporary.published=true;
        readback.clear();
        if(ReadRaw(path,readback)!=ReadResult::Found||readback!=encoded)return WriteResult::PublishedUnverified;
        // A commit that linearized before revocation belongs to the old exact
        // save key. Report it distinctly; never write it into the new session.
        return Admitted(admission,key)?WriteResult::Written:WriteResult::PublishedRetired;
    }catch(...){return WriteResult::Unavailable;}
}
}
