// Jarvis-HOOK: isolated mapped-PE producer test, not a game session.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/ExecutableProfile.h"
#include <windows.h>
#include "PrivatePeFixture.h"
#include "../hooks/VanguardRuntime.h"
#include "../hooks/VanguardUiBridge.h"
#include "../shared/Config.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace V=FfxHooks::Vanguard;
namespace C=FfxHooks::Config;
static unsigned checks=0,failures=0,rngCalls=0,rngValue=90;
static std::uintptr_t base=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
static void Log(const char* value){std::fputs(value,stdout);}
static unsigned __cdecl FixedRandom(unsigned){++rngCalls;return rngValue;}
static bool Patch(std::uintptr_t at,const void* bytes,unsigned size){
    DWORD old=0,ignored=0;if(!VirtualProtect(reinterpret_cast<void*>(at),size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(at),bytes,size);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),size);
    return VirtualProtect(reinterpret_cast<void*>(at),size,old,&ignored)!=FALSE;
}
static void W16(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
static void W32(unsigned char* p,int v){std::memcpy(p,&v,4);}
static std::vector<unsigned char> Kernel(){
    std::vector<unsigned char> data(20+148*108+1);W16(data.data(),1);W16(data.data()+10,147);W16(data.data()+12,108);W16(data.data()+14,148*108);W32(data.data()+16,20);
    for(const auto& entry:V::Abilities){W16(data.data()+20+entry.id*108,static_cast<unsigned>(data.size())-(20+148*108));
        for(const char* c=entry.label;*c;++c)data.push_back(*c==' '?58:*c=='\''?65:*c=='-'?71:static_cast<unsigned char>(*c+15));data.push_back(0);}
    return data;
}
#include "VanguardCostStatusCases.inl"
#include "VanguardAccuracyCases.inl"
#include "VanguardTurnCases.inl"
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;
    HMODULE image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    base=reinterpret_cast<std::uintptr_t>(image);Check(PrivatePeFixture::NormalizeRelocations(image),"private PE relocations");
    std::vector<unsigned char> actors(31*0xF90),kernel=Kernel();
    const auto actorAddress=reinterpret_cast<std::uintptr_t>(actors.data()),kernelAddress=reinterpret_cast<std::uintptr_t>(kernel.data());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),&actorAddress,4);std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A944>()),&kernelAddress,4);
    const auto size=static_cast<unsigned short>(kernel.size());std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),&size,2);
    *reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A8E0>())=1;
    for(unsigned i=0;i<31;++i){auto* a=actors.data()+i*0xF90;a[0xC]=static_cast<unsigned char>(i);W16(a+0xE,i<18?i:0x1000+i);
        a[0x592]=a[0x593]=255;a[0xDC8]=1;W32(a+0x594,10000);W32(a+0x598,999);W32(a+0x5D0,10000);W32(a+0x5D4,200);a[0x5A8]=32;a[0x5AA]=40;a[0x5BD]=100;}
    auto* source=actors.data();auto* target=actors.data()+0xF90;
    auto* pct=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0x1F11240>());std::memset(pct,0,31*4);
    auto* gear=reinterpret_cast<unsigned char*>(base+::FfxHooks::ExecutableProfile::Rva<0xD30F2C>());std::memset(gear,0,4400);
    std::array<unsigned char,96> command{};command[0x20]=1;command[0x23]=1;command[0x28]=4;command[0x2A]=16;
    using Percent=int(__cdecl*)(unsigned,unsigned,const unsigned char*,int);
    using Formula=int(__cdecl*)(const unsigned char*,const unsigned char*,const unsigned char*,int,int,unsigned,unsigned,int,int*,int*,int);
    using Critical=int(__cdecl*)(const unsigned char*,const unsigned char*,const unsigned char*,unsigned*,int);
    using Element=int(__cdecl*)(const unsigned char*,const unsigned char*,unsigned,int);
    const auto percent=reinterpret_cast<Percent>(base+::FfxHooks::ExecutableProfile::Rva<0x3892A0>());const auto formula=reinterpret_cast<Formula>(base+::FfxHooks::ExecutableProfile::Rva<0x389CB0>());
    const auto critical=reinterpret_cast<Critical>(base+::FfxHooks::ExecutableProfile::Rva<0x389750>());const auto element=reinterpret_cast<Element>(base+::FfxHooks::ExecutableProfile::Rva<0x38A420>());
    C::ResetForTests();C::LoadTextForTests("[vanguard]\n","C:\\private-vanguard-native.ini");
    unsigned char pristine[16]{};std::memcpy(pristine,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x3892A0>()),16);
    Check(!V::Start(base,true,Log)&&!V::Active(),"validate-only installs nothing");
    Check(!V::Start(base,false,Log)&&!V::Active()&&!std::memcmp(pristine,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x3892A0>()),16),"default-OFF leaves native code unchanged");
    std::string settings="[vanguard]\n";for(const auto& feature:V::Features)settings+=std::string(feature.key)+"=1\n";
    settings+="[f8_authority]\n";for(const auto& feature:V::Features)settings+=std::string("vanguard_")+feature.key+"=1\n";
    Check(C::LoadTextForTests(settings.c_str(),"C:\\private-vanguard-native.ini"),"complete authoritative fixture parses");target[0x5A9]=60;
    const int defended=formula(source,target,command.data(),1,16,0,1,0,nullptr,nullptr,0);
    const int raw=formula(source,target,command.data(),1,16,0x40,1,0,nullptr,nullptr,0);
    Check(V::Start(base,false,Log)&&V::Active(),"owned native Vanguard producers install");
    pct[1]=20;Check(percent(0,1,command.data(),1000)==1200,"formula MAG bonus on physical-class command");
    pct[1]=0;pct[6]=20;command[0x28]=1;
    Check(percent(0,1,command.data(),1000)==833,"EHP defense exactly once");
    Check(percent(0,1,command.data(),-1000)==-1000,"defense never mitigates healing");std::memset(pct,0,31*4);
    Check(formula(source,target,command.data(),1,16,0x40,1,0,nullptr,nullptr,0)==defended+raw/4,"additive Break in native formula");
    command[0x28]=4;command[0x20]=2;Check(formula(source,target,command.data(),4,16,0,1,0,nullptr,nullptr,0)==1728,"current MP adds ten MAG");
    target[0x5DD]=2;Check(element(target,command.data(),1,1000)==1250,"opposite weakness applies once");
    target[0x5DA]=1;Check(element(target,command.data(),1,1000)==-1000,"opposite weakness preserves absorption");target[0x5DD]=target[0x5DA]=0;
    V::MappingState mapping{};Check(V::ReadMapping(mapping),"bounded loaded kernel read");
    V::MappingState uiMapping{};Check(V::ReadUiMapping(uiMapping)&&uiMapping.stamp==mapping.stamp,"F8 service uses the actual native validation snapshot");
    for(unsigned i=0;i<V::AbilityCount;++i)Check(mapping.ids[i]==135+i&&mapping.codes[i]==V::MappingCode::Valid,"default hook-only binding validates");
    source[0x592]=0;gear[2]=1;gear[6]=0;gear[11]=4;for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);
    W16(gear+14,0x8088);W16(gear+16,0x8089);source[0x5BC]=76;command[0x2D]=1;command[0x28]=6;
    Check(percent(0,1,command.data(),1000)==1650,"equipped Boost and Burst synergy");
    source[0x5BC]=50;Check(percent(0,1,command.data(),1000)==1000,"strict OD threshold");
    source[0x5BC]=76;source[0x592]=255;Check(percent(0,1,command.data(),1000)==1000,"unequipped rows grant nothing");
    source[0x592]=0;W16(gear+14,0x8087);W16(gear+16,255);
    unsigned char before[5]{},jump[5]={0xE9};std::memcpy(before,reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0x398900>()),5);
    const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&FixedRandom)-(base+::FfxHooks::ExecutableProfile::Rva<0x398900>()+5));std::memcpy(jump+1,&delta,4);
    Check(Patch(base+::FfxHooks::ExecutableProfile::Rva<0x398900>(),jump,5),"private RNG boundary control");command[0x20]=4;command[0x27]=75;unsigned flags=0;rngCalls=0;
    Check(critical(source,target,command.data(),&flags,1000)==2000&&(flags&0x100)&&rngCalls==1,"Bravery uses one RNG sample");
    VanguardAccuracyCases(source,target,gear);
    Check(Patch(base+::FfxHooks::ExecutableProfile::Rva<0x398900>(),before,5),"private RNG restored");
    kernel[20+135*108+0x20]=1;Check(V::ReadMapping(mapping)&&mapping.codes[0]==V::MappingCode::NativePayload&&mapping.codes[1]==V::MappingCode::Valid,"kernel mutation invalidates stale mapping");
    Check(!V::SaveMapping(0,134),"reserved ID is rejected");Check(!V::SaveMapping(0,136),"battle mapping edits are rejected");
    kernel[20+135*108+0x20]=0;VanguardCostStatusCases(source,actors.data()+18*0xF90,gear);
    VanguardTurnCases(actors.data(),gear);
    source[0x592]=source[0x593]=target[0x592]=target[0x593]=255;std::memset(pct,0,31*4);
    command.fill(0);command[0x20]=1;command[0x23]=1;command[0x28]=1;command[0x2B]=1;
    Check(percent(0,1,command.data(),1000)==1000,"unspecified hit-balance rates remain neutral instead of inventing an original rule");
    const auto hitSettings=settings+"[vanguard_balance]\nsingle_hit_percent=110\nmulti_hit_percent=75\n";
    Check(C::LoadTextForTests(hitSettings.c_str(),"C:\\private-vanguard-native.ini"),"explicit hit-balance fixture configures independent rates");
    Check(percent(0,1,command.data(),1000)==1100,"explicit single-hit percentage is applied exactly once");
    command[0x2B]=3;Check(percent(0,1,command.data(),1000)==750,"multi-hit scaling uses the native declared hit-count category");
    Check(percent(0,1,command.data(),-1000)==-1000,"offensive hit normalization does not silently change restoration");
    command[0x28]=5;Check(percent(0,1,command.data(),1000)==1000,"fixed fractional damage is excluded from optional hit scaling");
    const auto invalidBalance=settings+"[vanguard_balance]\nsingle_hit_percent=999\nmulti_hit_percent=75\n";
    C::LoadTextForTests(invalidBalance.c_str(),"C:\\private-vanguard-native.ini");command[0x28]=1;
    Check(percent(0,1,command.data(),1000)==1000,"invalid balance pair fails neutral rather than partly applying it");
    const std::size_t oldPool=20+148*108,newPool=20+201*108;
    std::vector<unsigned char> extended(newPool+kernel.size()-oldPool);
    std::memcpy(extended.data(),kernel.data(),oldPool);
    std::memcpy(extended.data()+newPool,kernel.data()+oldPool,kernel.size()-oldPool);
    W16(extended.data()+10,200);W16(extended.data()+14,201*108);
    for(unsigned id:{148u,175u})std::memcpy(extended.data()+20+id*108,kernel.data()+20+135*108,108);
    const auto extendedAddress=reinterpret_cast<std::uintptr_t>(extended.data());
    const auto extendedSize=static_cast<unsigned short>(extended.size());
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A944>()),&extendedAddress,4);
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),&extendedSize,2);
    const auto reservedConfig=settings+"[vanguard_ids]\nhero_bravery=148\n";
    Check(C::LoadTextForTests(reservedConfig.c_str(),"C:\\private-vanguard-native.ini")&&V::ReadMapping(mapping)&&
          mapping.codes[0]==V::MappingCode::InvalidId&&mapping.codes[1]==V::MappingCode::Valid,
          "actual loaded-kernel mapping rejects an Aeon-reserved row even with the expected Vanguard name");
    const auto remappedConfig=settings+"[vanguard_ids]\nhero_bravery=175\n";
    Check(C::LoadTextForTests(remappedConfig.c_str(),"C:\\private-vanguard-native.ini")&&V::ReadMapping(mapping)&&
          mapping.codes[0]==V::MappingCode::Valid,"actual loaded-kernel mapping retains a verified unreserved extended row");
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A944>()),&kernelAddress,4);
    std::memcpy(reinterpret_cast<void*>(base+::FfxHooks::ExecutableProfile::Rva<0xD2A970>()),&size,2);
    V::RequestStop();Check(!V::Active(),"stop closes admission");pct[1]=20;command[0x20]=1;command[0x28]=4;
    Check(percent(0,1,command.data(),1000)==1000,"stopped detours preserve native behavior");
    std::printf("VANGUARD_RUNTIME_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
