// Jarvis-HOOK: native PE builder, root controller, descriptor registry and pump.
// Only peripheral resource/input/graphics endpoints are substituted. No game,
// save file or imported runtime is started by this private x86 fixture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <thread>
#include <cstring>
#include <cstdint>
#include "WorkshopMainPeFixture.inc"
#include "../hooks/EquipmentWorkshopMainMenu.cpp"
namespace M=FfxHooks::EquipmentWorkshop::MainMenu;
namespace {
unsigned checks=0,failures=0,opens=0,closes=0,backgrounds=0,layers=0,story=0;
unsigned rowPanels=0,rowTexts=0,rowCursors=0;float rowY=0,rowV=0,rowVEnd=0,textScaleX=0,textScaleY=0;
bool ready=true,busy=false,openSucceeds=true;
DWORD thread=0;std::uint32_t extendedBankBytes=0;
std::uint32_t BankSize(unsigned,std::uintptr_t){return extendedBankBytes;}
unsigned char* pe=nullptr;
unsigned char window[152]{};
int edge=0,direction=0;
const char* phase="setup";
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s: %s\n",phase,why);}}
LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("FAULT %s code=%08lX rva=%08lX address=%08lX base=%p eax=%08lX ecx=%08lX edx=%08lX\n",phase,e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(pe)),e->ExceptionRecord->NumberParameters>1?static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]):0,pe,e->ContextRecord->Eax,e->ContextRecord->Ecx,e->ContextRecord->Edx);for(unsigned i=0;i<16;++i)std::printf(" %08X",reinterpret_cast<unsigned*>(e->ContextRecord->Esp)[i]);std::puts("");std::fflush(stdout);return EXCEPTION_CONTINUE_SEARCH;}
void Log(const char* s){std::fputs(s,stdout);}
template<class T> T& At(unsigned rva){return *reinterpret_cast<T*>(pe+rva);}
void Jump(unsigned rva,void* target){pe[rva]=0xE9;At<unsigned>(rva+1)=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(pe)-rva-5);}
int __cdecl Quiet(){return 0;}
void* __cdecl Clear(void* target,int value,std::size_t size){return std::memset(target,value,size);}
int __cdecl Edge(){return edge;}
int __cdecl Direction(){return direction;}
int __cdecl Story(){return static_cast<int>(story);}
int __cdecl Two(){return 2;}
int __cdecl Aeons(){return 1;}
int __cdecl Allocate(){std::memset(window,0,sizeof(window));return static_cast<int>(reinterpret_cast<std::uintptr_t>(window));}
int __cdecl Register(int obj){*reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(obj)+64)=1;return obj;}
void __cdecl Update(int layer){if(layer==7&&window[64])reinterpret_cast<int(__cdecl*)(void*)>(pe+0x4E2040)(window);}
void __cdecl Background(){++backgrounds;}
void __cdecl Layer(int){++layers;}
float __cdecl Scale(float value){return value;}
void __cdecl RowPanel(unsigned atlas,float,float y,float,float,float,float v,float,float vEnd,unsigned,unsigned){
 if(atlas==0xFFFFFFFDu){++rowPanels;rowY=y;rowV=v;rowVEnd=vEnd;}}
