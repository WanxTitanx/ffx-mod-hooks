#include <array>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#if __has_include("../hooks/SeymourPersistentRosterCore.h")
#include "../hooks/SeymourPersistentRosterCore.h"
namespace R=FfxHooks::SeymourPersistentRoster;
namespace {
unsigned total=0,failed=0;
#define CHECK(x) do{++total;if(!(x)){++failed;if(failed<25)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Fake {
 R::Image image{};R::Token token{1,7};unsigned calls=0,reads=0,currentCalls=0,failRead=0;
 bool field=true,session=true,failAssign=false,throwAssign=false,drift=false,revoke=false;
 Fake(){image.party=0x10;image.front={0,1,2};image.reserve.fill(255);for(unsigned i=0;i<4;++i)image.reserve[i]=static_cast<unsigned char>(i+3);}
 static bool Session(void* p,bool,R::Token& t){auto& f=*static_cast<Fake*>(p);if(!f.session)return false;t=f.token;return true;}
 static bool Current(void* p,const R::Token& t,bool){auto& f=*static_cast<Fake*>(p);++f.currentCalls;return f.session&&f.token==t&&f.field;}
 static bool Read(void* p,R::Image& out){auto& f=*static_cast<Fake*>(p);if(++f.reads==f.failRead)return false;out=f.image;return true;}
 static bool Assign(void* p,bool enable){auto& f=*static_cast<Fake*>(p);++f.calls;if(f.failAssign)return false;
  if(enable){auto at=std::find(f.image.reserve.begin(),f.image.reserve.end(),255);if(at==f.image.reserve.end())return false;*at=7;f.image.party=0x11;}
  else {for(auto& v:f.image.front)if(v==7)v=255;for(auto& v:f.image.reserve)if(v==7)v=255;f.image.party=0x10;}
  if(f.drift)f.image.reserve[0]=15;
  if(f.revoke)++f.token.revision;
  if(f.throwAssign)throw std::runtime_error("native callback");
  return true;
 }
 R::Io Ops(){return {this,Session,Current,Read,Assign};}
};
}
int main(){
 {Fake f;R::Lease lease;auto before=f.image;CHECK(lease.Update(false,f.Ops())==R::Outcome::Off);CHECK(!f.calls&&!f.reads);
 CHECK(lease.Update(true,f.Ops())==R::Outcome::Applied&&lease.Owned());CHECK(f.calls==1&&R::ExactAddition(before,f.image));
 CHECK(lease.Update(true,f.Ops())==R::Outcome::Owned&&f.calls==1);
 std::swap(f.image.front[1],f.image.reserve[4]);CHECK(f.image.front[1]==7);
 CHECK(lease.Update(true,f.Ops())==R::Outcome::Owned);
 CHECK(lease.Update(false,f.Ops())==R::Outcome::Restored&&!lease.Owned());CHECK(f.calls==2&&R::Restored(before,f.image));}
 {Fake f;Fake::Assign(&f,true);f.calls=0;R::Lease lease;auto before=f.image;
 CHECK(lease.Update(true,f.Ops())==R::Outcome::Borrowed&&!lease.Owned());CHECK(f.calls==0);
 CHECK(lease.Update(false,f.Ops())==R::Outcome::Off&&f.calls==0&&f.image==before);}
 {Fake f;R::Lease lease;auto before=f.image;CHECK(lease.Update(true,f.Ops())==R::Outcome::Applied);
 auto oldCalls=f.calls;f.token.revision++;f.image=before;CHECK(lease.Update(false,f.Ops())==R::Outcome::Off);
 CHECK(f.calls==oldCalls&&!lease.Owned()&&f.image==before);}
 for(unsigned mode=0;mode<6;++mode){Fake f;R::Lease lease;const auto before=f.image;
  if(mode==0)f.failAssign=true;
  if(mode==1)f.failRead=2;
  if(mode==2)f.drift=true;
  if(mode==3)f.revoke=true;
  if(mode==4)f.field=false;
  if(mode==5)f.session=false;
  const auto status=lease.Update(true,f.Ops());CHECK(status!=R::Outcome::Applied&&status!=R::Outcome::Owned&&status!=R::Outcome::Borrowed);
  if(mode==0||mode==4||mode==5)CHECK(f.image==before);
  if(mode==1||mode==2||mode==3)CHECK(lease.Owned());
 }
 {Fake f;R::Lease lease;f.throwAssign=true;bool caught=false;
  try{(void)lease.Update(true,f.Ops());}catch(const std::runtime_error&){caught=true;}
  CHECK(caught&&lease.Owned()&&f.calls==1);f.throwAssign=false;
  CHECK(lease.Update(false,f.Ops())==R::Outcome::Restored&&f.calls==2);}
 {Fake f;R::Lease lease;CHECK(lease.Update(true,f.Ops())==R::Outcome::Applied);
  f.field=false;auto before=f.image;CHECK(lease.Update(false,f.Ops())==R::Outcome::RestorePending&&f.image==before);
  f.field=true;f.image.reserve[0]=15;before=f.image;
  CHECK(lease.Update(false,f.Ops())==R::Outcome::RestorePending&&f.image==before&&f.calls==1);}
 for(unsigned byte=0;byte<256;++byte){Fake f;R::Lease lease;f.image.party=static_cast<unsigned char>(byte);auto before=f.image;
  auto status=lease.Update(true,f.Ops());if(byte==0x10)CHECK(status==R::Outcome::Applied);else CHECK(status==R::Outcome::Rejected&&f.calls==0&&f.image==before);}
 for(unsigned duplicate=0;duplicate<20;++duplicate){Fake f;R::Lease lease;
  if(duplicate<3)f.image.front[duplicate]=7;else f.image.reserve[duplicate-3]=7;
  auto before=f.image;CHECK(lease.Update(true,f.Ops())==R::Outcome::Rejected&&f.calls==0&&f.image==before);}
 for(unsigned round=0;round<4096;++round){Fake f;R::Lease lease;auto before=f.image;
  std::rotate(f.image.reserve.begin(),f.image.reserve.begin()+round%17,f.image.reserve.end());before=f.image;
  CHECK(lease.Update(true,f.Ops())==R::Outcome::Applied);
  CHECK(lease.Update(false,f.Ops())==R::Outcome::Restored&&R::Restored(before,f.image));}
 std::printf("SeymourPersistentRosterRt0: %u/%u passed\n",total-failed,total);return failed?1:0;
}
#else
int main(){std::puts("FAIL: persistent Seymour roster core is missing");return 1;}
#endif
