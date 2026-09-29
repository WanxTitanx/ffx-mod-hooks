// Read-only metadata resolution. No inventory edits or external assets.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if __has_include("../hooks/SeymourGearPresentationCore.h")
#include "../hooks/SeymourGearPresentationCore.h"
namespace S=FfxHooks::SeymourGearPresentation;
static unsigned checks=0,failed=0;
#define CHECK(x) do {++checks;if(!(x)){++failed;std::printf("FAIL line %u: %s\n",__LINE__,#x);}}while(false)
int main(){
    const auto sentinel=reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(1));
    for(unsigned owner=0;owner<256;++owner)for(unsigned row=0;row<4096;++row){
        S::Presentation out{sentinel,0x1234};
        const bool ok=S::Resolve(static_cast<std::uint16_t>(row),static_cast<std::uint8_t>(owner),false,out);
        CHECK(ok==(owner==7&&row<171));
        if(!ok){CHECK(out.text==sentinel&&out.model==0x1234);continue;}
        CHECK(out.text&&out.model==(row<74?0x4066:0x4067));
        std::size_t length=0;for(;length<S::NameCapacity&&out.text[length];++length){CHECK(out.text[length]>=0x30);}
        CHECK(length>0&&length<S::NameCapacity);
    }
    for(unsigned prefix=0;prefix<16;++prefix)for(unsigned row=0;row<171;++row){
        S::Presentation full{},base{},shortName{};
        CHECK(S::Resolve(static_cast<std::uint16_t>((prefix<<12)|row),7,false,full));
        CHECK(S::Resolve(static_cast<std::uint16_t>(row),7,false,base)&&full.text==base.text&&full.model==base.model);
        CHECK(S::Resolve(static_cast<std::uint16_t>(row),7,true,shortName));
        CHECK(shortName.model==base.model);
        CHECK(shortName.text==base.text);
    }
    constexpr std::array<std::uint8_t,8> scepter={0x62,0x72,0x74,0x7f,0x83,0x74,0x81,0};
    CHECK(std::memcmp(S::Names[1].data(),scepter.data(),scepter.size())==0);
    S::Presentation a{},b{};CHECK(S::Resolve(0,7,false,a));CHECK(S::Resolve(74,7,false,b));
    CHECK(a.text!=b.text&&a.model==0x4066&&b.model==0x4067);
    std::printf("SeymourGearPresentationRt0: %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SeymourGearPresentationCore is not implemented");return 1;}
#endif
