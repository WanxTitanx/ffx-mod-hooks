#include "../../BattlePhotoMode/PhotoModeCore.h"
#include <cstdio>
#include <limits>
#include <string>
namespace P=FfxHooks::Photo;
namespace {int checks=0,failed=0;
void Check(bool v,const char* text){++checks;if(!v){++failed;std::fprintf(stderr,"FAIL %s\n",text);}}
struct Fixture {
 P::Frame frame{1,0x1000,9};
 P::Actor actor{{0,0x2000,3},{1,2,3,0,1,2,3}};
 P::Camera camera{10,20,30};unsigned writes=0;bool valid=true,fail=false;
 static bool Capture(void* c,P::Capture& out) noexcept {
  auto& f=*static_cast<Fixture*>(c);if(!f.valid)return false;
  out={};out.frame=f.frame;out.actors[0]=f.actor;out.count=1;out.camera=f.camera;return true;
 }
 static bool ReadActor(void* c,const P::Frame& frame,const P::Identity& id,P::Pose& out) noexcept {
  auto& f=*static_cast<Fixture*>(c);if(!f.valid||!(frame==f.frame)||!(id==f.actor.id))return false;
  out=f.actor.pose;return true;
 }
 static bool WriteActor(void* c,const P::Frame& frame,const P::Identity& id,const P::Pose& expected,const P::Pose& desired) noexcept {
  auto& f=*static_cast<Fixture*>(c);P::Pose value{};
  if(f.fail||!ReadActor(c,frame,id,value)||!(value==expected))return false;
  f.actor.pose=desired;++f.writes;return true;
 }
 static bool ReadCamera(void* c,const P::Frame& frame,P::Camera& out) noexcept {
  auto& f=*static_cast<Fixture*>(c);if(!f.valid||!(frame==f.frame))return false;out=f.camera;return true;
 }
 static bool WriteCamera(void* c,const P::Frame& frame,const P::Camera& expected,const P::Camera& desired) noexcept {
  auto& f=*static_cast<Fixture*>(c);P::Camera value{};
  if(f.fail||!ReadCamera(c,frame,value)||!(value==expected))return false;
  f.camera=desired;++f.writes;return true;
 }
 P::Io Io(){return {this,Capture,ReadActor,WriteActor,ReadCamera,WriteCamera};}
};}
int main(){
 Fixture f;auto io=f.Io();P::Session session;
 Check(!session.Active()&&!session.Move(io,1,0,0),"off mode never writes");
 Check(session.Begin(io),"valid native snapshot starts session");
 Check(session.Count()==1&&session.Selected()==0,"bounded actor selection");
 Check(session.Move(io,2,-1,4),"move selected actor");
 Check(f.actor.pose.x==3&&f.actor.pose.y==1&&f.actor.pose.z==7,"actor position updated");
 Check(f.actor.pose.rx==3&&f.actor.pose.ry==1&&f.actor.pose.rz==7,"render translation follows position delta");
 Check(session.Rotate(io,0.5f)&&f.actor.pose.yaw==0.5f,"yaw uses facing field");
 Check(session.Pan(io,1,2,3)&&f.camera.x==11&&f.camera.y==22&&f.camera.z==33,"camera target pan");
 Check(!session.Move(io,std::numeric_limits<float>::quiet_NaN(),0,0),"NaN input rejected");
 Check(!session.Move(io,100001,0,0),"unbounded input rejected");
 std::string json;
 Check(session.Export(json)&&json.find("\"schema\":\"ffx-hooks.photo-scene\"")!=std::string::npos,"scene export declares versioned schema");
 Check(json.find("\"refX\":11")!=std::string::npos&&json.find("\"id\":3")!=std::string::npos,"export contains camera and actor identity");
 Check(json.find("8192")==std::string::npos,"export excludes raw pointers");
 Check(session.Reset(io)&&f.actor.pose.x==1&&f.actor.pose.yaw==0&&f.camera.x==10,"owned reset restores captured values");
 Check(session.Move(io,1,0,0),"second edit");
 f.actor.pose.x=40;
 const auto before=f.writes;
 Check(!session.Reset(io)&&f.actor.pose.x==40,"foreign actor drift is not overwritten by reset");
 Check(f.writes==before,"conflicted actor restoration issues no writes");
 session.End(io);
 Check(!session.Active(),"end closes session");
 Check(session.Begin(io)&&session.Move(io,1,0,0),"new session can capture current state");
 ++f.frame.generation;
 const auto transitionWrites=f.writes;
 Check(!session.Tick(io)&&!session.Active(),"scene generation change closes admission");
 Check(f.writes==transitionWrites,"transition never writes stale actor storage");
 Check(session.Begin(io),"new generation capture");
 ++f.actor.id.value;
 Check(!session.Move(io,1,0,0),"reused address with a different actor identity rejected");
 session.End(io);
 f.valid=false;
 Check(!session.Begin(io)&&!session.Active(),"invalid capture never leaves mode ON");
 f.valid=true;f.actor.pose.x=0;f.actor.pose.y=0;f.actor.pose.z=0;
 Check(session.Begin(io),"origin position is not mistaken for absent actor");
 f.fail=true;
 Check(!session.Move(io,1,0,0),"failed write is reported");
 f.fail=false;session.End(io);
 std::printf("RecoveryPhotoModeRt0: %d/%d passed\n",checks-failed,checks);
 return failed?1:0;
}
