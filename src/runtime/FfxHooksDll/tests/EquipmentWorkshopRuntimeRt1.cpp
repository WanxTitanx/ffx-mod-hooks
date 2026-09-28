#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/RonsoPoolSave.h"
#include "PrivatePeFixture.h"
#include "WorkshopFieldFixture.h"
#include "WorkshopEconomyFixture.h"
#include "../hooks/EquipmentWorkshopSettings.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace FfxHooks::EquipmentWorkshop;
static unsigned checks,failures;
static std::uintptr_t nativeFixtureBase=0;
static LONG WINAPI AeonDiagnostic(EXCEPTION_POINTERS* p){
    const auto* frame=reinterpret_cast<const unsigned*>(p->ContextRecord->Ebp);
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&frame)
        std::printf("AEON_FRAME caller=%08lX args=%08X,%08X\n",static_cast<unsigned long>(frame[1]-nativeFixtureBase),frame[2],frame[3]);
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&frame&&frame[0]){
        const auto* parent=reinterpret_cast<const unsigned*>(frame[0]);
        std::printf("AEON_PARENT caller=%08lX args=%08X,%08X\n",static_cast<unsigned long>(parent[1]-nativeFixtureBase),parent[2],parent[3]);
    }
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
        std::printf("AEON_PRIVATE_EXCEPTION rva=%08lX address=%08lX\n",static_cast<unsigned long>(p->ContextRecord->Eip-nativeFixtureBase),static_cast<unsigned long>(p->ExceptionRecord->ExceptionInformation[1]));
    return EXCEPTION_CONTINUE_SEARCH;
}
static void Check(bool b,const char* m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}
// SaveFlowRt1 exercises real Ronso serialization. This isolated suite explicitly
// supplies vanilla serialization rather than assuming an absent owner is safe.
static bool FixtureSerializer(const unsigned char* input,unsigned char* output,std::size_t size,
                              FfxHooks::NativeSaveEvents::CheckpointOwnership* ownership) noexcept {
    if(size!=kSaveBytes)return false;
    std::memcpy(output,input,size);*ownership={};return true;
}
#include "AeonWorkshopRuntimeCases.inl"
#include "CombatProducerCases.inl"
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);AddVectoredExceptionHandler(1,AeonDiagnostic);
    if(argc!=5&&argc!=6)return 2;
    HMODULE module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module)return 2;const auto base=reinterpret_cast<std::uintptr_t>(module);
    nativeFixtureBase=base;
    Check(PrivatePeFixture::NormalizeRelocations(module),"private image uses runtime HIGHLOW relocation semantics on Windows and Wine");
    if(failures)return 2;
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));
    SaveImage image{};std::ifstream f(argv[2],std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(image.data()),image.size()))return 2;
    WorkshopEconomyFixture::Seed(image);WorkshopEconomyFixture::Mode(1);
    Check(!StartForTests(base,false,root.c_str(),nullptr),"default-OFF runtime installs nothing");
    Check(!FfxHooks::NativeSaveEvents::Requested(),"OFF does not request an I/O producer");
    if(argc==6&&std::strcmp(argv[5],"combat-only")==0){
        Check(CombatProducerFixture::B::Subscribe(CombatProducerFixture::B::Slot::Elemental,&CombatProducerFixture::observer),"standalone combat requests the shared producer");
        Check(StartForTests(base,false,root.c_str(),nullptr)&&CombatProducerReady(),"standalone combat installs without enabling Workshop");
        Check(!Requested()&&!Status().enabled&&Status().code==RuntimeCode::Disabled,"shared infrastructure does not advertise Workshop as enabled");
        Check(!FfxHooks::NativeSaveEvents::Requested()&&GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES,"standalone combat creates no inventory store or save I/O");
        Check(*reinterpret_cast<unsigned char*>(base+0x386787)==4&&*reinterpret_cast<unsigned char*>(base+0x39C8A4)==4,"standalone combat leaves equipment loop widths untouched");
        CombatProducerFixture::Run(base);
        std::printf("STANDALONE_COMBAT_PRODUCER_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
    }
    Check(FfxHooks::NativeSaveEvents::RegisterCheckpointSerializer(&FixtureSerializer),"isolated fixture registers its explicit native save serializer");
    Check(StartForTests(base,true,root.c_str(),nullptr),"supported image installs the production hooks and safe views");
    Check(Status().code==RuntimeCode::WaitingForSave,"enabled runtime waits for an observed save load");
    if(argc==6&&std::strcmp(argv[5],"combat")==0){CombatProducerFixture::Run(base);
        std::printf("COMBAT_PRODUCER_RUNTIME_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    const auto savePath=root+L"\\ffx_094";
    Check(LoadForTests(savePath.c_str(),image,image),"actual save event prepares metadata association");
    Check(CommitLoadForTests(image),"native load boundary admits the matching inventory on its owner thread");
    workshop::State state{};Check(Capture(state),"in-game snapshot is available after load");
#ifdef FFXHOOKS_ASCENSION_RUNTIME_V1
    {
        FfxHooks::AeonAscension::Request request{};
        FfxHooks::AeonAscension::Plan plan{};
        const auto before=state;
        Check(PreviewAscension(request,plan)==workshop::Error::UnsupportedAbility,
              "paid upgrade requests remain unavailable without an admitted mapping provider");
        Check(!AscensionEffect(8,0)&&!AscensionEffect(8,1)&&!AscensionEffect(7,1),
              "an ordinary loaded save grants no paid Ascension permission");
        Check(Capture(state)&&std::memcmp(&before,&state,sizeof(state))==0,
              "unavailable paid requests preserve the existing inventory and progression");
    }
#else
    Check(false,"paid Workshop admission interface is not connected");
#endif
    const auto importedRng=state.rng;
    Check(LoadForTests(savePath.c_str(),image,image)&&CommitLoadForTests(image)&&Capture(state)&&state.rng==importedRng,
          "reloading an initially unowned save cannot reroll the Workshop seed");
    if(argc==6){AeonRuntimeCases(base,root,image,argv[4]);RequestStop();
        std::printf("AEON_WORKSHOP_RUNTIME_RT1 %u/%u passed\n",checks-failures,checks);return failures?1:0;}
    // Battle phase is a BYTE; adjacent native state survives battle teardown.
    // A DWORD read falsely treats that adjacent state as an ongoing battle.
    auto* phase=reinterpret_cast<unsigned char*>(base+0xD2A8E0);
    unsigned char phaseBefore[4]{};std::memcpy(phaseBefore,phase,4);
    const auto beforeBattle=state;
    phase[0]=1;phase[1]=1;phase[2]=0xA5;phase[3]=0x5A;
    Check(!Capture(state),"active battle still denies Workshop mutation snapshots");
    phase[0]=0;
    Check(Capture(state),"battle exit restores Workshop even when adjacent state bytes remain nonzero");
    Check(std::memcmp(&state,&beforeBattle,sizeof(state))==0,
          "battle admission changes preserve all inventory identities and extension metadata");
    std::memcpy(phase,phaseBefore,4);
    std::uint16_t story=0x447;std::memcpy(reinterpret_cast<void*>(base+0xD2D67C),&story,2);
    Check(Access()==workshop::Error::Locked,"runtime reads the native Customize admission boundary");
    story=0x448;std::memcpy(reinterpret_cast<void*>(base+0xD2D67C),&story,2);
    Check(Access()==workshop::Error::Ok,"native Customize admission opens Workshop without runtime story writes");
    unsigned slot=200;for(unsigned i=0;i<200;++i)if(state.pieces[i].id && state.pieces[i].native[6]==255 && !(state.pieces[i].native[3]&12) && state.pieces[i].native[4]<7 && state.pieces[i].native[5]<2){slot=i;break;}
    if(slot==200)return 2;
    workshop::Request r{};r.op=workshop::Op::Mode;r.slot=static_cast<std::uint16_t>(slot);r.pieceId=state.pieces[slot].id;r.revision=state.revision;r.value=1;
    workshop::Plan plan{};Check(Preview(r,plan)==workshop::Error::Ok,"live preview uses the shared tested rules");
    const auto parked=root+L"-blocked";
    Check(MoveFileExW(root.c_str(),parked.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"private storage failure fixture parks its own directory");
    HANDLE blocker=CreateFileW(root.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(blocker!=INVALID_HANDLE_VALUE,"private storage path is temporarily unavailable");
    if(blocker!=INVALID_HANDLE_VALUE)CloseHandle(blocker);
    const auto beforeStorageFailure=state;
    const bool storageAccepted=Commit(r,plan);
    DeleteFileW(root.c_str());
    Check(MoveFileExW(parked.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE,"private storage directory is restored");
    Check(!storageAccepted,"failed durable intent rejects confirmation before changing inventory");
    Check(LoadForTests(savePath.c_str(),image,image)&&CommitLoadForTests(image)&&Capture(state),"verified reload recovers storage-failure fixture");
    Check(state.rng==beforeStorageFailure.rng&&state.pieces[slot].mode==beforeStorageFailure.pieces[slot].mode,"storage failure cannot advance RNG or retain an unpaid extension");
    r.revision=state.revision;Check(Preview(r,plan)==workshop::Error::Ok,"restored storage permits a new reviewed request");
    Check(Commit(r,plan),"live transaction commits the exact reviewed revision");
    Check(!Commit(r,plan),"repeated confirmation cannot commit twice");
    Check(Capture(state)&&state.pieces[slot].mode==1,"runtime retains the committed piece extension");
    Check(WriteForTests(savePath.c_str(),image),"successful native save publishes its bound sidecar");
    auto create=reinterpret_cast<unsigned(__cdecl*)(const void*)>(base+0x3AB930);
    auto remove=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3ABCC0);
    unsigned char gear[22]{};gear[2]=1;gear[6]=255;gear[11]=4;
    const std::uint16_t weaponWords[]={0x8000,0x8062,0x8063,0x8064};std::memcpy(gear+14,weaponWords,8);
    unsigned char armorGear[22]{};std::memcpy(armorGear,gear,22);armorGear[5]=1;
    const std::uint16_t armorWords[]={0x8008,0x806A,0x806B,0x806C};std::memcpy(armorGear+14,armorWords,8);
    unsigned char emptyGear[22]{};std::memcpy(emptyGear,armorGear,22);
    for(unsigned i=0;i<4;++i){emptyGear[14+2*i]=255;emptyGear[15+2*i]=0;}
    const unsigned created=create(emptyGear);
    Check(created>=0x5000&&created<0x50C8&&Capture(state),"real native creation keeps the runtime inventory coherent");
    const unsigned index=created&0xFFF;const auto id=state.pieces[index].id;remove(created);
    Check(Capture(state)&&state.pieces[index].id==0,"real native free retires the actual piece identity");
    create(emptyGear);Check(Capture(state)&&state.pieces[index].id!=id,"real slot reuse receives a new identity");
    auto step=[&](workshop::Op operation,unsigned value){
        if(!Capture(state))return false;
        workshop::Request request{};request.op=operation;request.slot=static_cast<std::uint16_t>(index);
        request.pieceId=state.pieces[index].id;request.revision=state.revision;request.value=static_cast<std::uint16_t>(value);
        workshop::Plan planned{};return Preview(request,planned)==workshop::Error::Ok&&Commit(request,planned);
    };
    const auto ownerSpheres=state.items[77];
    Check(step(workshop::Op::UnlockFifth,0)&&Capture(state)&&state.items[77]+10==ownerSpheres,"four-open-slot unlock debits ten actual owner spheres");
    const auto unopenedAbilities=state;
    Check(!step(workshop::Op::SetFifth,0x8055)&&Capture(state)&&std::memcmp(&state,&unopenedAbilities,sizeof(state))==0,"empty native abilities reject fifth customization without spending");
    // Fill the four native slots through paid fusion before fifth customization.
    for(unsigned pair=0;pair<2;++pair){
        const unsigned donor=create(armorGear);Check(donor>=0x5000&&donor<0x50C8&&Capture(state),"native producer supplies post-unlock donor");
        if(donor<0x5000||donor>=0x50C8)return 2;
        workshop::Request fill{};fill.op=workshop::Op::Fuse;fill.slot=static_cast<std::uint16_t>(index);fill.pieceId=state.pieces[index].id;fill.revision=state.revision;
        fill.other=static_cast<std::uint16_t>(donor&0xFFF);fill.otherId=state.pieces[fill.other].id;fill.count=2;
        fill.from[0]=fill.to[0]=static_cast<unsigned char>(pair*2);fill.from[1]=fill.to[1]=static_cast<unsigned char>(pair*2+1);
        workshop::Plan filled{};Check(Preview(fill,filled)==workshop::Error::Ok&&Commit(fill,filled),"paid native fusion fills ordinary slots without touching the fifth");
    }
    Check(step(workshop::Op::SetFifth,0x8055)&&Capture(state)&&state.pieces[index].fifth==0x8055,"selected fifth ability reaches the production runtime");
    workshop::Policy configured{};Check(FfxHooks::EquipmentWorkshop::Settings::Read(configured)&&configured.mode==1,"global A selection does not refine the piece by itself");
    Check(Capture(state)&&state.pieces[index].rank==0,"mode confirmation is not a refinement success");
    const auto genericMaterials=state.items[64];
    Check(step(workshop::Op::Refine,0)&&Capture(state)&&workshop::AbilityRank(state.pieces[index],0)==1&&state.items[64]+1==genericMaterials,
          "mixed native piece refines and debits the generic recipe exactly once");
    unsigned consumedSlot=200;
    for(unsigned transfers:{1u,2u}){
        const unsigned donor=create(armorGear),twin=create(armorGear);
        Check(donor>=0x5000&&donor<0x50C8&&twin>=0x5000&&twin<0x50C8&&Capture(state),"native producer creates distinct identical donor and control equipment");
        if(donor<0x5000||donor>=0x50C8||twin<0x5000||twin>=0x50C8)return 2;
        consumedSlot=donor&0xFFF;const unsigned twinSlot=twin&0xFFF;
        workshop::Request choose{};choose.op=workshop::Op::Mode;choose.slot=static_cast<std::uint16_t>(consumedSlot);
        choose.pieceId=state.pieces[consumedSlot].id;choose.revision=state.revision;choose.value=1;
        workshop::Plan chosen{};Check(Preview(choose,chosen)==workshop::Error::Ok&&Commit(choose,chosen)&&Capture(state),"native donor selects the matching refinement mode");
        choose.op=workshop::Op::Refine;choose.value=0;choose.revision=state.revision;
        Check(Preview(choose,chosen)==workshop::Error::Ok&&Commit(choose,chosen)&&Capture(state),"private donor refinement preserves the receiving fixture's effect ranks");
        const auto before=state;workshop::Request fusion{};fusion.op=workshop::Op::Fuse;
        fusion.slot=static_cast<std::uint16_t>(index);fusion.pieceId=state.pieces[index].id;fusion.revision=state.revision;
        fusion.other=static_cast<std::uint16_t>(consumedSlot);fusion.otherId=state.pieces[consumedSlot].id;
        fusion.count=static_cast<unsigned char>(transfers);fusion.from[1]=fusion.to[1]=1;
        workshop::Plan fused{};
        Check(Preview(fusion,fused)==workshop::Error::Ok&&Capture(state)&&std::memcmp(&state,&before,sizeof(state))==0,"native fusion preview does not consume or modify inventory");
        Check(Commit(fusion,fused)&&Capture(state),"production commit accepts reviewed one/two-ability fusion");
        Check(!state.pieces[consumedSlot].id&&!state.pieces[consumedSlot].native[2]&&
              *reinterpret_cast<unsigned char*>(base+0xD30F2C+22*consumedSlot+2)==0,"fusion consumes the donor in actual native RAM, not just the Workshop list");
        Check(std::memcmp(&state.pieces[twinSlot],&before.pieces[twinSlot],sizeof(workshop::Piece))==0,"native fusion preserves an identical unselected control piece");
        Check(state.pieces[index].abilities[0]==before.pieces[consumedSlot].abilities[0]&&workshop::AbilityRank(state.pieces[index],0)==1&&state.pieces[index].fifth==0x8055,"fusion preserves target identity, working refinement and fifth ability");
        const auto committed=state;
        Check(!Commit(fusion,fused)&&Capture(state)&&std::memcmp(&state,&committed,sizeof(state))==0,"duplicate native confirmation cannot debit or consume again");
        fusion.revision=state.revision;
        Check(Preview(fusion,fused)==workshop::Error::Stale,"consumed native donor cannot be reused even with the current revision");
    }
    const auto hasAbility=reinterpret_cast<int(__cdecl*)(const void*,unsigned)>(base+0x3A0C40);
    const auto* nativePiece=reinterpret_cast<const unsigned char*>(base+0xD30F2C+22*index);
    Check(nativePiece[11]==4&&hasAbility(nativePiece,0x8055)==1,"real native direct-ID queries see the fifth while the record stays22 bytes");
    SaveImage saved=image;std::memcpy(saved.data()+64,reinterpret_cast<const void*>(base+0xD2CA90),0x68C0);
    FfxHooks::RonsoPool::SealSave(saved);
    Check(WriteForTests(savePath.c_str(),saved),"native save event persists the edited live inventory and extension together");
    const auto savedIdentity=state.pieces[index].id,oldRevision=state.revision;
    Check(LoadForTests(savePath.c_str(),saved,saved)&&CommitLoadForTests(saved)&&Capture(state)&&state.pieces[index].id==savedIdentity&&state.pieces[index].fifth==0x8055&&workshop::AbilityRank(state.pieces[index],0)==1,"real load boundary restores the same piece and fifth ability");
    Check(state.revision>oldRevision,"load invalidates confirmations from the previous session");
    Check(consumedSlot<200&&!state.pieces[consumedSlot].id&&!state.pieces[consumedSlot].native[2],"native save and reload cannot resurrect the fused donor");
    const unsigned replacement=create(gear);
    Check(replacement==0x5000+consumedSlot&&Capture(state)&&state.pieces[consumedSlot].id&&state.pieces[consumedSlot].mode==0,
          "native inventory reuses the vacant donor slot as a fresh unrefined piece");
    // Execute the complete native aggregator with production Gear/Row detours,
    // rather than replacing those consumers with a test-only adapter.
    std::ifstream kernelFile(argv[4],std::ios::binary);
    std::vector<unsigned char> kernel((std::istreambuf_iterator<char>(kernelFile)),{});
    if(kernel.size()<20+131*108)return 2;
    const auto originalKernel=kernel;
    const auto kernelAddress=reinterpret_cast<std::uintptr_t>(kernel.data());
    std::memcpy(reinterpret_cast<void*>(base+0xD2A944),&kernelAddress,4);
    unsigned char actor[0xF90]{};
    const auto actorAddress=reinterpret_cast<std::uintptr_t>(actor);
    std::memcpy(reinterpret_cast<void*>(base+0xD334CC),&actorAddress,4);
    auto* player=reinterpret_cast<unsigned char*>(base+0xD3205C);
    player[0x2D]=player[0x2E]=255;
    const auto equip=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(base+0x3AB990);
    equip(0,1,created);
    Check(Capture(state)&&state.pieces[index].native[6]==0,"real native equip preserves the modified piece identity");
    const auto aggregate=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x39C610);
    aggregate(0);
    Check((actor[0x632]&0x10)!=0,"production five-entry native aggregator applies fifth Auto-Protect");
    aggregate(0);
    Check((actor[0x632]&0x10)!=0&&kernel==originalKernel,"repeated aggregation preserves the global ability kernel");
    Check(WorkshopFieldFixture::Percent(base,0,10)==101,"production field detour adds generic STR alongside three numeric refinements");
    Check(WorkshopFieldFixture::Percent(base,0,11)==101,"production row detour delivers generic MAG to native arithmetic");
    Check(WorkshopFieldFixture::Percent(base,0,12)==121,"three distinct native armor tiers contribute their exact refined defense");
    Check(WorkshopFieldFixture::Percent(base,0,10)==101&&kernel==originalKernel,"production refresh does not compound or edit the global kernel");
    equip(0,0,replacement);
    Check(Capture(state)&&state.pieces[replacement&0xFFF].native[6]==0,"native weapon equips alongside the refined armor");
    Check(WorkshopFieldFixture::Percent(base,0,10)==119&&WorkshopFieldFixture::Percent(base,0,11)==101&&kernel==originalKernel,
          "native field loop includes the weapon and refined armor without changing global rows");
    equip(0,0,255);
    Check(Capture(state)&&WorkshopFieldFixture::Percent(base,0,10)==101,"unequipping the weapon restores the armor-only numeric view");
    RequestStop();Check(!FfxHooks::NativeSaveEvents::Requested()&&!Capture(state),"stop closes mutations and save subscriptions");
    aggregate(0);
    Check((actor[0x632]&0x10)==0,"retained hooks provide safe vanilla views after stop");
    Check(WorkshopFieldFixture::Percent(base,0,10)==100&&WorkshopFieldFixture::Percent(base,0,11)==100,
          "stopped production hooks restore vanilla numeric views");
    std::printf("EquipmentWorkshopRuntimeRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
