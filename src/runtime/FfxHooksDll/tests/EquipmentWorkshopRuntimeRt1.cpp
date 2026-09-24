#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../hooks/EquipmentWorkshopRuntime.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/RonsoPoolSave.h"
#include "PrivatePeFixture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace FfxHooks::EquipmentWorkshop;
static unsigned checks,failures;
static void Check(bool b,const char* m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    HMODULE module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module)return 2;const auto base=reinterpret_cast<std::uintptr_t>(module);
    Check(PrivatePeFixture::NormalizeRelocations(module),"private image uses runtime HIGHLOW relocation semantics on Windows and Wine");
    if(failures)return 2;
    const std::wstring root(argv[3],argv[3]+std::strlen(argv[3]));
    SaveImage image{};std::ifstream f(argv[2],std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(image.data()),image.size()))return 2;
    Check(!StartForTests(base,false,root.c_str(),nullptr),"default-OFF runtime installs nothing");
    Check(!FfxHooks::NativeSaveEvents::Requested(),"OFF does not request an I/O producer");
    Check(StartForTests(base,true,root.c_str(),nullptr),"supported image installs the production hooks and safe views");
    Check(Status().code==RuntimeCode::WaitingForSave,"enabled runtime waits for an observed save load");
    const auto savePath=root+L"\\ffx_094";
    Check(LoadForTests(savePath.c_str(),image,image),"actual save event prepares metadata association");
    Check(CommitLoadForTests(image),"native load boundary admits the matching inventory on its owner thread");
    workshop::State state{};Check(Capture(state),"in-game snapshot is available after load");
    unsigned slot=200;for(unsigned i=0;i<200;++i)if(state.pieces[i].id && state.pieces[i].native[6]==255 && !(state.pieces[i].native[3]&12) && state.pieces[i].native[4]<7 && state.pieces[i].native[5]<2){slot=i;break;}
    if(slot==200)return 2;
    workshop::Request r{};r.op=workshop::Op::Mode;r.slot=static_cast<std::uint16_t>(slot);r.pieceId=state.pieces[slot].id;r.revision=state.revision;r.value=1;
    workshop::Plan plan{};Check(Preview(r,plan)==workshop::Error::Ok,"live preview uses the shared tested rules");
    Check(Commit(r,plan),"live transaction commits the exact reviewed revision");
    Check(!Commit(r,plan),"repeated confirmation cannot commit twice");
    Check(Capture(state)&&state.pieces[slot].mode==1,"runtime retains the committed piece extension");
    Check(WriteForTests(savePath.c_str(),image),"successful native save publishes its bound sidecar");
    auto create=reinterpret_cast<unsigned(__cdecl*)(const void*)>(base+0x3AB930);
    auto remove=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x3ABCC0);
    unsigned char gear[22]{};gear[2]=1;gear[6]=255;gear[11]=4;for(unsigned i=0;i<4;++i){gear[14+2*i]=100;gear[15+2*i]=128;}
    const unsigned created=create(gear);
    Check(created>=0x5000&&created<0x50C8&&Capture(state),"real native creation keeps the runtime inventory coherent");
    const unsigned index=created&0xFFF;const auto id=state.pieces[index].id;remove(created);
    Check(Capture(state)&&state.pieces[index].id==0,"real native free retires the actual piece identity");
    create(gear);Check(Capture(state)&&state.pieces[index].id!=id,"real slot reuse receives a new identity");
    auto step=[&](workshop::Op operation,unsigned value){
        if(!Capture(state))return false;
        workshop::Request request{};request.op=operation;request.slot=static_cast<std::uint16_t>(index);
        request.pieceId=state.pieces[index].id;request.revision=state.revision;request.value=static_cast<std::uint16_t>(value);
        workshop::Plan planned{};return Preview(request,planned)==workshop::Error::Ok&&Commit(request,planned);
    };
    const auto keySpheres=state.items[84];
    Check(step(workshop::Op::UnlockFifth,0)&&Capture(state)&&state.items[84]+1==keySpheres,"fifth unlock debits the actual native material slot once");
    Check(step(workshop::Op::SetFifth,0x8055)&&Capture(state)&&state.pieces[index].fifth==0x8055,"selected fifth ability reaches the production runtime");
    const auto hasAbility=reinterpret_cast<int(__cdecl*)(const void*,unsigned)>(base+0x3A0C40);
    const auto* nativePiece=reinterpret_cast<const unsigned char*>(base+0xD30F2C+22*index);
    Check(nativePiece[11]==4&&hasAbility(nativePiece,0x8055)==1,"real native direct-ID queries see the fifth while the record stays22 bytes");
    SaveImage saved=image;std::memcpy(saved.data()+64,reinterpret_cast<const void*>(base+0xD2CA90),0x68C0);
    FfxHooks::RonsoPool::SealSave(saved);
    Check(WriteForTests(savePath.c_str(),saved),"native save event persists the edited live inventory and extension together");
    const auto savedIdentity=state.pieces[index].id,oldRevision=state.revision;
    Check(LoadForTests(savePath.c_str(),saved,saved)&&CommitLoadForTests(saved)&&Capture(state)&&state.pieces[index].id==savedIdentity&&state.pieces[index].fifth==0x8055,"real load boundary restores the same piece and fifth ability");
    Check(state.revision>oldRevision,"load invalidates confirmations from the previous session");
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
    equip(0,0,created);
    Check(Capture(state)&&state.pieces[index].native[6]==0,"real native equip preserves the modified piece identity");
    const auto aggregate=reinterpret_cast<int(__cdecl*)(unsigned)>(base+0x39C610);
    aggregate(0);
    Check((actor[0x632]&0x10)!=0,"production five-entry native aggregator applies fifth Auto-Protect");
    aggregate(0);
    Check((actor[0x632]&0x10)!=0&&kernel==originalKernel,"repeated aggregation preserves the global ability kernel");
    RequestStop();Check(!FfxHooks::NativeSaveEvents::Requested()&&!Capture(state),"stop closes mutations and save subscriptions");
    aggregate(0);
    Check((actor[0x632]&0x10)==0,"retained hooks provide safe vanilla views after stop");
    std::printf("EquipmentWorkshopRuntimeRt1 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
