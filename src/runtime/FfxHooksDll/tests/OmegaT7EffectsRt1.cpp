#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/OmegaT7Native.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
namespace O=FfxHooks::OmegaT7;namespace N=O::Native;namespace P=FfxHooks::ExecutableProfile;
static unsigned checks=0,failures=0;
static std::uintptr_t testBase=0;static const char* stage="load";
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e){std::printf("OMEGA_EFFECT_EXCEPTION stage=%s code=%08lX rva=%08lX\n",stage,e->ExceptionRecord->ExceptionCode,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-testBase));return EXCEPTION_CONTINUE_SEARCH;}
static void Check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL %s\n",text);}}
static std::vector<unsigned char> ReadFileBytes(const char* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Put(void* p,unsigned value,unsigned n=4){std::memcpy(p,&value,n);}
static void Patch(void* p,const void* data,unsigned size){DWORD old=0,ignored=0;VirtualProtect(p,size,PAGE_EXECUTE_READWRITE,&old);std::memcpy(p,data,size);FlushInstructionCache(GetCurrentProcess(),p,size);VirtualProtect(p,size,old,&ignored);}
int main(int argc,char** argv){
    if(argc!=3)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,Fault);
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image){std::printf("PE_LOAD_FAILED %lu\n",GetLastError());return 2;}
    Check(PrivatePeFixture::NormalizeRelocations(image),"exact private Steam image relocates");const auto base=reinterpret_cast<std::uintptr_t>(image);testBase=base;
    auto text=ReadFileBytes(argv[2]);std::string manifest(text.begin(),text.end());const auto at=manifest.find("false");if(at==std::string::npos)return 2;manifest.replace(at,5,"true");
    O::Bindings bindings;Check(O::ParseBindings(manifest,bindings)==O::BindingCode::Ready,"all 26 installed command rows meet the approved semantic contract");
    if(!bindings.Ready())return 1;
    std::vector<unsigned char> actors(31*0xF90),resource(4096);
    Put(reinterpret_cast<void*>(base+P::Rva<0xD334CC>()),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(actors.data())));
    const auto actor=[&](unsigned id){return actors.data()+id*0xF90;};
    for(unsigned id:{0u,20u,21u}){auto* p=actor(id);p[0xDC8]=p[0xDC9]=1;Put(p+0xC,id,2);Put(p+0xE,id<18?id:0x1000+83,2);
        Put(p+0x48,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(resource.data())));Put(p+0x594,1000);Put(p+0x5D0,1000);}
    stage="compare import";const auto compare=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&std::strcmp));Patch(reinterpret_cast<void*>(base+0x70C378),&compare,4);
    FfxHooks::SharedCombat::originals[2]=reinterpret_cast<void*>(base+P::Rva<0x38E2F0>());
    const auto ptr=reinterpret_cast<std::uintptr_t>(actor(20));
    for(unsigned i=0;i<4;++i)actor(20)[0x65E+O::NativeStackIndex[i]]=4;
    std::array<unsigned char,8> baseStats{};std::memcpy(baseStats.data(),actor(20)+0x5A8,8);
    stage="stacks";N::Stacks(base,20,ptr,{2,2,2,2});const auto full=N::Life(ptr);
    for(unsigned i=0;i<4;++i)Check(full.stacks[i]==5,"actual native stat-buff leaf clamps every approved counter to five");
    Check(!std::memcmp(baseStats.data(),actor(20)+0x5A8,8),"buff counters never inflate base attributes");
    stage="temporary buffs";actor(20)[0x60B]=7;N::Grant(base,20,ptr,O::Shell|O::Protect|O::Regen|O::Haste);
    Check(actor(20)[0x60B]==7&&actor(20)[0x60C]==254&&actor(20)[0x612]==254&&actor(20)[0x613]==254,"native positive grants are missing-only and removable");
    N::Grant(base,20,ptr,O::NulBlaze|O::NulFrost|O::NulShock|O::NulTide);
    Check(actor(20)[0x60E]==1&&actor(20)[0x60F]==1&&actor(20)[0x610]==1&&actor(20)[0x611]==1,"four native Nuls receive one removable block in their correct fields");
    stage="ctb";actor(20)[0x65C]=100;N::Advance(base,20,ptr,20);Check(actor(20)[0x65C]==80,"actual native CTB leaf advances the remaining gauge twenty percent");
    stage="absorb";actor(20)[0x5DA]=128;N::BasicAbsorb(base,20,O::Fire,true);Check(actor(20)[0x5DA]==129,"temporary basic absorption preserves native Dark affinity");
    N::BasicAbsorb(base,20,O::Fire,false);Check(actor(20)[0x5DA]==128,"native affinity restoration clears only the temporary bit");
    stage="healing";Put(actor(20)+0x5D0,950);N::Heal(base,20,ptr,15);Check(N::ReadOr<unsigned>(ptr+0x5D0)==1000,"native conditional healing respects maximum HP");
    Put(actor(20)+0x606,2,2);Put(actor(20)+0x5D0,500);N::Heal(base,20,ptr,20);Check(N::ReadOr<unsigned>(ptr+0x5D0)==300,"Zombie turns conditional recovery into damage");
    stage="zombie KO";Put(actor(20)+0x5D0,100);N::Heal(base,20,ptr,20);Check(!N::ReadOr<unsigned>(ptr+0x5D0)&&(N::ReadOr<unsigned short>(ptr+0x606)&1),"lethal Zombie recovery uses the native KO path");
    Put(actor(20)+0x606,0,2);actor(20)[0xDCE]=0;Put(actor(20)+0x5D0,1000);
    const unsigned char zeroRng[]={0x33,0xC0,0xC3};unsigned char oldRng[3]{};
    std::memcpy(oldRng,reinterpret_cast<void*>(base+P::Rva<0x398900>()),3);Patch(reinterpret_cast<void*>(base+P::Rva<0x398900>()),zeroRng,3);
    stage="slow";const auto row=bindings.Get(O::Skill::Slowga).row;
    actor(20)[0x614]=0;actor(20)[0x613]=19;actor(20)[0x659]=255;
    N::Slow(base,0,reinterpret_cast<std::uintptr_t>(actor(0)),20,ptr,row);
    Check(!actor(20)[0x614]&&actor(20)[0x613]==19,"native Slow Proof blocks the conditional attempt without clearing Haste");
    actor(20)[0x659]=0;N::Slow(base,0,reinterpret_cast<std::uintptr_t>(actor(0)),20,ptr,row);
    Check(actor(20)[0x614]==254&&!actor(20)[0x613],"accepted native Slow removes its incompatible temporary Haste");
    Patch(reinterpret_cast<void*>(base+P::Rva<0x398900>()),oldRng,3);
    std::printf("OmegaT7EffectsRt1: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
