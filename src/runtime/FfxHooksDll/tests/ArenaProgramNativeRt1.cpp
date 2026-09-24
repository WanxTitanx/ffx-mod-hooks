// Reuse the exact PE mapper/accessor fixture; its original main is not executed.
#define main UnusedPositionHarnessMain
#include "ArenaPositionNativeRt1.cpp"
#undef main
#include "../hooks/ArenaBattleProgram.h"
#include <filesystem>
namespace Program = FfxHooks::ArenaBattleProgram;
using LookupWorker = unsigned char*(__cdecl*)(void*,int,int*);
struct NativeInit { int(__cdecl* initialize)(); int calls=0; std::uint16_t* foreignSize=nullptr; };
static bool InitHeader(void* raw,int* result){auto& c=*static_cast<NativeInit*>(raw);++c.calls;*result=c.initialize();if(c.foreignSize)*c.foreignSize=123u;return true;}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    std::string error;
    R::StartProduction(0x00400000u,true,false);
    Check(!R::ProductionOperational(),"missing verified normal profiles cannot expose an operational Mix producer");
    auto corrupt=Read(argv[3]);if(corrupt.empty())return 2;corrupt.back()^=1u;
    const auto badPath=std::filesystem::temp_directory_path()/("JarvisArenaBad-"+std::to_string(GetCurrentProcessId())+".bin");
    {std::ofstream f(badPath,std::ios::binary);f.write(reinterpret_cast<const char*>(corrupt.data()),corrupt.size());}
    Check(!Program::Load(badPath.string().c_str(),&error)&&!Program::Ready(),"modified profile bytes are rejected before publication");
    std::filesystem::remove(badPath);
    Check(Program::Load(argv[3],&error),"exact private normal-profile pack loads");
    if(!Program::Ready()){std::printf("PROFILE: %s\n",error.c_str());return 2;}
    const auto original=Read(argv[2]);auto* image=MapPe(Read(argv[1]));
    auto* carrier=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x5000u,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image||!carrier||original.size()!=0x4428u)return 2;
    std::memcpy(carrier,original.data(),original.size());const auto base=reinterpret_cast<uintptr_t>(image);
    auto* root=reinterpret_cast<uint32_t*>(image+0xD2A9A8u);auto* size=reinterpret_cast<uint16_t*>(image+0xD2A9A6u);
    *root=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(carrier));*size=0x4428u;
    std::memcpy(image+0xD2C25Au,"dome02_00",10u);*reinterpret_cast<uint32_t*>(image+0xD2C254u)=0x00470460u;image[0xD2C258u]=image[0xD2C259u]=0;
    // Exact relocated native pointer-initialization and cached-count instructions.
    // Actor/render calls are not executed by this fixture.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,256u,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!code)return 2;
    constexpr size_t first=0x383F1Au-0x383ED0u,second=0x383F9Eu-0x383F83u;
    std::memcpy(code,image+0x383ED0u,first);std::memcpy(code+first,image+0x383F83u,second);
    code[first+second]=0x5E;code[first+second+1]=0xC3;
    FlushInstructionCache(GetCurrentProcess(),code,256u);
    NativeInit init{reinterpret_cast<int(__cdecl*)()>(code)};
    init.initialize();
    CheckNativeHookActivation(base);
    const auto lookup=reinterpret_cast<LookupWorker>(image+0x397420u);
    int event=0;auto* baseline=lookup(carrier+0x3790u,41,&event);
    Check(baseline&&baseline[1]==2u,"baseline native lookup reproduces Zanarkand per-monster script override");
    std::vector<const FfxHooks::ArenaMonsters::Entry*> singles;
    for(const auto& entry:FfxHooks::ArenaMonsters::kEntries)if(entry.count==1)singles.push_back(&entry);
    unsigned catalogCursor=0;
    const unsigned firstScene=argc>4?static_cast<unsigned>(std::strtoul(argv[4],nullptr,10)):0u;
    const unsigned lastScene=argc>5?static_cast<unsigned>(std::strtoul(argv[5],nullptr,10)):FfxHooks::ArenaScenery::kChoiceCount;
    Check(firstScene<lastScene && lastScene<=FfxHooks::ArenaScenery::kChoiceCount && lastScene-firstScene<=24u,"native scene batches stay within the published-frame bound");
    if(firstScene>=lastScene || lastScene>FfxHooks::ArenaScenery::kChoiceCount || lastScene-firstScene>24u)return 2;
    for(unsigned roster=0;roster<3;++roster)for(unsigned scene=firstScene;scene<lastScene;++scene)for(unsigned camera=0;camera<2;++camera){
        std::memcpy(carrier,original.data(),original.size());*root=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(carrier));*size=0x4428u;
        *reinterpret_cast<uint32_t*>(image+0xD2C254u)=0x00470460u;
        R::StartProduction(base,true,false);
        SelectionInput selection{};selection.activationCount=8;selection.scenery=static_cast<FfxHooks::ArenaScenery::Choice>(scene);selection.camera=static_cast<FfxHooks::ArenaScenery::Camera>(camera);
        if(roster)for(unsigned slot=0;slot<8;++slot)selection.activations[slot]=singles[(catalogCursor++)%singles.size()]->choice;
        const auto expected=BuildSelection(selection).expanded;
        Check(R::ProductionArmSelection(selection,10),"normal arena selection arms");
        const int before=init.calls;const auto result=R::RunProductionBattle({&init,&InitHeader},11);
        Check(result.transaction.result==TransactionResult::Restored&&init.calls==before+1,"native init runs exactly once for the admitted Mix");
        auto* map=reinterpret_cast<unsigned char*>(*reinterpret_cast<uint32_t*>(image+0xD2A9B4u));
        auto* formation=reinterpret_cast<unsigned char*>(*reinterpret_cast<uint32_t*>(image+0xD2A9C0u));
        auto* script=reinterpret_cast<unsigned char*>(*reinterpret_cast<uint32_t*>(image+0xD2A9ACu));
        Check(map&&map[0]==2u&&image[0xD2A9A4u]==2u,"late native state contains only the normal main/camera workers");
        for(int slot=41;slot<=48;++slot){event=0;Check(lookup(map,slot,&event)==nullptr,"every monster slot is free of inherited boss/movement script overrides");}
        for(unsigned slot=0;slot<8;++slot)Check(*reinterpret_cast<uint16_t*>(formation+12u+slot*2u)==expected.monsterIds[slot],"late native formation keeps every catalog ID, not restored glyph IDs");
        Check(script!=carrier+0x30u,"native ATEL registration receives the normal arena program");
        Check(*root==reinterpret_cast<uintptr_t>(carrier)&&*size==0x4428u&&std::memcmp(carrier,original.data(),original.size())==0,"native file allocation ownership and every source byte are restored");
        const auto getter=reinterpret_cast<Getter>(image+0x3AC000u);
        for(int slot=0;slot<8;++slot){float point[4]={};Check(getter(0,0,0,5,slot,point)==0,"native position reader accepts every expanded target slot");}
        float scripted[4]={15,7,160,9},observed[4]={};
        Check(getter(1,0,0,5,0,scripted)==0&&getter(0,0,0,5,0,observed)==0&&
              std::memcmp(scripted,observed,sizeof(scripted))==0,
              "native scripted movement owns the normal frame without a late output overwrite");
        std::array<unsigned char,0x700> actor{};
        std::memcpy(actor.data()+0x3B0u,scripted,sizeof(scripted));actor[0x6D4u]=0;
        Check(getter(0,reinterpret_cast<uintptr_t>(actor.data()),0,5,0,observed)==0&&
              std::memcmp(scripted,observed,sizeof(scripted))==0,
              "cached actor/world coordinates retain the original native accessor path");
        R::ProductionRequestStop();R::ProductionResetAfterDrain();
        event=0;Check(lookup(map,41,&event)==nullptr,"borrowed worker mappings remain valid after Hooks stop");
        init.initialize();event=0;auto* nextMap=reinterpret_cast<unsigned char*>(*reinterpret_cast<uint32_t*>(image+0xD2A9B4u));
        Check(lookup(nextMap,41,&event)!=nullptr,"next unmodified native initialization restores its own scene program");
    }
    std::memcpy(carrier,original.data(),original.size());*root=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(carrier));*size=0x4428u;
    R::StartProduction(base,true,false);SelectionInput conflict{};conflict.activationCount=3;
    Check(R::ProductionArmSelection(conflict,40),"partial root-conflict fixture arms");init.foreignSize=size;
    const auto conflicted=R::RunProductionBattle({&init,&InitHeader},41);
    Check(conflicted.transaction.result==TransactionResult::OriginalRejected&&*root==reinterpret_cast<uintptr_t>(carrier)&&*size==123u,
          "a changed size cannot strand the private view as native allocation owner; foreign size survives");
    init.foreignSize=nullptr;*size=0x4428u;R::ProductionRequestStop();R::ProductionResetAfterDrain();
    VirtualFree(code,0,MEM_RELEASE);VirtualFree(carrier,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::printf("ArenaProgramNativeRt1: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
