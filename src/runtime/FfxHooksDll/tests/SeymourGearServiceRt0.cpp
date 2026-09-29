#include <cstdint>
#include <cstdio>
#include <stdexcept>
#if __has_include("../hooks/SeymourGearPresentationService.h")
#include "../hooks/SeymourGearPresentationService.h"
namespace S=FfxHooks::SeymourGearPresentation;
namespace {
unsigned checks=0,failed=0;
#define CHECK(x) do {++checks;if(!(x)){++failed;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Fake {
    unsigned nativeCalls=0,writes=0,admissionCalls=0;
    bool admitted=true,canWrite=true,revoke=false,throwNative=false;
    unsigned name=0,owner=0;int shortName=0;std::uint16_t* output=nullptr;
    static const std::uint8_t nativeText[2];
    static const std::uint8_t* Native(void* p,unsigned name,unsigned owner,int shortName,std::uint16_t* output){
        auto& f=*static_cast<Fake*>(p);++f.nativeCalls;f.name=name;f.owner=owner;f.shortName=shortName;f.output=output;
        if(f.throwNative)throw std::runtime_error("native");
        return nativeText;
    }
    static bool Current(void* p){auto& f=*static_cast<Fake*>(p);++f.admissionCalls;
        return f.admitted&&!(f.revoke&&f.admissionCalls>1);}
    static bool Model(void* p,std::uint16_t* output,std::uint16_t model){
        auto& f=*static_cast<Fake*>(p);if(!f.canWrite)return false;
        if(output){*output=model;++f.writes;}return true;
    }
    S::Io Ops(){return {this,Native,Current,Model};}
};
const std::uint8_t Fake::nativeText[2]={0x41,0};
}
int main(){
    for(unsigned owner:{0u,6u,8u,255u}){
        Fake f;std::uint16_t out=0xcafe;
        CHECK(S::Service(true,0,owner,9,&out,f.Ops())==Fake::nativeText);
        CHECK(f.nativeCalls==1&&f.writes==0&&f.admissionCalls==0&&out==0xcafe);
        CHECK(f.owner==owner&&f.shortName==9&&f.output==&out);
    }
    for(unsigned row:{171u,4095u,0xffffu}){
        Fake f;std::uint16_t out=0xcafe;CHECK(S::Service(true,row,7,0,&out,f.Ops())==Fake::nativeText);
        CHECK(f.nativeCalls==1&&!f.writes&&out==0xcafe&&f.name==row);
    }
    for(unsigned row=0;row<171;++row)for(int shortName:{0,1,-1,9}){
        Fake f;std::uint16_t out=0xcafe;S::Presentation expected{};
        CHECK(S::Resolve(static_cast<std::uint16_t>(row),7,shortName!=0,expected));
        CHECK(S::Service(true,row,7,shortName,&out,f.Ops())==expected.text);
        CHECK(out==expected.model&&f.writes==1&&f.nativeCalls==0);
        CHECK(S::Service(true,row,7,shortName,nullptr,f.Ops())==expected.text);
        CHECK(f.writes==1&&f.nativeCalls==0);
    }
    for(unsigned mode=0;mode<4;++mode){
        Fake f;std::uint16_t out=0xcafe;
        if(mode==1)f.admitted=false;
        if(mode==2)f.canWrite=false;
        if(mode==3)f.revoke=true;
        CHECK(S::Service(mode!=0,74,7,0,&out,f.Ops())==Fake::nativeText);
        CHECK(f.nativeCalls==1&&!f.writes&&out==0xcafe);
    }
    {Fake f;std::uint16_t out=0;S::Presentation expected{};S::Resolve(0x804a,7,false,expected);
        CHECK(S::Service(true,0x804a,0x107,0,&out,f.Ops())==expected.text&&out==0x4067);}
    {Fake f;f.throwNative=true;bool caught=false;
        try{(void)S::Service(false,0,7,0,nullptr,f.Ops());}catch(const std::runtime_error&){caught=true;}
        CHECK(caught&&f.nativeCalls==1);}
    std::printf("SeymourGearServiceRt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SeymourGearPresentationService is not implemented");return 1;}
#endif
