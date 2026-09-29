#include <cstdio>
#include <thread>
#include <array>
#if __has_include("../hooks/SeymourOverdriveControl.h")
#include "../hooks/SeymourOverdriveControl.h"
using FfxHooks::SeymourOverdrive::Control;
int main(){
 unsigned total=0,failed=0;auto check=[&](bool ok){++total;if(!ok)++failed;};
 Control c;check(!c.Current(c.Read()));check(c.Publish(false));check(c.Read()==0);
 for(unsigned i=0;i<4096;++i){
  check(c.Publish(true));const auto before=c.Read();check(c.Current(before));
  check(c.Publish(true));check(c.Read()==before);check(c.Publish(false));check(!c.Current(before));
  check(c.Publish(true));check(!c.Current(before));check(c.Current(c.Read()));check(c.Publish(false));
 }
 const auto old=c.Read();c.Stop();check(!c.Publish(true));check(!c.Current(old));
 std::array<std::thread,8> workers;std::atomic<unsigned> accepted{0};
 for(auto& w:workers)w=std::thread([&]{for(unsigned i=0;i<10000;++i)if(c.Publish(true))++accepted;});
 for(auto& w:workers){w.join();}
 check(accepted.load()==0);check(!c.Current(c.Read()));
 std::printf("SeymourOverdriveControlRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: SeymourOverdriveControl is missing");return 1;}
#endif
