#include <cstdio>
#include <cstdint>
#include <limits>
#if __has_include("../hooks/SeymourSessionCore.h")
#include "../hooks/SeymourSessionCore.h"
namespace S=FfxHooks::SeymourSession;
static unsigned total=0,failed=0;
#define CHECK(x) do{++total;if(!(x)){++failed;if(failed<30)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
int main(){
    {S::Core c;CHECK(!c.Capture(1).Valid());CHECK(!c.Current({},1));
     auto a=c.Begin(1,7);CHECK(a.Valid()&&!c.Capture(7).Valid());
     CHECK(c.End(a,true,7));auto t=c.Capture(7);CHECK(t.Valid()&&c.Current(t,7)&&!c.Current(t,8));
     CHECK(!c.End(a,true,7)&&c.Current(t,7));
     auto b=c.Begin(2,7);CHECK(b.Valid()&&!c.Current(t,7));CHECK(!c.End(b,false,7));CHECK(!c.Capture(7).Valid());}
    {S::Core c;auto a=c.Begin(1,7);auto b=c.Begin(2,7);
     CHECK(!c.End(b,true,7)&&!c.Capture(7).Valid());CHECK(!c.End(a,true,7)&&!c.Capture(7).Valid());
     auto d=c.Begin(3,7);CHECK(c.End(d,true,7)&&c.Capture(7).Valid());}
    {S::Core c;auto a=c.Begin(1,7);auto b=c.Begin(2,8);
     CHECK(!c.End(a,true,7));CHECK(c.End(b,true,8));CHECK(!c.Capture(7).Valid()&&c.Capture(8).Valid());}
    {S::Core c;auto a=c.Begin(1,7);CHECK(!c.End(a,true,8));CHECK(!c.Capture(7).Valid()&&!c.Capture(8).Valid());}
    {S::Core c;CHECK(c.Reset(7));auto before=c.Capture(7);CHECK(before.Valid());
     auto bad=c.Begin(1,0);CHECK(!bad.Valid()&&!c.Current(before,7));
     CHECK(c.Reset(7));auto good=c.Begin(2,7);CHECK(good.Valid());
     CHECK(!c.Reset(7));CHECK(!c.End(good,true,7));CHECK(!c.Capture(7).Valid());CHECK(c.Reset(7));}
    {S::Core c;auto a=c.Begin(1,7);auto fake=a;fake.revision++;
     CHECK(!c.End(fake,true,7));CHECK(!c.End(a,true,7));CHECK(!c.Capture(7).Valid());}
    {S::Core c;CHECK(c.Reset(7));auto t=c.Capture(7);c.Invalidate();CHECK(!c.Current(t,7));
     CHECK(c.Reset(7));CHECK(c.Capture(7).revision>t.revision);}
    {S::Core c;std::array<S::Attempt,S::Capacity> p{};for(unsigned i=0;i<S::Capacity;++i){p[i]=c.Begin(i+1,7);CHECK(p[i].Valid());}
     CHECK(!c.Begin(S::Capacity+1,7).Valid());CHECK(c.Poisoned());for(auto a:p)CHECK(!c.End(a,true,7));
     CHECK(!c.Reset(7)&&!c.Capture(7).Valid());}
    {S::Core c;CHECK(c.Reset(7));auto t=c.Capture(7);auto a=c.Begin(42,7);CHECK(a.Valid());
     CHECK(!c.Begin(42,7).Valid());CHECK(!c.End(a,true,7));CHECK(!c.Current(t,7));}
    {S::Core c;CHECK(c.Reset(7));auto t=c.Capture(7);c.Stop();CHECK(!c.Capture(7).Valid()&&!c.Current(t,7));
     CHECK(c.Capture(7,true)==t&&c.Current(t,7,true));CHECK(!c.Reset(7));
     auto a=c.Begin(1,7);CHECK(a.Valid());CHECK(!c.Current(t,7,true));CHECK(!c.End(a,true,7));CHECK(!c.Capture(7,true).Valid());}
    for(unsigned repeat=0;repeat<8192;++repeat){S::Core c;auto a=c.Begin(1,repeat%31+1);
     CHECK(c.End(a,true,repeat%31+1));auto t=c.Capture(repeat%31+1);
     CHECK(t.Valid());auto b=c.Begin(2,repeat%31+1);CHECK(!c.Current(t,repeat%31+1));
     CHECK(c.End(b,true,repeat%31+1));CHECK(c.Capture(repeat%31+1).revision>t.revision);}
    std::printf("SeymourSessionRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SeymourSessionCore is missing");return 1;}
#endif
