// Read-only corpus admission check. The files remain external private fixtures.
#include "workshop.h"
#include "../../../src/runtime/FfxHooksDll/hooks/RonsoPoolSave.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

int main(int argc,char** argv){
    unsigned admitted=0,rejected=0;
    for(int n=1;n<argc;++n){
        FfxHooks::RonsoPool::SaveImage image{};std::ifstream file(argv[n],std::ios::binary);
        bool valid=bool(file.read(reinterpret_cast<char*>(image.data()),image.size()));
        char extra=0;if(valid&&file.read(&extra,1))valid=false;
        if(!valid||!FfxHooks::RonsoPool::IsValidSave(image)){
            ++rejected;std::printf("CORPUS_ENTRY %d CRC_OR_SIZE_REJECTED\n",n-1);continue;
        }
        std::array<std::uint16_t,112> items{};std::array<bool,112> seen{};
        for(unsigned slot=0;slot<256;++slot){
            const unsigned at=0x3F0C+2*slot,id=image[at]|(unsigned(image[at+1])<<8);
            if(id>=0x2000&&id<0x2070){const unsigned item=id-0x2000;
                valid=valid&&!seen[item];seen[item]=true;items[item]=image[0x410C+slot];
            }
        }
        workshop::State state{};const auto code=valid?workshop::Import(image.data()+0x44DC,items.data(),123,state):workshop::Error::InvalidState;
        if(code!=workshop::Error::Ok){
            ++rejected;std::printf("CORPUS_ENTRY %d MODEL_REJECTED %d\n",n-1,static_cast<int>(code));continue;
        }
        bool identical=std::memcmp(items.data(),state.items,sizeof(state.items))==0;
        for(unsigned slot=0;slot<200;++slot)identical&=std::memcmp(state.pieces[slot].native,image.data()+0x44DC+22*slot,22)==0;
        if(!identical){++rejected;std::printf("CORPUS_ENTRY %d IMPORT_CHANGED_NATIVE_BYTES\n",n-1);continue;}
        ++admitted;std::printf("CORPUS_ENTRY %d ADMITTED_UNCHANGED\n",n-1);
    }
    std::printf("WORKSHOP_SAVE_CORPUS admitted=%u rejected=%u total=%d\n",admitted,rejected,argc-1);
    return argc<2?2:rejected?1:0;
}