int __cdecl RowMeasure(const unsigned char*,float* width,int,float){*width=120;return 1;}
int __cdecl RowText(const unsigned char* text,float,float,int,float sx,float sy,unsigned char){textScaleX=sx;textScaleY=sy;if(text==M::title)++rowTexts;return 1;}
int __cdecl RowCursor(float,float,int){++rowCursors;return 1;}
const unsigned char* __cdecl NativeLabel(int id){Check(id!=0x7FFF,"virtual label never reaches the native resource lookup");return reinterpret_cast<const unsigned char*>("Native");}
bool Ready(){return ready&&GetCurrentThreadId()==thread&&!busy;}
bool Open(){++opens;if(openSucceeds)busy=true;return openSucceeds;}
bool Busy(){return busy;}
bool Close(){++closes;busy=false;return true;}
void Label(bool help,unsigned char* out,std::size_t capacity){strcpy_s(reinterpret_cast<char*>(out),capacity,help?"Refine equipment.":"Workshop");}
M::Ui ui{Ready,Open,Busy,Close,Label,BankSize};
M::Descriptor* Root(){return reinterpret_cast<M::Descriptor*>(pe+M::rootRva);}
void Tick(){reinterpret_cast<void(__cdecl*)()>(pe+0x4AA200)();M::Pump();}
void Build(int mode=1){phase="native builder";reinterpret_cast<int(__cdecl*)(int,int)>(pe+0x4E1C60)(0,mode);}
void RegisterRoot(){
 At<unsigned>(0xF40824)=1;At<unsigned>(0x14408F8)=0;
 reinterpret_cast<void(__cdecl*)(int,void*)>(pe+0x4AAAB0)(1,Root());
 Build();M::NativeActivate(1);Root()->state=5;At<int>(0x1471558)=4096;
}
void Confirm(){phase="native root confirm";At<unsigned>(0x1471548)=M::nativeCount;edge=0x20;Tick();edge=0;}
void AwaitOpen(){for(unsigned i=0;i<16&&!busy;++i)Tick();}
void RemoveRoot(){reinterpret_cast<void(__cdecl*)(int)>(pe+0x4AAB20)(1);M::Pump();}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;AddVectoredExceptionHandler(1,Fault);thread=GetCurrentThreadId();
 pe=MapPe(Read(argv[1]));if(!pe)return 2;const auto base=reinterpret_cast<std::uintptr_t>(pe);
 Check(!M::Start(base,false,false,ui,Log),"OFF does not install hooks");
 Check(!M::Start(base,true,true,ui,Log),"validation-only does not install hooks");
 for(const auto& span:M::Evidence::spans)for(unsigned i=0;i<32;++i){pe[span.rva+i]^=1;Check(!M::Evidence::Profile(base),"each witnessed byte is gated");pe[span.rva+i]^=1;}
 Check(M::Start(base,true,false,ui,Log),"real MinHook family installs on the private Steam PE");
 if(!M::Active())return 2;
 // Keep complete builder/controller/registry/pump bodies and all hook targets.
 // Font/resource/sound/party services do not exist in this non-running image.
 for(unsigned rva:{0x4BE410u,0x486E80u,0x4AAD60u,0x4AAEB0u,0x4AA410u,0x4C30D0u,
     0x4A9680u,0x4AEEC0u,0x4AF380u,0x4BE3D0u,0x4AF5C0u,0x4AE0F0u,0x46BE50u,
     0x4AB1E0u,0x4DC9E0u,0x54923Au,0x4E0BA0u,0x4DCE30u,0x4DD440u,0x241060u,
     0x4F5C60u,0x4DCE50u,0x48EC60u,0x4AA000u})Jump(rva,reinterpret_cast<void*>(Quiet));
 Jump(0x4BE470,reinterpret_cast<void*>(Direction));Jump(0x4BE4B0,reinterpret_cast<void*>(Edge));
 Jump(0x46C470,reinterpret_cast<void*>(Story));Jump(0x4A9B50,reinterpret_cast<void*>(Two));
 Jump(0x4AB1E0,reinterpret_cast<void*>(Aeons));
 Jump(0x4AA1A0,reinterpret_cast<void*>(Allocate));Jump(0x4AAB00,reinterpret_cast<void*>(Register));
 Jump(0x4A9230,reinterpret_cast<void*>(Update));Jump(0x4F5C10,reinterpret_cast<void*>(Background));
 Jump(0x4A9690,reinterpret_cast<void*>(Layer));
 Jump(0x54964C,reinterpret_cast<void*>(Clear));
 Jump(0x2447E0,reinterpret_cast<void*>(Scale));Jump(0x244820,reinterpret_cast<void*>(Scale));
 Jump(0x503EE0,reinterpret_cast<void*>(RowPanel));Jump(0x505290,reinterpret_cast<void*>(RowMeasure));
 Jump(0x505970,reinterpret_cast<void*>(RowText));Jump(0x4C0670,reinterpret_cast<void*>(RowCursor));
 M::originals[M::Name]=reinterpret_cast<void*>(NativeLabel);M::originals[M::Help]=reinterpret_cast<void*>(NativeLabel);
 FlushInstructionCache(GetCurrentProcess(),pe,0x700000);
 for(unsigned progress:{1u,70u,215u,1100u,0u}){
  story=progress;RegisterRoot();phase="native root options";
  Check(M::View(),"only the standard enabled root acquires the free child slot");
  std::vector<unsigned char> names(160);auto put=[&](unsigned at,unsigned value){names[at]=static_cast<unsigned char>(value);names[at+1]=static_cast<unsigned char>(value>>8);};
  names[0]=1;put(8,23);put(10,23);put(12,108);put(14,108);put(16,20);
  names[128]=0x51;names[129]=0x81;names[130]=0;
  At<unsigned>(0xD2A944)=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(names.data()));At<unsigned short>(0xD2A970)=static_cast<unsigned short>(names.size());
  unsigned char name[32]{};std::size_t length=0;
  Check(M::AbilityName(0x8017,name,sizeof(name),length)&&length==2&&name[0]==0x51,"native menu reads the actual table name for a modified vanilla ID");
  Check(!M::AbilityName(0x8018,name,sizeof(name),length),"out-of-range ability does not borrow an adjacent row");
  put(20,160);Check(!M::AbilityName(0x8017,name,sizeof(name),length),"unbounded name offset is rejected");put(20,0);
  names[128]=3;Check(!M::AbilityName(0x8017,name,sizeof(name),length),"native control stream never becomes a plain label");names[128]=0x51;
  bool foreignName=false;std::thread otherThread([&]{foreignName=M::AbilityName(0x8017,name,sizeof(name),length);});otherThread.join();
  Check(!foreignName,"name lookup is owned by the native menu thread");
  names.resize(70000);put(20,65535);names[65663]=0x52;names[65664]=0;
  At<unsigned>(0xD2A944)=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(names.data()));At<unsigned short>(0xD2A970)=static_cast<unsigned short>(names.size());
  extendedBankBytes=static_cast<std::uint32_t>(names.size());
  Check(M::AbilityName(0x8017,name,sizeof(name),length)&&length==1&&name[0]==0x52,"32-bit translated bank names survive the native truncated size field");
  extendedBankBytes=0;At<unsigned>(0xD2A944)=0;
  const unsigned count=At<unsigned>(0x1471688);std::vector<unsigned char> records(pe+0x1471568,pe+0x1471668);
  Check(M::CountShim()==static_cast<int>(count+1),"one extra row follows every progression-dependent native list");
  for(unsigned i=0;i<count;++i)Check(M::IdShim(i)==static_cast<int>(At<unsigned>(0x1471568+8*i)),"all original option identities remain intact");
  Check(M::IdShim(count)==0x7FFF&&M::KindShim(count)==0&&M::EnabledShim(count)==1,"Workshop opens directly without character targeting");
  Check(M::NameShim(0x7FFF)==M::title&&M::HelpShim(0x7FFF)==M::help,"Workshop native name/help use owned persistent buffers");
  Check(M::DrawText(M::title,0,0,60,true)&&textScaleX==.26f&&textScaleY==.35f,"translated labels fit their native column in both axes");
  Check(!M::DrawText(M::title,0,0,0,true),"invalid label width never reaches the renderer");
  At<unsigned>(0x1471548)=0;direction=0x1000;Tick();direction=0;
  Check(At<unsigned>(0x1471548)==count,"native up wraps onto Workshop while retaining caller registers");
  direction=0x4000;Tick();direction=0;
  Check(At<unsigned>(0x1471548)==0,"native down wraps from Workshop to the first row");
  At<unsigned>(0x1471548)=count;rowPanels=rowTexts=rowCursors=0;
  reinterpret_cast<int(__cdecl*)(int,float,int,int,int)>(pe+0x4E22A0)(count,0.f,0,0,4096);
  Check(rowPanels==1&&rowTexts==1&&rowCursors==1,"Workshop draws its stock native button, text and selection cursor");
  Check(rowY==194.f+75.f*count&&rowV==0&&rowVEnd==67,"appended row keeps its position but uses an existing authored atlas strip");
  int foreignCount=0;std::thread other([&]{foreignCount=M::CountShim();});other.join();Check(foreignCount==static_cast<int>(count),"foreign threads never see the private row");
  const unsigned previous=opens;Confirm();
  Check(!Root()->controlActive&&M::descriptor.controlActive&&Root()->state==26,"native confirm suspends the parent and waits for the child");
  Check(opens==previous,"the immediate pre-publication activation callback cannot open early");
  AwaitOpen();Check(busy&&opens==previous+1,"one Workshop opens after the actual native fade reaches zero");
  Check(std::memcmp(records.data(),pe+0x1471568,records.size())==0,"native option records are not rewritten by the mod");
  phase="native child draw";At<unsigned char>(0xF40859)=1;backgrounds=layers=0;
  reinterpret_cast<void(__cdecl*)()>(pe+0x4AA290)();
  Check(backgrounds==1&&layers==9,"native draw dispatcher renders the child before window layers");
  phase="native cancel return";busy=false;Tick();
  Check(Root()->controlActive&&!M::descriptor.controlActive&&Root()->state==2,"closing Workshop resumes the actual native root transition");
  Check(At<unsigned>(0x1471548)==count,"normal return preserves Workshop selection");
  Tick();Tick();Check(Root()->state==5,"native root accepts input again");
  RemoveRoot();Check(!M::attached.load()&&!At<std::uintptr_t>(M::registryRva+4*M::slot),"root removal unregisters only the private child");
 }
 phase="special menu gates";
 for(int mode:{12,13,14,15,16,24}){RegisterRoot();Build(mode);Check(!M::attached.load()&&M::CountShim()==1,"scripted single-option menus receive no extra row");RemoveRoot();}
 phase="foreign child owner";M::Descriptor foreign{};
 At<std::uintptr_t>(M::registryRva+4*M::slot)=reinterpret_cast<std::uintptr_t>(&foreign);RegisterRoot();
 Check(!M::attached.load()&&At<M::Descriptor*>(M::registryRva+4*M::slot)==&foreign,"an occupied slot is never stolen");RemoveRoot();At<std::uintptr_t>(M::registryRva+4*M::slot)=0;
 RegisterRoot();openSucceeds=false;Confirm();for(unsigned i=0;i<16;++i)Tick();
 Check(Root()->controlActive&&!busy,"failed UI allocation returns control to the native parent");openSucceeds=true;RemoveRoot();
 RegisterRoot();Confirm();AwaitOpen();phase="foreign logical stop";
 bool stopped=true;std::thread stop([&]{stopped=M::StopReady();});stop.join();
 Check(!stopped&&busy,"foreign stop requests owner cleanup without writing the native menu");Tick();
 Check(!busy&&!M::attached.load()&&Root()->controlActive,"owner pump drains the UI and resumes parent after stop");
 Check(At<unsigned>(0x1471548)<At<unsigned>(0x1471688),"stop cannot leave a virtual selection beyond the native rows");
 Check(M::StopReady()&&M::StopReady(),"stop is repeatable and nonblocking");RemoveRoot();
 std::printf("EquipmentWorkshopMainMenuRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
