#include "../hooks/AeroEquipmentBridge.h"
static unsigned aeroRoll=0,aeroSamples=0;
static unsigned __cdecl AeroSample(unsigned){++aeroSamples;return aeroRoll;}
static LONG aeroForeignResult=0;
static DWORD WINAPI AeroForeignRead(void* actor){
    const bool rejected=!FfxHooks::AeroEquipmentBridge::Valefor()&&!FfxHooks::AeroEquipmentBridge::Sos(0,actor);
    InterlockedExchange(&aeroForeignResult,rejected?1:2);return 0;
}
static void SpiraAeroCases(std::vector<unsigned char>& kernel){
    namespace A=FfxHooks::AeroEquipmentBridge;
    Check(A::Valefor(),"admitted Spira Aero bank enables existing-save Valefor compatibility");
    const auto reader=CreateThread(nullptr,0,AeroForeignRead,Actor(0),0,nullptr);
    Check(reader&&WaitForSingleObject(reader,5000)==WAIT_OBJECT_0&&InterlockedCompareExchange(&aeroForeignResult,0,0)==1,
        "a foreign thread cannot borrow the main-thread Aero admission");if(reader)CloseHandle(reader);
    Equip(0,1,{0x80B4});
    for(unsigned condition=0;condition<4;++condition){Actor(0)[0x63E]=static_cast<unsigned char>(condition);
        Check(A::Sos(0,Actor(0))==((condition==1||condition==2)?64u:0u),"SOS follows native critical conditions1/2 only");}
    Actor(0)[0x63E]=1;W32(Actor(0)+0x5D0,0);
    Check(!A::Sos(0,Actor(0)),"dead actor cannot activate SOS");W32(Actor(0)+0x5D0,500);
    Equip(0,1,{});Equip(0,0,{0x80B4});Check(!A::Sos(0,Actor(0)),"SOS word on a weapon is not an armor grant");
    Equip(0,1,{0x80B4});Check(!A::Sos(1,Actor(0))&&!A::Sos(18,Actor(0)),"SOS validates canonical allied identity");
    const auto flags=Actor(0)[0x60D];(void)A::Sos(0,Actor(0));
    Check(Actor(0)[0x60D]==flags,"SOS never occupies a native elemental status timer");
    const unsigned offset=20+180*108+0x10;kernel[offset]^=2;
    Check(!A::Valefor()&&!A::Sos(0,Actor(0)),"changed loaded Aero payload revokes compatibility before next tick");kernel[offset]^=2;
    FfxHooks::Config::LoadTextForTests("[spira_ids]\nspira_devils_bargain=175\n","C:\\private-spira.ini");
    S::TickMainThread();Check(!A::Valefor(),"a foreign configured meaning cannot borrow an Aero ID");
    FfxHooks::Config::LoadTextForTests("[spira]\nenabled=1\n[aeon_ascension]\nenabled=1\n","C:\\private-spira.ini");S::TickMainThread();
    std::array<unsigned char,20+2*96> commands{};W32(commands.data(),1);W16(commands.data()+10,1);
    W16(commands.data()+12,96);W16(commands.data()+14,192);W32(commands.data()+16,20);
    const auto cp=reinterpret_cast<std::uintptr_t>(commands.data());
    std::uint32_t oldCommands=0;std::memcpy(&oldCommands,reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD2A92C>()),4);
    std::memcpy(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD2A92C>()),&cp,4);
    auto* row=commands.data()+20;row[0x1E]=4;row[0x20]=1;row[0x23]=1;row[0x2B]=1;
    Equip(0,0,{0x80B0});Equip(0,1,{});Equip(8,0,{});Equip(8,1,{});
    std::array<unsigned char,5> sampleBytes{};const auto sampleAt=base+FfxHooks::ExecutableProfile::Rva<0x398900>();
    std::memcpy(sampleBytes.data(),reinterpret_cast<void*>(sampleAt),5);
    unsigned char jump[5]={0xE9};W32(jump+1,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&AeroSample)-sampleAt-5));
    Check(WorkshopFieldFixture::Write(sampleAt,jump,5),"only private entropy endpoint is deterministic");
    const auto affinity=reinterpret_cast<int(__cdecl*)(const unsigned char*,const unsigned char*,unsigned,int)>(base+FfxHooks::ExecutableProfile::Rva<0x38A420>());
    Actor(1)[0x5DD]=64;
    std::array<unsigned char,0x94> saveBefore{};std::memcpy(saveBefore.data(),Ply(8),saveBefore.size());
    std::array<unsigned char,44> gearBefore{};std::memcpy(gearBefore.data(),Gear(8,0),gearBefore.size());
    for(unsigned roll:{0u,49u,50u,99u}){
        aeroRoll=roll;const auto samples=aeroSamples;Actor(0)[0x5D9]=0;
        B::DamageScope scope;B::EnterDamage(scope,{0,Actor(0),1,Actor(1),row,0x3000});
        Check(Actor(0)[0x5D9]==(roll<50?64:0)&&aeroSamples==samples+1,"Touch rolls once per eligible physical hit at50percent boundary");
        Check(affinity(Actor(1),row,Actor(0)[0x5D9],1000)==(roll<50?1500:1000),"actual native affinity consumes the current hit Aero mask");
        Check(scope.forwardCommand==row,"Touch preserves the original command and hit count");
        B::LeaveDamage(scope,0,true);Check(!Actor(0)[0x5D9],"Touch lease restores the native strike byte after the hit");
    }
    for(unsigned mode=0;mode<4;++mode){
        row[0x20]=static_cast<unsigned char>(mode==0?2:mode==1?0x11:1);row[0x23]=static_cast<unsigned char>(mode==2?2:1);
        row[0x1E]=static_cast<unsigned char>(mode==3?0:4);const auto samples=aeroSamples;
        B::DamageScope scope;B::EnterDamage(scope,{0,Actor(0),1,Actor(1),row,0x3000});
        Check(!Actor(0)[0x5D9]&&aeroSamples==samples,"magic healing MP-only and nonweapon actions cannot roll Touch");B::LeaveDamage(scope,0,true);
    }
    row[0x20]=row[0x23]=1;row[0x1E]=4;Actor(0)[0x5D9]=64;const auto samples=aeroSamples;
    {B::DamageScope scope;B::EnterDamage(scope,{0,Actor(0),1,Actor(1),row,0x3000});B::LeaveDamage(scope,0,true);}
    Check(Actor(0)[0x5D9]==64&&aeroSamples==samples,"native Aero Strike is retained without an unnecessary Touch draw");Actor(0)[0x5D9]=0;
    aeroRoll=0;
    {B::DamageScope scope;B::EnterDamage(scope,{0,Actor(0),1,Actor(1),row+96,0x3000});
        Check(!Actor(0)[0x5D9]&&aeroSamples==samples,"an aliased command pointer cannot borrow the weapon roll");B::LeaveDamage(scope,0,true);}
    {B::DamageScope scope;B::EnterDamage(scope,{0,Actor(0),1,Actor(1),row,0x3000});
        Check(Actor(0)[0x5D9]==64,"eligible lease enters before ownership-change test");Actor(0)[0x5D9]=2;
        B::LeaveDamage(scope,0,true);Check(Actor(0)[0x5D9]==2,"lease cleanup preserves a subsequent independent native write");Actor(0)[0x5D9]=0;}
    const auto postTouchSamples=aeroSamples;
    {B::DamageScope scope;B::EnterDamage(scope,{8,Actor(8),1,Actor(1),row,0x3000});
        Check(Actor(8)[0x5D9]==64&&aeroSamples==postTouchSamples,"old Valefor weapon gains Aero for its physical hit without a roll");
        B::LeaveDamage(scope,0,false);Check(!Actor(8)[0x5D9],"aborted Valefor action restores the owned lease");}
    row[0x20]=2;row[0x2D]=64;
    {B::DamageScope scope;B::EnterDamage(scope,{1,Actor(1),8,Actor(8),row,0x3000});
        Check((Actor(8)[0x5DA]&64)&&affinity(Actor(8),row,64,1000)==-1000,"existing-save Valefor absorbs Aero through actual native affinity");
        B::DamageScope nested;B::EnterDamage(nested,{1,Actor(1),8,Actor(8),row,0x3000});B::LeaveDamage(nested,0,true);
        Check(Actor(8)[0x5DA]==64,"nested hit cannot release its parent's absorption lease");
        S::RequestStop();B::LeaveDamage(scope,0,false);Check(!Actor(8)[0x5DA],"stop still unwinds the retained observer token");}
    Check(!A::Valefor()&&!A::Sos(0,Actor(0)),"stop closes every compatibility grant");
    Check(!std::memcmp(saveBefore.data(),Ply(8),saveBefore.size())&&!std::memcmp(gearBefore.data(),Gear(8,0),gearBefore.size()),"Aero compatibility does not overwrite existing save data or Valefor equipment");
    WorkshopFieldFixture::Write(sampleAt,sampleBytes.data(),5);
    std::memcpy(reinterpret_cast<void*>(base+FfxHooks::ExecutableProfile::Rva<0xD2A92C>()),&oldCommands,4);
}
