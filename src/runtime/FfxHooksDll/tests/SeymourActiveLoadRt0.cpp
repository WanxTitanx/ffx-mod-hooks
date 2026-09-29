#include <cstdio>
#include <cstring>
#if __has_include("../hooks/SeymourActiveLoadCore.h")
#include "../hooks/SeymourActiveLoadCore.h"
namespace S=FfxHooks::SeymourSession;
namespace R=FfxHooks::RonsoPool;
namespace {
unsigned total=0,failed=0;
#define CHECK(x) do{++total;if(!(x)){++failed;if(failed<25)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Fixture {
    R::SaveImage source{};
    std::array<unsigned char,R::kSaveSize-64> live{};
    bool reads=true;
    S::ActiveLoad core;
    Fixture():core(reinterpret_cast<std::uintptr_t>(live.data())) {
        for(unsigned i=64;i<source.size();++i)source[i]=static_cast<unsigned char>(i*17);
        R::SealSave(source);CopyNative();
    }
    void CopyNative(){std::memcpy(live.data(),source.data()+64,live.size());}
    static bool Copy(void* p,void* output,const void* input,std::size_t size){
        auto& f=*static_cast<Fixture*>(p);
        if(!f.reads||!output||!input)return false;
        std::memcpy(output,input,size);return true;
    }
    S::ActiveLoad::Io Io(){return {this,Copy};}
    void Begin(unsigned cookie=1,unsigned thread=7){core.Begin(cookie,live.data(),source.data(),thread,Io());}
};
}
int main(){
    {Fixture f;CHECK(!f.core.Capture(7).Valid());f.Begin();CHECK(!f.core.Capture(7).Valid());
     CHECK(f.core.End(1,true,7,f.Io()));auto token=f.core.Capture(7);CHECK(token.Valid());
     CHECK(f.core.Current(token,7));CHECK(!f.core.Capture(8).Valid());
     CHECK(!f.core.End(1,true,7,f.Io())&&f.core.Current(token,7));
     unsigned char other[8]{};f.core.Begin(2,other,nullptr,7,f.Io());CHECK(f.core.Current(token,7));
     f.core.Begin(3,f.live.data(),nullptr,7,f.Io());CHECK(!f.core.Capture(7).Valid());
     CHECK(!f.core.End(3,true,7,f.Io())&&!f.core.Current(token,7));}
    for(unsigned mode=0;mode<9;++mode){Fixture f;f.Begin();
     if(mode==0)f.live.front()^=1;
     if(mode==1)f.live.back()^=1;
     if(mode==2)f.live[0x5900]^=1;
     if(mode==3)f.reads=false;
     if(mode==4)f.core.Stop();
     if(mode==5)f.core.Invalidate();
     if(mode==6)f.Begin(2);
     CHECK(!f.core.End(1,mode!=7,mode==8?8:7,f.Io()));CHECK(!f.core.Capture(7).Valid());}
    {Fixture f;f.source[100]^=1;f.CopyNative();f.Begin();CHECK(!f.core.End(1,true,7,f.Io()));}
    {Fixture f;f.source[25844]=f.source[25845]=f.source[25846]=f.source[25847]=0;
     f.CopyNative();f.Begin();CHECK(f.core.End(1,true,7,f.Io()));}
    {Fixture f;f.Begin(1);f.Begin(2);CHECK(!f.core.End(2,true,7,f.Io()));
     CHECK(!f.core.End(1,true,7,f.Io()));CHECK(!f.core.Capture(7).Valid());
     f.Begin(3);CHECK(f.core.End(3,true,7,f.Io()));auto token=f.core.Capture(7);
     f.core.Stop();CHECK(!f.core.Capture(7).Valid());CHECK(f.core.Current(token,7,true));
     f.Begin(4);CHECK(!f.core.Current(token,7,true));}
    {Fixture f;CHECK(f.core.Reset(7));auto token=f.core.Capture(7);CHECK(token.Valid());
     f.Begin();CHECK(!f.core.Reset(7));CHECK(!f.core.End(1,true,7,f.Io()));}
    for(unsigned round=0;round<2048;++round){Fixture f;f.source[200]=static_cast<unsigned char>(round);R::SealSave(f.source);
     f.CopyNative();f.Begin();CHECK(f.core.End(1,true,7,f.Io()));auto first=f.core.Capture(7);
     f.source[300]^=1;R::SealSave(f.source);f.Begin(2);CHECK(!f.core.Current(first,7));
     CHECK(!f.core.End(2,true,7,f.Io()));f.CopyNative();f.Begin(3);CHECK(f.core.End(3,true,7,f.Io()));}
    std::printf("SeymourActiveLoadRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: active save-copy/readback adapter core is missing");return 1;}
#endif
