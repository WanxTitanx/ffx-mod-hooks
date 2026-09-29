// Real libc file I/O in an exclusive test directory, never installed saves.
// Production commit adapter; POSIX identity/readback endpoints, not Win32 proof.
#include "../hooks/NativeSaveCommitAdapter.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
namespace fs=std::filesystem;
using namespace FfxHooks;
using namespace NativeSaveCommit;
static unsigned checks=0,failures=0,mode=0,closes=0,staged=0,verified=0;
static std::uint64_t cookie=0;
static std::uint32_t epoch=1,threadId=7,lastError=0,nativeLast=0;
static int nativeErrno=0;
static Runtime* active=nullptr;
static fs::path target;
static RonsoPool::SaveImage expected{};
static void Check(bool ok,const char* reason){++checks;if(!ok){++failures;if(failures<=16)std::cerr<<"FAIL "<<reason<<'\n';}}
static bool Ready() noexcept{return true;}
static std::uint32_t Epoch() noexcept{return epoch;}
static std::uint32_t Thread() noexcept{return threadId;}
static std::uint32_t GetError() noexcept{return lastError;}
static void SetError(std::uint32_t x) noexcept{lastError=x;}
static int* Number() noexcept{return &errno;}
static bool Identity(void* stream,FileIdentity& result) noexcept{
    result={};struct stat info{};
    if(!stream||fstat(fileno(static_cast<FILE*>(stream)),&info)!=0||!S_ISREG(info.st_mode))return false;
    result.volume=static_cast<std::uint32_t>(info.st_dev);result.index=static_cast<std::uint64_t>(info.st_ino);
    lastError=71;errno=ERANGE;return result.Valid();
}
static bool Readback(const wchar_t* path,FileIdentity& file,RonsoPool::SaveImage& data) noexcept{
    try{
        FILE* in=fopen(fs::path(path).string().c_str(),"rb");if(!in)return false;
        const bool identity=Identity(in,file);
        const auto count=fread(data.data(),1,data.size(),in);
        const bool exact=count==data.size()&&fgetc(in)==EOF&&!ferror(in);
        const bool closed=fclose(in)==0;
        lastError=81;errno=EDOM;
        return identity&&exact&&closed&&RonsoPool::IsValidSave(data);
    }catch(...){return false;}
}
static void ReadEvent(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept{}
static void WriteEvent(const wchar_t*,const unsigned char*,std::size_t) noexcept{}
static void Staged(std::uint64_t id,const wchar_t*,const unsigned char*,std::size_t) noexcept{cookie=id;++staged;}
static void Verified(std::uint64_t id,const wchar_t*,const unsigned char* data,std::size_t size) noexcept{
    Check(id==cookie&&closes==1&&size==expected.size()&&std::memcmp(data,expected.data(),size)==0,"verified callback payload/order");
    ++verified;lastError=91;errno=EINVAL;
}
static void Aborted(std::uint64_t) noexcept{lastError=101;errno=EINVAL;}
static void WriteFixture(const fs::path& path,const RonsoPool::SaveImage& data){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    out.close();if(!out)throw std::runtime_error("could not write private fixture");
}
static int OriginalClose(void* stream){
    ++closes;int result=fclose(static_cast<FILE*>(stream));
    if(mode==1)result=-1; // Simulated reported close failure after actual cleanup.
    if(mode==2){std::ofstream out(target,std::ios::binary|std::ios::app);out.put('x');}
    if(mode==3){auto changed=expected;changed[0]^=1;WriteFixture(target,changed);}
    if(mode==4){fs::rename(target,target.string()+".retired");WriteFixture(target,expected);}
    if(mode==5)++epoch;
    if(mode==6)++threadId;
    if(mode==7)active->Stop();
    if(mode==9){auto changed=expected;changed[64+0x21ec]^=1;RonsoPool::SealSave(changed);WriteFixture(target,changed);}
    if(mode==10)fs::rename(target,target.string()+".unavailable");
    nativeErrno=errno;nativeLast=lastError;return result;
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Usage: SphereGridSaveDiskRt1 READ_ONLY_CORPUS NEW_TEST_DIRECTORY\n";return 2;}
    try{
        const fs::path root(argv[2]);if(!fs::create_directory(root))throw std::runtime_error("test directory must be new");
        NativeSaveEvents::Observer observer{ReadEvent,WriteEvent};
        observer.writeStaged=Staged;observer.writeVerified=Verified;observer.writeAborted=Aborted;
        Check(NativeSaveEvents::SubscribeAdditional(&observer),"subscriber admission");
        unsigned saves=0,cases=0;
        for(const auto& entry:fs::directory_iterator(argv[1])){
            if(!fs::is_regular_file(fs::symlink_status(entry.path()))||entry.file_size()!=expected.size())continue;
            std::ifstream input(entry.path(),std::ios::binary);
            input.read(reinterpret_cast<char*>(expected.data()),expected.size());
            if(!input||!RonsoPool::IsValidSave(expected))continue;
            const auto original=expected;++saves;
            for(mode=0;mode<11;++mode){
                ++cases;target=root/("case-"+std::to_string(cases));
                FILE* out=fopen(target.string().c_str(),"wbx");if(!out)throw std::runtime_error("exclusive fixture create failed");
                epoch=1;threadId=7;closes=staged=verified=0;lastError=11;errno=EAGAIN;
                auto runtime=std::make_unique<Runtime>();active=runtime.get();
                Check(runtime->Configure({Ready,Epoch,Thread,Identity,Readback,GetError,SetError,Number}),"adapter setup");
                auto ticket=runtime->Stage(out,target.wstring(),expected);
                Check(ticket.Valid()&&staged==1&&verified==0,"buffering cannot publish completion");
                const auto request=expected.size()-(mode==8?1u:0u);
                const auto count=fwrite(expected.data(),1,request,out);
                runtime->Finish(ticket,count==expected.size());
                const int result=runtime->Close(out,OriginalClose);
                Check(result==(mode==1?-1:0)&&closes==1,"original fclose result/count");
                Check(verified==(mode==0?1u:0u),"wrong file/bytes/thread/epoch/short write/stop admitted");
                Check(lastError==nativeLast&&errno==nativeErrno,"metadata did not preserve native error state");
                active=nullptr;
            }
            RonsoPool::SaveImage after{};std::ifstream recheck(entry.path(),std::ios::binary);
            recheck.read(reinterpret_cast<char*>(after.data()),after.size());
            Check(static_cast<bool>(recheck)&&after==original,"input source changed");
        }
        NativeSaveEvents::UnsubscribeAdditional(&observer);Check(saves>0,"no valid source saves");
        std::cout<<"{\"scope\":\"production commit adapter with real POSIX fclose and disk readback\",\"saves\":"<<saves
          <<",\"disk_scenarios\":"<<cases<<",\"checks_passed\":"<<checks-failures<<",\"checks_total\":"<<checks
          <<",\"failures\":"<<failures<<",\"win32_native_io_proven\":false,\"production_ready\":false}\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
