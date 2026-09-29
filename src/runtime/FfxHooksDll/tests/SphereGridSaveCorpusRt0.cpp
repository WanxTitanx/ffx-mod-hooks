// Real saves, read-only. All corruption is private memory, never a game save write.
#include "../hooks/NativeSaveCommitCore.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <thread>
#include <vector>
namespace fs=std::filesystem;
using namespace FfxHooks::NativeSaveCommit;
using FfxHooks::RonsoPool::SaveImage;
using FfxHooks::RonsoPool::IsValidSave;
using FfxHooks::RonsoPool::SealSave;
struct Counts {std::uint64_t passed=0,total=0;};
static std::map<std::string,Counts> groups;
static unsigned failures=0;
static void Check(const char* group,bool ok,const char* reason){
    auto& c=groups[group];++c.total;
    if(ok)++c.passed;
    else if(++failures<=16)std::cerr<<"FAIL "<<group<<": "<<reason<<'\n';
}
static std::vector<unsigned char> Read(const fs::path& path){
    if(!fs::is_regular_file(fs::symlink_status(path)))throw std::runtime_error("Nonregular input");
    auto length=fs::file_size(path);
    if(length>4*1024*1024)throw std::runtime_error("Oversized input");
    std::ifstream in(path,std::ios::binary);
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    if(!in.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(length)))throw std::runtime_error("Short read");
    if(in.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Input grew during read");
    return data;
}
static bool Admit(const std::vector<unsigned char>& bytes,SaveImage& out){
    if(bytes.size()!=out.size())return false;
    SaveImage copy{};std::copy(bytes.begin(),bytes.end(),copy.begin());
    if(!IsValidSave(copy))return false;
    out=copy;return true;
}
static CloseAttempt Buffered(Tracker& t,const SaveImage& image){
    auto ticket=t.BeginWrite(0x1000,{17,1234},1,7,L"C:\\fixture\\ffx_000",image);
    if(!ticket.Valid()||!t.FinishWrite(ticket,true))throw std::runtime_error("Could not stage valid input");
    return t.BeginClose(0x1000,{17,1234},1,7);
}
static bool Finish(Tracker& t,const CloseAttempt& a,const SaveImage& image){return t.CompleteClose(a,true,{17,1234},1,7,image);}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: SphereGridSaveCorpusRt0 READ_ONLY_CORPUS\n";return 2;}
    try{
        std::vector<std::pair<fs::path,std::vector<unsigned char>>> files;
        for(const auto& e:fs::directory_iterator(argv[1]))files.emplace_back(e.path(),Read(e.path()));
        std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        std::vector<SaveImage> valid;
        std::vector<std::string> invalidIds;
        unsigned nativeSized=0,invalid=0,other=0;
        auto tracker=std::make_unique<Tracker>();
        for(const auto& entry:files){
            SaveImage image{};image.fill(0xA5);const auto before=image;
            const bool admitted=Admit(entry.second,image);
            if(entry.second.size()!=image.size()){
                ++other;Check("format_separation",!admitted&&image==before,"wrong size accepted or output changed");continue;
            }
            ++nativeSized;
            SaveImage raw{};std::copy(entry.second.begin(),entry.second.end(),raw.begin());
            if(!admitted){
                ++invalid;invalidIds.push_back(entry.first.filename().string());
                Check("invalid_input",!IsValidSave(raw)&&image==before,"invalid input changed output");
                Check("invalid_input",!tracker->BeginWrite(0x1000,{17,1234},1,7,L"ffx_000",raw).Valid(),"invalid CRC staged");continue;
            }
            valid.push_back(image);
            Check("unchanged_commit",Finish(*tracker,Buffered(*tracker,image),image),"valid save did not commit");
            for(unsigned mode=0;mode<12;++mode){
                auto a=Buffered(*tracker,image);auto actual=image;FileIdentity file{17,1234};
                unsigned epoch=1,thread=7;bool closed=true;
                if(mode==0)closed=false;
                if(mode==1)++file.index;
                if(mode==2)++file.volume;
                if(mode==3)++epoch;
                if(mode==4)++thread;
                if(mode==5)a.path[0]^=1;
                if(mode==6)++a.ticket.serial;
                if(mode==7)actual[0]^=1;
                if(mode==8)actual.back()^=1;
                if(mode==9){actual[64+0x21ec]^=1;SealSave(actual);}
                if(mode==10)tracker->Reset();
                if(mode==11)a.image[64+0x21ec]^=1;
                Check("identity_failure",!tracker->CompleteClose(a,closed,file,epoch,thread,actual),"mismatched completion accepted");
                tracker->Reset();
            }
            for(std::size_t length:{0u,1u,63u,64u,8748u,25844u,25847u,26879u,26881u}){
                auto bytes=entry.second;bytes.resize(length);auto output=before;
                Check("truncation_and_trailing",!Admit(bytes,output)&&output==before,"partial/trailing data accepted");
            }
        }
        Check("corpus_presence",nativeSized>0&&!valid.empty(),"no usable FFX saves");
        std::uint64_t mutations=0,crcDetected=0,crcCollisions=0;
        std::mt19937 random(0x53F3A);
        for(std::size_t index=0;index<valid.size();++index){
            const auto& original=valid[index];const unsigned count=index==0?0x1320u*8u:1024u;
            for(unsigned i=0;i<count;++i){
                const std::size_t at=index==0?64+0x21ec+i/8:64+(random()%(25844-64));
                const unsigned bit=index==0?i%8:random()%8;
                auto changed=original;changed[at]^=static_cast<unsigned char>(1u<<bit);++mutations;
                if(!IsValidSave(changed))++crcDetected;else ++crcCollisions;
                auto a=Buffered(*tracker,original);
                Check("corruption_exact_readback",!Finish(*tracker,a,changed),"mutated readback accepted");
            }
            for(const auto& candidate:valid){
                auto a=Buffered(*tracker,original);
                Check("all_save_pairs",Finish(*tracker,a,candidate)==(candidate==original),"another save matched staging");
            }
            auto old=Buffered(*tracker,original);tracker->Reset();
            auto next=tracker->BeginWrite(0x1000,{17,1234},1,7,L"C:\\other\\ffx_000",original);
            Check("same_bytes_different_path",next.Valid()&&next.serial!=old.ticket.serial,"ticket reuse");
            Check("same_bytes_different_path",!Finish(*tracker,old,original),"old path adopted new save");
            Check("same_bytes_different_path",tracker->FinishWrite(next,true),"old completion cancelled new write");
            auto a=tracker->BeginClose(0x1000,{17,1234},1,7);
            Check("same_bytes_different_path",Finish(*tracker,a,original),"new matching completion rejected");
        }
        if(!valid.empty())for(unsigned round=0;round<32;++round){
            tracker->Reset();std::array<bool,8> accepted{};std::array<std::uint64_t,8> serials{};
            std::vector<std::thread> threads;std::atomic<bool> go{false};
            for(unsigned i=0;i<8;++i)threads.emplace_back([&,i]{
                while(!go.load(std::memory_order_acquire))std::this_thread::yield();
                const auto& image=valid[(round+i)%valid.size()];
                auto ticket=tracker->BeginWrite(0x1000+i,{17,1234+i},1,10+i,L"ffx_000",image);serials[i]=ticket.serial;
                bool staged=ticket.Valid()&&tracker->FinishWrite(ticket,true);
                auto a=tracker->BeginClose(0x1000+i,{17,1234+i},1,10+i);
                accepted[i]=staged&&tracker->CompleteClose(a,true,{17,1234+i},1,10+i,image);
            });
            go.store(true,std::memory_order_release);for(auto& t:threads)t.join();
            for(bool ok:accepted)Check("concurrent_streams",ok,"independent stream failed");
            std::sort(serials.begin(),serials.end());
            Check("concurrent_streams",std::adjacent_find(serials.begin(),serials.end())==serials.end(),"serial collision");
        }
        for(const auto& entry:files)Check("source_preservation",Read(entry.first)==entry.second,"source changed");
        std::cout<<"{\n  \"scope\":\"production CRC and commit tracker; private corpus; no game\",\n"
          <<"  \"unique_native_sized\":"<<nativeSized<<",\n  \"valid_native_crc\":"<<valid.size()<<",\n  \"invalid_native_crc\":"<<invalid
          <<",\n  \"other_format_entries\":"<<other<<",\n  \"corruption_inputs\":"<<mutations<<",\n  \"crc_detected\":"<<crcDetected
          <<",\n  \"crc_collisions_observed\":"<<crcCollisions<<",\n  \"groups\":{\n";
        bool first=true;for(const auto& g:groups){
            if(!first)std::cout<<",\n";
            first=false;std::cout<<"    \""<<g.first<<"\":{\"passed\":"<<g.second.passed<<",\"total\":"<<g.second.total<<"}";
        }
        std::cout<<"\n  },\n  \"invalid_source_hashes\":[";
        for(std::size_t i=0;i<invalidIds.size();++i){if(i)std::cout<<',';std::cout<<'"'<<invalidIds[i]<<'"';}
        std::cout<<"],\n  \"failures\":"<<failures<<",\n  \"production_ready\":false\n}\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<"Corpus test refused: "<<e.what()<<'\n';return 2;}
}
