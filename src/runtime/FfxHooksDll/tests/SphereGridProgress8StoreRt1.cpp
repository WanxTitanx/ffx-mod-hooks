#include <cstdio>
#if __has_include("../hooks/SphereGridProgress8Store.h")
#include "../hooks/SphereGridProgress8Store.h"
#include <algorithm>
#include <string>

namespace G=FfxHooks::SphereGridProgress8;
namespace D=FfxHooks::SphereGridProgress8Disk;
unsigned checks=0,failures=0;
#define CHECK(value) do{++checks;if(!(value)){if(++failures<=30)std::printf("FAIL line %u: %s\n",__LINE__,#value);}}while(false)
G::Key Key(unsigned variant=0){
    G::Key key{};unsigned byte=1;
    for(auto* hash:{&key.path,&key.image,&key.layout,&key.contents}){
        for(auto& value:*hash)value=static_cast<unsigned char>(byte++);
    }
    key.image[0]=static_cast<unsigned char>(variant);return key;
}
G::Snapshot Snapshot(){
    G::Snapshot value;value.nodes.resize(1003);value.links.resize(1024);
    for(unsigned i=0;i<value.nodes.size();++i)value.nodes[i]={static_cast<unsigned char>(i%130),static_cast<unsigned char>(i%256)};
    for(unsigned i=0;i<value.links.size();++i)value.links[i]=static_cast<unsigned char>(i%256);
    for(unsigned i=0;i<8;++i)value.cursors[i]=static_cast<unsigned short>(i*131);
    value.cursors[7]=1002;value.tilt=2;value.zoom=3;return value;
}
struct Guard {
    unsigned calls=0,revoke=0;
    bool active=true;
    static bool Current(void* p,const G::Key&) noexcept {
        auto& self=*static_cast<Guard*>(p);++self.calls;
        if(self.revoke&&self.calls>=self.revoke)self.active=false;
        return self.active;
    }
    D::Admission Gate(){return {this,Current};}
};
bool RawWrite(const std::wstring& path,const G::Bytes& bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;const bool ok=bytes.empty()||(WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size());
    return CloseHandle(file)&&ok;
}
unsigned TemporaryCount(const std::wstring& root){
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((root+L"\\*.tmp-*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE)return 0;
    unsigned count=0;do{++count;}while(FindNextFileW(find,&data));FindClose(find);return count;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const std::wstring root=argv[1];
    // The runner supplies a new private VMTasks directory. Never reuse a user's
    // existing save/config directory, and keep failure artifacts for inspection.
    if(GetFileAttributesW(root.c_str())!=INVALID_FILE_ATTRIBUTES||!CreateDirectoryW(root.c_str(),nullptr))return 2;
    const auto key=Key();const auto state=Snapshot();Guard guard;G::Snapshot observed=state;
    D::Store store;
    CHECK(store.Read(key,observed)==D::ReadResult::Unavailable&&observed==state);
    CHECK(!store.Initialize(L"",false));CHECK(store.Initialize(root,false));
    CHECK(store.Initialize(root,false));CHECK(!store.Initialize(root+L"-other",true));
    {D::Store other;CHECK(!other.Initialize(root,false));}
    CHECK(store.Read(key,observed)==D::ReadResult::Missing&&observed==state);
    CHECK(store.Publish(key,state,{})==D::WriteResult::Rejected);
    CHECK(store.Read(key,observed)==D::ReadResult::Missing);
    CHECK(store.Publish(key,state,guard.Gate())==D::WriteResult::Written);
    const auto healthyCalls=guard.calls;CHECK(healthyCalls>=3&&healthyCalls<32);
    CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==state);
    CHECK(store.Publish(key,state,guard.Gate())==D::WriteResult::Unchanged);
    auto changed=state;changed.nodes.back().mask^=0x80;changed.cursors[7]=1001;
    CHECK(store.Publish(key,changed,guard.Gate())==D::WriteResult::Written);
    CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
    {
        // Distinct full identities sharing the abbreviated filename must never
        // overwrite or import one another, even if the checksum is valid.
        auto collision=key;collision.contents[31]^=1;
        CHECK(store.RecordPath(collision)==store.RecordPath(key));observed=state;
        CHECK(store.Read(collision,observed)==D::ReadResult::Invalid&&observed==state);
        CHECK(store.Publish(collision,state,guard.Gate())==D::WriteResult::Rejected);
        CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
    }
    for(unsigned edge=1;edge<=healthyCalls;++edge){
        const auto unique=Key(edge+10);Guard stopped;stopped.revoke=edge;
        const auto result=store.Publish(unique,state,stopped.Gate());observed=changed;
        const auto found=store.Read(unique,observed);
        CHECK(!stopped.active&&stopped.calls>=edge);
        CHECK(result==D::WriteResult::Rejected||result==D::WriteResult::PublishedRetired);
        if(result==D::WriteResult::PublishedRetired)CHECK(found==D::ReadResult::Found&&observed==state);
        else CHECK(found==D::ReadResult::Missing&&observed==changed);
        CHECK(TemporaryCount(root)==0);
    }
    G::Bytes encoded;CHECK(G::Encode(Key(90),state,encoded));
    for(std::size_t size:{std::size_t(0),std::size_t(1),std::size_t(159),std::size_t(160),encoded.size()-1,encoded.size()+1,G::kMaxBytes+1}){
        auto damaged=encoded;damaged.resize(size);CHECK(RawWrite(store.RecordPath(Key(90)),damaged));observed=state;
        CHECK(store.Read(Key(90),observed)==D::ReadResult::Invalid&&observed==state);
        CHECK(store.Publish(Key(90),state,guard.Gate())==D::WriteResult::Rejected);
        CHECK(TemporaryCount(root)==0);
    }
    {
        auto bad=encoded;bad.back()^=1;CHECK(RawWrite(store.RecordPath(Key(90)),bad));observed=changed;
        CHECK(store.Read(Key(90),observed)==D::ReadResult::Invalid&&observed==changed);
    }
    {
        const auto path=store.RecordPath(Key(91));CHECK(CreateDirectoryW(path.c_str(),nullptr)!=0);observed=state;
        CHECK(store.Read(Key(91),observed)==D::ReadResult::Invalid&&observed==state);
        CHECK(store.Publish(Key(91),state,guard.Gate())==D::WriteResult::Rejected);
    }
    {
        HANDLE locked=CreateFileW(store.RecordPath(key).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        CHECK(locked!=INVALID_HANDLE_VALUE);observed=state;
        CHECK(store.Read(key,observed)==D::ReadResult::Unavailable&&observed==state);
        CHECK(store.Publish(key,state,guard.Gate())==D::WriteResult::Unavailable);
        if(locked!=INVALID_HANDLE_VALUE)CHECK(CloseHandle(locked)!=0);
        CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
    }
    {
        const auto alias=store.RecordPath(Key(92));
        CHECK(CreateHardLinkW(alias.c_str(),store.RecordPath(key).c_str(),nullptr)!=0);observed=state;
        CHECK(store.Read(Key(92),observed)==D::ReadResult::Invalid&&observed==state);
        CHECK(store.Publish(Key(92),state,guard.Gate())==D::WriteResult::Rejected);
        CHECK(DeleteFileW(alias.c_str())!=0);
        CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
    }
    {
        // A live store pins its directory, rather than trusting a recycled path.
        CHECK(!MoveFileExW(root.c_str(),(root+L"-moved").c_str(),0));
        CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
        auto invalid=state;invalid.cursors[7]=65534;
        CHECK(store.Publish(key,invalid,guard.Gate())==D::WriteResult::Rejected);
        CHECK(store.Read(key,observed)==D::ReadResult::Found&&observed==changed);
    }
    CHECK(TemporaryCount(root)==0);
    std::printf("SphereGridProgress8StoreRt1: %u/%u passed (real Win32 filesystem; private directory)\n",checks-failures,checks);
    return failures?1:0;
}
#else
int main(){std::puts("FAIL: eight-character progress filesystem store is missing");return 1;}
#endif
