// Real Win32 files; injected failures wrap explicit OS boundaries in the actual
// store implementation, not a separate storage model.
#include <cstdio>
#if __has_include("../hooks/SphereGridProgress8Store.h")
#include "../hooks/SphereGridProgress8Store.h"
#include <cstdlib>
#include <new>
#include <atomic>
namespace Fault {
enum class Kind {None,ShortWrite,Write,Flush,Close,Rename,ReadTemporary,ReadPublished,Allocation};
Kind kind=Kind::None;
bool fired=false,renamed=false;
HANDLE temporary=INVALID_HANDLE_VALUE,temporaryReader=INVALID_HANDLE_VALUE;
std::atomic<bool> failAllocation{false};
bool Trigger(Kind value){if(kind!=value||fired)return false;fired=true;SetLastError(ERROR_WRITE_FAULT);return true;}
HANDLE WINAPI Create(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,
                     DWORD disposition,DWORD attributes,HANDLE source){
    HANDLE file=::CreateFileW(name,access,share,security,disposition,attributes,source);
    if(file!=INVALID_HANDLE_VALUE&&wcsstr(name,L".tmp-")){
        if(access&GENERIC_WRITE){temporary=file;
            if(kind==Kind::Allocation&&!fired){fired=true;failAllocation.store(true);}
        }else temporaryReader=file;
    }
    return file;
}
BOOL WINAPI Write(HANDLE file,LPCVOID data,DWORD size,LPDWORD count,LPOVERLAPPED async){
    if(file==temporary){
        if(Trigger(Kind::Write)){*count=0;return FALSE;}
        if(Trigger(Kind::ShortWrite))return ::WriteFile(file,data,size/2,count,async);
    }
    return ::WriteFile(file,data,size,count,async);
}
BOOL WINAPI Flush(HANDLE file){if(file==temporary&&Trigger(Kind::Flush))return FALSE;return ::FlushFileBuffers(file);}
BOOL WINAPI Close(HANDLE file){
    const bool fail=file==temporary&&Trigger(Kind::Close);const BOOL result=::CloseHandle(file);
    if(file==temporary)temporary=INVALID_HANDLE_VALUE;
    if(file==temporaryReader)temporaryReader=INVALID_HANDLE_VALUE;
    return fail?FALSE:result;
}
BOOL WINAPI Rename(LPCWSTR oldName,LPCWSTR newName,DWORD flags){
    if(Trigger(Kind::Rename))return FALSE;
    const BOOL result=::MoveFileExW(oldName,newName,flags);if(result)renamed=true;return result;
}
BOOL WINAPI Read(HANDLE file,LPVOID data,DWORD size,LPDWORD count,LPOVERLAPPED async){
    if((file==temporaryReader&&Trigger(Kind::ReadTemporary))||(renamed&&Trigger(Kind::ReadPublished))){*count=0;return FALSE;}
    return ::ReadFile(file,data,size,count,async);
}
}
void* operator new(std::size_t size){
    if(Fault::failAllocation.exchange(false))throw std::bad_alloc();
    if(void* result=std::malloc(size?size:1))return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete[](void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}
#define CreateFileW Fault::Create
#define WriteFile Fault::Write
#define FlushFileBuffers Fault::Flush
#define CloseHandle Fault::Close
#define MoveFileExW Fault::Rename
#define ReadFile Fault::Read
#include "../hooks/SphereGridProgress8Store.cpp"
#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef CloseHandle
#undef MoveFileExW
#undef ReadFile
namespace G=FfxHooks::SphereGridProgress8;
namespace D=FfxHooks::SphereGridProgress8Disk;
unsigned checks=0,failures=0;
#define CHECK(value) do{++checks;if(!(value)){++failures;std::printf("FAIL line %u: %s\n",__LINE__,#value);}}while(false)
bool Allowed(void*,const G::Key&) noexcept{return true;}
unsigned TemporaryCount(const std::wstring& root){
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((root+L"\\*.tmp-*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE)return 0;
    unsigned count=0;do{++count;}while(FindNextFileW(find,&data));FindClose(find);return count;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const std::wstring root=argv[1];
    if(GetFileAttributesW(root.c_str())!=INVALID_FILE_ATTRIBUTES||!CreateDirectoryW(root.c_str(),nullptr))return 2;
    D::Store store;CHECK(store.Initialize(root,false));
    G::Snapshot state;state.nodes.resize(1003);state.links.resize(1024);state.cursors.fill(1002);
    for(auto& node:state.nodes)node={34,128};for(auto& link:state.links)link=128;
    for(unsigned index=1;index<=8;++index){
        G::Key key{};key.path.fill(1);key.image.fill(static_cast<unsigned char>(index));key.layout.fill(3);key.contents.fill(4);
        Fault::kind=static_cast<Fault::Kind>(index);Fault::fired=false;Fault::renamed=false;
        const auto result=store.Publish(key,state,{nullptr,Allowed});
        Fault::kind=Fault::Kind::None;Fault::failAllocation.store(false);
        CHECK(Fault::fired);CHECK(TemporaryCount(root)==0);
        G::Snapshot observed;const auto read=store.Read(key,observed);
        if(index==7){CHECK(result==D::WriteResult::PublishedUnverified);CHECK(read==D::ReadResult::Found&&observed==state);}
        else {CHECK(result==D::WriteResult::Unavailable);CHECK(read==D::ReadResult::Missing);}
    }
    std::printf("SphereGridProgress8StoreFaultRt1: %u/%u passed (actual source; real files; injected Win32 failures)\n",checks-failures,checks);
    return failures?1:0;
}
#else
int main(){std::puts("FAIL: eight-character progress filesystem store is missing");return 1;}
#endif
