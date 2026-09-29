#include <cstdio>
#if __has_include("../hooks/NativeSaveLoadEvents.h")
#include "../hooks/NativeSaveLoadEvents.h"
namespace E=FfxHooks::NativeSaveEvents;
namespace {
unsigned checks=0,failed=0,begins=0,ends=0,failures=0;
std::uint64_t first=0,last=0;
#define CHECK(x) do{++checks;if(!(x)){++failed;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
void Read(const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept{}
void Write(const wchar_t*,const unsigned char*,std::size_t) noexcept{}
void Before(std::uint64_t cookie,void* destination,const void* source) noexcept{
    (void)source;++begins;CHECK(cookie>last&&destination);if(!first)first=cookie;last=cookie;
}
void After(std::uint64_t cookie,bool completed) noexcept{++ends;CHECK(cookie!=0);if(!completed)++failures;}
}
int main(){
    unsigned char source[16]{},destination[16]{};
    CHECK(E::BeginLoad(destination,source).cookie==0);
    E::Observer invalid{Read,Write};invalid.loadStarting=Before;
    CHECK(!E::SubscribeAdditional(&invalid));
    invalid.loadStarting=nullptr;invalid.loadCompleted=After;
    CHECK(!E::SubscribeAdditional(&invalid));
    E::Observer observer{Read,Write};observer.loadStarting=Before;observer.loadCompleted=After;
    CHECK(E::SubscribeAdditional(&observer));
    CHECK(E::BeginLoad(nullptr,source).cookie==0&&begins==0);
    auto missing=E::BeginLoad(destination,nullptr);CHECK(missing.cookie&&begins==1);E::EndLoad(missing,false);
    auto outer=E::BeginLoad(destination,source);auto inner=E::BeginLoad(destination,source);
    CHECK(outer.cookie&&inner.cookie>outer.cookie&&begins==3&&ends==1);
    E::EndLoad(inner,false);E::UnsubscribeAdditional(&observer);E::EndLoad(outer,true);
    CHECK(ends==3&&failures==2);
    CHECK(E::BeginLoad(destination,source).cookie==0&&begins==3);
    CHECK(E::SubscribeAdditional(&observer));
    for(unsigned i=0;i<2048;++i){auto call=E::BeginLoad(destination,source);CHECK(call.cookie>first);E::EndLoad(call,true);}
    CHECK(begins==2051&&ends==2051&&failures==2);
    E::UnsubscribeAdditional(&observer);
    std::printf("NativeSaveLoadEventsRt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: shared native load lifecycle is missing");return 1;}
#endif
