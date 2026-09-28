// Extracted production transaction against private Win32 memory, not live hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/EquipmentWorkshopSettings.h"
#include "../hooks/EquipmentWorkshopCatalogBridge.h"
#include "../hooks/RonsoPoolSave.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
namespace FfxHooks::EquipmentWorkshop {
constexpr std::uintptr_t kSaveRam=0xD2CA90,kGearRam=0xD30F2C,kGilRam=0xD307D8;
std::uintptr_t module=0;
std::recursive_mutex mutex;
workshop::State state{};
AeonAscension::Ledger ascensionReceipts{};
AeonAscension::SaveId ascensionSave{};
bool ready=true,failGilReadback=false,gilWritten=false,failPartialGil=false,foreignPartialGil=false;
bool journalReady=true,journalThrows=false;
bool checkpointReady=true,checkpointThrows=false,serializerReady=true;
bool projectionReady=true;
unsigned checkpointMaximum=0,projectionCommits=0;
std::wstring loadedPath=L"C:\\private-transaction\\ffx_091";
Hash loadedDisk{};
Hash checkpointHead{};
// This harness isolates RAM ordering. StoreRt1/SaveFlowRt1 exercise real disk I/O.
struct IntentBoundary {
    bool StageTransaction(const std::wstring&,const Hash&,const SaveImage&,
        const workshop::State&,const workshop::Request&,const workshop::Economy&,const workshop::Plan&,
        const AeonAscension::Ledger*)const {
        if(journalThrows)throw std::bad_alloc();
        return journalReady;
    }
    bool PublishCheckpoint(const std::wstring&,const Hash&,const SaveImage& image,
        const NativeSaveEvents::CheckpointOwnership&,const workshop::State&,Hash&,const AeonAscension::Ledger*)const {
        std::memcpy(&checkpointMaximum,image.data()+0x5630,4);
        if(checkpointThrows)throw std::bad_alloc();
        return checkpointReady;
    }
} store;
bool FixtureSerializer(const unsigned char* input,unsigned char* output,std::size_t size,
                       NativeSaveEvents::CheckpointOwnership* ownership) noexcept {
    if(!serializerReady||size!=kSaveBytes)return false;
    std::memcpy(output,input,size);*ownership={};return true;
}
bool NativeImage(SaveImage& image){image.fill(0);std::memcpy(image.data()+64,reinterpret_cast<void*>(module+kSaveRam),0x68C0);return true;}
int failBeforeWrite=-1;
void Log(const char*){}
static unsigned refreshedOwner=255;
static bool failAeonRefresh=false;
void RefreshEquippedAeon(unsigned owner){refreshedOwner=owner;if(failAeonRefresh)ready=false;}
void Fault(RuntimeCode,const char*){ready=false;}
bool Copy(void* to,const void* from,std::size_t size){
    if(failGilReadback&&gilWritten&&reinterpret_cast<std::uintptr_t>(from)==module+kGilRam){failGilReadback=false;return false;}
    if(failPartialGil&&reinterpret_cast<std::uintptr_t>(to)==module+kGilRam&&size==4){
        failPartialGil=false;std::memcpy(to,from,2);gilWritten=true;
        if(foreignPartialGil)static_cast<unsigned char*>(to)[2]=0xAB;
        return false;
    }
    std::memcpy(to,from,size);
    if(reinterpret_cast<std::uintptr_t>(to)==module+kGilRam)gilWritten=true;
    return true;
}
bool Capture(workshop::State& out){
    if(!ready)return false;
    for(unsigned i=0;i<200;++i)if(std::memcmp(state.pieces[i].native,reinterpret_cast<void*>(module+kGearRam+22*i),22))return false;
    bool changed=false;
    for(unsigned i=0;i<112;++i){unsigned char qty=0;std::memcpy(&qty,reinterpret_cast<void*>(module+kSaveRam+0x40CC+i),1);
        if(state.items[i]!=qty){state.items[i]=qty;changed=true;}}
    if(changed)++state.revision;
    out=state;return true;
}
#include "WorkshopTransaction.inc"
static void SetGil(std::uint32_t value){std::memcpy(reinterpret_cast<void*>(module+kGilRam),&value,4);}
static std::uint32_t Gil(){std::uint32_t value=0;std::memcpy(&value,reinterpret_cast<void*>(module+kGilRam),4);return value;}
static void Reset(){
    ascensionReceipts={};ascensionSave={};
    std::memset(reinterpret_cast<void*>(module+kSaveRam),0,0x68C0);
    std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> items{};items.fill(255);
    for(unsigned slot=0;slot<2;++slot){auto* row=gear.data()+22*slot;row[2]=1;row[5]=1;row[6]=255;row[11]=4;
        const unsigned words[]={0x8008,0x806A,0x8055,0x8075};
        for(unsigned i=0;i<4;++i){row[14+2*i]=static_cast<unsigned char>(words[i]);row[15+2*i]=static_cast<unsigned char>(words[i]>>8);}}
    Check(workshop::Import(gear.data(),items.data(),12345,state)==workshop::Error::Ok,"private inventory imports");
    std::memcpy(reinterpret_cast<void*>(module+kGearRam),gear.data(),gear.size());
    for(unsigned i=0;i<112;++i){const std::uint16_t word=static_cast<std::uint16_t>(0x2000+i);
        std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0x3ECC+2*i),&word,2);
        *reinterpret_cast<unsigned char*>(module+kSaveRam+0x40CC+i)=255;}
    const std::uint16_t story=0x448;std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0xBEC),&story,2);
    SetGil(1000000);ready=true;failBeforeWrite=-1;failGilReadback=false;gilWritten=false;
    journalReady=true;journalThrows=false;failPartialGil=false;foreignPartialGil=false;
    checkpointReady=true;checkpointThrows=false;serializerReady=true;
    failAeonRefresh=false;refreshedOwner=255;
    Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=2\n","C:\\private-transaction.ini");
}
static workshop::Request Fusion(){
    workshop::Request r{};r.op=workshop::Op::Fuse;r.pieceId=state.pieces[0].id;
    r.other=1;r.otherId=state.pieces[1].id;r.revision=state.revision;r.count=1;r.from[0]=2;r.to[0]=2;return r;
}
static std::array<unsigned char,0x68C0> Bytes(){std::array<unsigned char,0x68C0> b{};std::memcpy(b.data(),reinterpret_cast<void*>(module+kSaveRam),b.size());return b;}
static void Run(){
    Reset();auto r=Fusion();workshop::Plan p{};
    SetGil(9999);auto before=Bytes();
    Check(Preview(r,p)==workshop::Error::Gil&&p.gilCost==10000&&Bytes()==before,"quote refuses insufficient native Gil without touching donor");
    SetGil(1000000);Check(Preview(r,p)==workshop::Error::Ok,"production quote captures native Gil and policy");
    auto forged=p;forged.gilCost=0;before=Bytes();
    Check(!Commit(r,forged)&&Bytes()==before,"forged free confirmation cannot bypass the native quote");
    SetGil(999999);before=Bytes();
    Check(!Commit(r,p)&&Bytes()==before,"native Gil drift invalidates the reviewed plan before writes");
    SetGil(1000000);Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=1\n","C:\\private-transaction.ini");before=Bytes();
    Check(!Commit(r,p)&&Bytes()==before,"F8 policy drift invalidates the reviewed plan before writes");
    Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=2\n","C:\\private-transaction.ini");
    Check(Commit(r,p)&&Gil()==990000&&state.items[57]==231&&!state.pieces[1].id&&!*reinterpret_cast<unsigned char*>(module+kGearRam+24),"transaction debits currency/material and consumes donor together");
    before=Bytes();Check(!Commit(r,p)&&Bytes()==before,"same confirmation never charges native Gil twice");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"rollback fixture obtains a valid quote");
    unsigned beforeGil=0;
    for(unsigned i=0;i<200;++i)if(std::memcmp(state.pieces[i].native,p.after.pieces[i].native,22))++beforeGil;
    for(unsigned i=0;i<112;++i)if(state.items[i]!=p.after.items[i])++beforeGil;
    failBeforeWrite=static_cast<int>(beforeGil);before=Bytes();const auto oldState=state;
    Check(!Commit(r,p)&&Bytes()==before&&Gil()==1000000&&std::memcmp(&oldState,&state,sizeof(state))==0&&!ready,"failure before Gil restores donor/material writes and closes admission");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"post-Gil readback fixture obtains a valid quote");
    before=Bytes();failGilReadback=true;gilWritten=false;
    Check(!Commit(r,p)&&gilWritten&&Bytes()==before&&Gil()==1000000&&!ready,"failed readback after actual Gil write restores the complete transaction");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"partial-write fixture obtains a valid quote");
    before=Bytes();failPartialGil=true;
    Check(!Commit(r,p)&&gilWritten&&Bytes()==before&&!ready,"a partial Gil write restores its owned prefix and all prior writes");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"foreign-byte fixture obtains a valid quote");
    before=Bytes();failPartialGil=true;foreignPartialGil=true;
    auto foreignExpected=before;foreignExpected[kGilRam-kSaveRam+2]=0xAB;
    Check(!Commit(r,p)&&Bytes()==foreignExpected&&!ready,"partial-write rollback preserves a foreign byte while restoring owned bytes");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"journal-order fixture obtains a valid quote");
    before=Bytes();journalReady=false;const auto beforeJournal=state;
    Check(!Commit(r,p)&&Bytes()==before&&std::memcmp(&state,&beforeJournal,sizeof(state))==0&&!ready,"failed intent persistence precedes every RAM mutation");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"journal-exception fixture obtains a valid quote");
    before=Bytes();journalThrows=true;bool escaped=false,accepted=false;
    try{accepted=Commit(r,p);}catch(...){escaped=true;}
    Check(!escaped&&!accepted&&Bytes()==before&&!ready,"a journal allocation exception cannot escape or charge the player");
    for(unsigned failure=0;failure<3;++failure){
        Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"checkpoint-failure fixture obtains a reviewed quote");
        before=Bytes();const auto old=state;
        if(failure==0)checkpointReady=false;
        if(failure==1)checkpointThrows=true;
        if(failure==2)serializerReady=false;
        escaped=false;accepted=false;
        try{accepted=Commit(r,p);}catch(...){escaped=true;}
        Check(!escaped&&!accepted&&Bytes()==before&&std::memcmp(&state,&old,sizeof(old))==0&&!ready,
              "serializer/publish failure restores all paid RAM bytes and RNG before rejecting the transaction");
    }
    Reset();workshop::Request refine{};refine.op=workshop::Op::Refine;refine.pieceId=state.pieces[0].id;
    *reinterpret_cast<unsigned char*>(module+kSaveRam+0x40CC+57)=0;workshop::State captured{};Capture(captured);refine.revision=captured.revision;before=Bytes();
    Check(Preview(refine,p)==workshop::Error::Materials&&p.requirements[57]==7&&Bytes()==before,"native quote requires every potential ingredient before random selection");
    Reset();refine.pieceId=state.pieces[0].id;refine.revision=state.revision;
    Check(Preview(refine,p)==workshop::Error::Ok&&Commit(refine,p)&&Gil()==999000&&state.rolls==1,"default B debits the first progressive native Gil fee");
    static const NativeSaveEvents::Observer projection{
        [](const wchar_t*,const unsigned char*,const unsigned char*,std::size_t) noexcept {},
        [](const wchar_t*,const unsigned char*,std::size_t) noexcept {},nullptr,
        [](const wchar_t*,const unsigned char*,unsigned char* output,std::size_t size,void**) noexcept {
            if(size!=kSaveBytes)return false;const unsigned maximum=9999;std::memcpy(output+0x5630,&maximum,4);return true;},
        [](void*,const wchar_t*,const unsigned char*,std::size_t) noexcept {return projectionReady;},
        [](void*,const unsigned char*,std::size_t,bool success) noexcept {if(success)++projectionCommits;},
        []() noexcept {return true;}};
    Check(NativeSaveEvents::Subscribe(&projection),"checkpoint participates in the existing native save projection service");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"projected checkpoint obtains a reviewed quote");
    const unsigned temporaryMaximum=450000;std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0x55F0),&temporaryMaximum,4);
    Check(Commit(r,p)&&checkpointMaximum==9999&&projectionCommits==1&&
          *reinterpret_cast<unsigned*>(module+kSaveRam+0x55F0)==450000,
          "paid checkpoint strips temporary native stats from a copy and commits extension metadata once");
    Reset();r=Fusion();Check(Preview(r,p)==workshop::Error::Ok,"projection failure obtains a reviewed quote");
    before=Bytes();projectionReady=false;
    Check(!Commit(r,p)&&Bytes()==before&&!ready,"projection metadata failure cannot publish or charge a paid checkpoint");
    projectionReady=true;NativeSaveEvents::Unsubscribe(&projection);
}
static void CatalogAndPresentationCases(){
    namespace Bridge=FfxHooks::EquipmentWorkshop::CatalogBridge;
    static workshop::Catalog active{};
    static const Bridge::Provider provider{
        [](workshop::Catalog& out) noexcept {out=active;return true;},
        [](unsigned) noexcept -> const char* {return "Private test ability";}};
    Check(Bridge::Register(&provider),"transaction fixture owns one explicit catalogue provider");
    Reset();active={};active.proof=111;active.entries[6]={0x808D,2,73,0};
    state.pieces[0].fifthUnlocked=1;
    workshop::Request request{};request.op=workshop::Op::SetFifth;request.pieceId=state.pieces[0].id;
    request.revision=state.revision;request.value=0x808D;workshop::Plan plan{};
    Check(Preview(request,plan)==workshop::Error::Ok&&plan.catalog.proof==111,
          "native transaction quotes a currently admitted extension recipe");
    auto before=Bytes();const auto beforeState=state;active.proof=222;
    Check(!Commit(request,plan)&&Bytes()==before&&std::memcmp(&state,&beforeState,sizeof(state))==0,
          "changed kernel/mapping proof rejects confirmation before resources or RNG change");
    active.proof=111;active.entries[6].item=77;
    Check(!Commit(request,plan)&&Bytes()==before,
          "changed recipe data rejects an old plan even when its proof number is reused");
    active.entries[6].item=73;
    Check(Commit(request,plan)&&state.pieces[0].fifth==0x808D&&Gil()==800000&&state.items[73]==210,
          "native extension transaction charges the declared mod recipe and fifth fee once");
    before=Bytes();Bridge::Unregister(&provider);
    Check(!Commit(request,plan)&&Bytes()==before&&state.pieces[0].fifth==0x808D,
          "losing catalogue admission cannot replay a quote or erase an accepted extension");
    Reset();auto& piece=state.pieces[0];piece.native[4]=8;piece.native[6]=8;
    std::memcpy(reinterpret_cast<void*>(module+kGearRam),piece.native,22);
    auto* payload=reinterpret_cast<unsigned char*>(module+kSaveRam);
    payload[0xC6C]=2;payload[0x55CC+8*0x94+0x2C]=0x10;
    payload[0x55CC+8*0x94+0x2D]=255;payload[0x55CC+8*0x94+0x2E]=0;
    request={};request.op=workshop::Op::Refine;request.pieceId=piece.id;request.revision=state.revision;
    Check(Preview(request,plan)==workshop::Error::Ok&&plan.gilDebit==2000,
          "equipped acquired Valefor armour gets one reviewed double-Gil transaction");
    failAeonRefresh=true;
    Check(Commit(request,plan)&&!ready&&refreshedOwner==8&&Gil()==998000&&state.rolls==1,
          "post-acceptance Aeon refresh failure keeps the paid outcome and closes further admission");
    before=Bytes();const auto paid=state;
    Check(!Commit(request,plan)&&Bytes()==before&&std::memcmp(&paid,&state,sizeof(paid))==0,
          "presentation failure never refunds or offers a second free refinement");
}
static void ProgressionCases(){
    Reset();workshop::Request request{};request.op=workshop::Op::Refine;request.pieceId=state.pieces[0].id;request.revision=state.revision;workshop::Plan plan{};
    std::uint16_t story=0x447;std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0xBEC),&story,2);auto before=Bytes();
    Check(Preview(request,plan)==workshop::Error::Locked&&Bytes()==before,"native story below Customize prevents a quote");
    story=0x448;std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0xBEC),&story,2);
    Check(Preview(request,plan)==workshop::Error::Ok,"native Customize boundary admits the same piece");
    story=0x447;std::memcpy(reinterpret_cast<void*>(module+kSaveRam+0xBEC),&story,2);before=Bytes();
    Check(!Commit(request,plan)&&Bytes()==before,"progression changes invalidate confirmation before writes");
    Config::LoadTextForTests("[equipment_workshop]\ndev_ignore_progression=1\ndev_free_materials=1\ndev_free_gil=1\n","C:\\private-transaction.ini");
    SetGil(0);for(unsigned i=0;i<112;++i)*reinterpret_cast<unsigned char*>(module+kSaveRam+0x40CC+i)=1;
    workshop::State captured{};Capture(captured);request=Fusion();
    Check(Preview(request,plan)==workshop::Error::Ok&&plan.gilCost==10000&&plan.gilDebit==0,"Dev quote preserves full nominal prices");
    Check(Commit(request,plan)&&Gil()==0&&state.items[57]==1&&!state.pieces[1].id,"native Dev fusion consumes donor but not resources");
    Reset();request={};request.op=workshop::Op::Refine;request.pieceId=state.pieces[0].id;request.revision=state.revision;
    Config::LoadTextForTests("[equipment_workshop]\ndev_free_gil=1\n","C:\\private-transaction.ini");
    Check(Preview(request,plan)==workshop::Error::Ok&&plan.gilCost==1000&&!plan.gilDebit,"native free-Gil preview records its policy");
    Config::LoadTextForTests("[equipment_workshop]\ndev_free_gil=0\n","C:\\private-transaction.ini");before=Bytes();
    Check(!Commit(request,plan)&&Bytes()==before,"turning Dev off invalidates a previously free quote");
}
}
int main(){
    using namespace FfxHooks::EquipmentWorkshop;
    void* memory=VirtualAlloc(nullptr,0xE00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!memory)return 2;module=reinterpret_cast<std::uintptr_t>(memory);
    Check(FfxHooks::NativeSaveEvents::RegisterCheckpointSerializer(&FixtureSerializer),"transaction fixture registers a controlled serializer boundary");
    Run();ProgressionCases();CatalogAndPresentationCases();
    Reset();auto& piece=state.pieces[0];piece.native[11]=2;
    for(unsigned i=2;i<4;++i){piece.native[14+2*i]=255;piece.native[15+2*i]=0;piece.abilities[i]=0;piece.ranks[i]=0;}
    std::memcpy(reinterpret_cast<void*>(module+kGearRam),piece.native,22);
    workshop::Request expand{};expand.op=workshop::Op::Expand;expand.pieceId=piece.id;expand.revision=state.revision;expand.value=4;
    workshop::Plan plan{};
    Check(Preview(expand,plan)==workshop::Error::Ok&&plan.costs[83]==1&&plan.costs[84]==1,"default expansion A charges one matching sphere for each added slot");
    auto before=Bytes();FfxHooks::Config::LoadTextForTests("[equipment_workshop]\nexpansion_recipe=2\n","C:\\private-transaction.ini");
    Check(!Commit(expand,plan)&&Bytes()==before,"changing expansion recipe invalidates the cheaper reviewed confirmation");
    Check(Preview(expand,plan)==workshop::Error::InvalidPolicy&&Bytes()==before,"native caller cannot select A while configured for B");
    expand.policy=1;
    Check(Preview(expand,plan)==workshop::Error::Ok&&plan.costs[83]==3&&plan.costs[84]==4,"expansion B uses third and fourth slot quantities");
    Check(Commit(expand,plan)&&state.pieces[0].native[11]==4&&state.items[83]==252&&state.items[84]==251,"native expansion commits configured capacity and exact resource debit once");
    before=Bytes();Check(!Commit(expand,plan)&&Bytes()==before,"expansion confirmation cannot debit a second time");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("WORKSHOP_TRANSACTION %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
