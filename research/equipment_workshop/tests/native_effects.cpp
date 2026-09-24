// RT1 only: privately maps verified PE bytes. Never launches FFX or installs DLLs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "effects.h"
#include "lifecycle.h"
using namespace workshop;
static unsigned checks,failed;
static void Check(bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL %s\n",label);}}
static std::vector<unsigned char> Read(const char* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static std::string Sha(const std::vector<unsigned char>& b){
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;std::string result;
    if(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) && CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) && CryptHashData(hash,b.data(),static_cast<DWORD>(b.size()),0)){
        std::array<unsigned char,32> out{};DWORD size=32;
        if(CryptGetHashParam(hash,HP_HASHVAL,out.data(),&size,0))for(auto c:out){char pair[3]{};std::snprintf(pair,sizeof(pair),"%02x",c);result+=pair;}
    }
    if(hash)CryptDestroyHash(hash);
    if(provider)CryptReleaseContext(provider,0);
    return result;
}
static void Put32(unsigned char* p,std::uint32_t v){std::memcpy(p,&v,4);}
static unsigned char* Map(const std::vector<unsigned char>& b){
    if(b.size()<sizeof(IMAGE_DOS_HEADER))return nullptr;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(b.data());
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || static_cast<std::size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>b.size())return nullptr;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(b.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386)return nullptr;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image)return nullptr;
    std::memcpy(image,b.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        if(s.PointerToRawData+s.SizeOfRawData>b.size() || s.VirtualAddress+s.SizeOfRawData>nt->OptionalHeader.SizeOfImage){VirtualFree(image,0,MEM_RELEASE);return nullptr;}
        std::memcpy(image+s.VirtualAddress,b.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image)-nt->OptionalHeader.ImageBase);
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for(unsigned at=0;at<directory.Size;){
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image+directory.VirtualAddress+at);
        if(block->SizeOfBlock<8 || at+block->SizeOfBlock>directory.Size){VirtualFree(image,0,MEM_RELEASE);return nullptr;}
        const auto* words=reinterpret_cast<const WORD*>(block+1);
        for(unsigned i=0;i<(block->SizeOfBlock-8)/2;++i)if((words[i]>>12)==IMAGE_REL_BASED_HIGHLOW){auto* p=image+block->VirtualAddress+(words[i]&0xFFF);std::uint32_t v;std::memcpy(&v,p,4);Put32(p,v+delta);}
        at+=block->SizeOfBlock;
    }
    return image;
}
static Effects effects;
static State state;
static unsigned char* image;
static std::vector<unsigned char> kernel;
static std::array<unsigned char,24> gear;
static std::array<std::array<unsigned char,108>,5> rows;
static unsigned cursor;
static unsigned activeSlot;
static const void* __cdecl GearView(unsigned code,void*){
    if(code==255)return nullptr;
    if(code!=activeSlot || !effects.Gear(activeSlot,state.pieces[activeSlot].native,gear))return nullptr;
    return gear.data();
}
static const void* __cdecl RowView(unsigned id,void*,void*){
    while(cursor<5 && Ability(state.pieces[activeSlot],cursor)==Empty)++cursor;
    if(cursor>=5 || Ability(state.pieces[activeSlot],cursor)!=0x8000+id)return nullptr;
    const auto slot=cursor++;
    if(!effects.AbilityRow(activeSlot,slot,kernel.data()+20+id*108,rows[slot]))return nullptr;
    return rows[slot].data();
}
static void Branch(unsigned char* at,unsigned char opcode,const void* target){
    at[0]=opcode;Put32(at+1,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(at)-5));
}
static State Fixture(std::uint16_t fifth,unsigned rank){
    std::array<unsigned char,4400> native{};std::array<std::uint16_t,112> items{};
    native[2]=1;native[6]=0;native[11]=4;
    for(unsigned i=0;i<4;++i){native[14+2*i]=255;native[15+2*i]=0;}
    State s{};Check(Import(native.data(),items.data(),1,s)==Error::Ok,"fixture uses production importer");
    s.pieces[0].fifthUnlocked=1;s.pieces[0].fifth=fifth;s.pieces[0].abilities[4]=s.nextId++;
    s.pieces[0].mode=1;s.pieces[0].rank=static_cast<unsigned char>(rank);return s;
}
static int Numeric(unsigned statIndex=10){
    // This entry bridge initializes the same frame locals as the field-stat
    // producer. The real PE's five-entry consumer and percent arithmetic run.
    // It skips grid/kernel setup before 786786 and exits before unrelated writes.
    auto* entry=static_cast<unsigned char*>(VirtualAlloc(nullptr,512,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    std::vector<unsigned char> code={0x55,0x8B,0xEC,0x81,0xEC,0xA0,0,0,0,0x53,0x56,0x57};
    auto imm=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)code.push_back(static_cast<unsigned char>(v>>(8*i)));};
    for(int at=-0x74;at<=-8;at+=4){code.insert(code.end(),{0xC7,0x85});imm(static_cast<std::uint32_t>(at));imm(at>=-0x3C?100:0);}
    code.push_back(0xB8);imm(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gear.data())));
    static std::array<unsigned char,128> flags{};flags.fill(0);code.push_back(0xBB);imm(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(flags.data())));
    code.push_back(0xE9);imm(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image+0x386786)-reinterpret_cast<std::uintptr_t>(entry)-code.size()-4));
    // Strength's numeric flag0400 is local index10: -3C + 4*10 == -14.
    const std::size_t tail=code.size();code.insert(code.end(),{0x8B,0x45,static_cast<unsigned char>(-0x3C+4*statIndex),0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3});
    std::memcpy(entry,code.data(),code.size());
    std::array<unsigned char,6> oldExit{};std::memcpy(oldExit.data(),image+0x386802,6);
    std::array<unsigned char,5> oldTail{};std::memcpy(oldTail.data(),image+0x386850,5);
    Branch(image+0x386802,0xE9,image+0x386818);image[0x386807]=0x90;
    Branch(image+0x386850,0xE9,entry+tail);
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);cursor=0;
    const int value=reinterpret_cast<int(__cdecl*)()>(entry)();
    std::memcpy(image+0x386802,oldExit.data(),6);std::memcpy(image+0x386850,oldTail.data(),5);
    VirtualFree(entry,0,MEM_RELEASE);return value;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3 && argc!=4)return 2;
    const auto pe=Read(argv[1]);kernel=Read(argv[2]);
    Check(Sha(pe)=="78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced","exact supported PC PE identity");
    Check(Sha(kernel)=="d0610e7d37cde6e65298da116f6dc05a69236126d9ff157c2db65d17a97729aa","exact native ability kernel identity");
    if(failed)return 2;
    image=Map(pe);if(!image)return 2;
    const unsigned char entry[]={0x55,0x8B,0xEC,0x83,0xEC,0x14};
    Check(std::memcmp(image+0x39C610,entry,sizeof(entry))==0,"actor aggregate signature");
    Check(image[0x39C8A3]==0xB9 && image[0x39C8A4]==4 && image[0x386786]==0xB9 && image[0x386787]==4,"both native consumers have independent four-slot loops");
    if(failed)return 2;
    // All writes below affect this private image, not the installed game or DLL.
    Branch(image+0x39C77D,0xE8,reinterpret_cast<void*>(&GearView));
    Branch(image+0x39C8D4,0xE8,reinterpret_cast<void*>(&RowView));
    Branch(image+0x3867B0,0xE8,reinterpret_cast<void*>(&RowView));
    image[0x39C8A4]=5;image[0x386787]=5;
    state=Fixture(0x8064,0);
    Check(!effects.Begin(false,true,state),"adapter defaults off");
    Check(!effects.Begin(true,false,state),"unsupported profile remains off");
    Check(effects.Begin(true,true,state) && effects.Gear(0,state.pieces[0].native,gear),"admitted identity supplies a private fifth word");
    Check(Numeric()==110,"real native field-stat consumer includes fifth Strength +10 percent");
    state.pieces[0].rank=10;effects.Begin(true,true,state);effects.Gear(0,state.pieces[0].native,gear);
    Check(Numeric()==120,"real native percentage arithmetic includes ten refinement points");
    Check(Numeric()==120,"repeated refresh never compounds refinement");
    Check(state.pieces[0].native[11]==4 && kernel[20+100*108+0x55]==10,"original capacity and global kernel remain unchanged");
    auto unknown=state;unknown.pieces[0].native[14]=0x86;unknown.pieces[0].native[15]=0x80;
    unknown.pieces[0].abilities[0]=unknown.nextId++;
    Check(!effects.Begin(true,true,unknown),"kernel profile rejects an out-of-range original ability before native reads");
    for(unsigned id=98;id<=121;++id){
        const auto* row=kernel.data()+20+id*108;const unsigned mask=row[0x56]|(unsigned(row[0x57])<<8);
        unsigned index=0;while(index<14 && !(mask&(1u<<index)))++index;
        Check(index<14 && mask==(1u<<index),"numeric kernel row owns one known stat channel");
        if(index>=14)return 2;
        for(unsigned rank=0;rank<=10;++rank){
            state=Fixture(static_cast<std::uint16_t>(0x8000+id),rank);
            effects.Begin(true,true,state);effects.Gear(0,state.pieces[0].native,gear);
            Check(Numeric(index)==100+int(row[0x55])+int(rank),"all numeric abilities and ranks execute native arithmetic");
        }
    }
    state=Fixture(0x8055,10);effects.Begin(true,true,state);
    std::array<unsigned char,0xF90> actor{};
    Put32(image+0xD334CC,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actor.data())));
    auto* ply=image+0xD3205C;std::memset(ply,0,0x94);ply[0x2D]=0;ply[0x2E]=255;
    Put32(ply+0x24,1000);Put32(ply+0x28,100);cursor=0;
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    reinterpret_cast<int(__cdecl*)(int)>(image+0x39C610)(0);
    Check((actor[0x632]&0x10)!=0,"full native actor aggregator applies fifth Auto-Protect flag");
    std::array<unsigned char,64> command{};command[0x20]=1;
    std::array<unsigned char,64> statuses{};statuses[0xB]=(actor[0x632]&0x10)?1:0;
    unsigned flags=0;int divisor=0;
    auto protect=reinterpret_cast<int(__cdecl*)(const void*,unsigned*,int*,const void*,int)>(image+0x38AE00);
    const auto base=protect(command.data(),&flags,&divisor,statuses.data(),1000);
    Check(base==500 && divisor==2 && (flags&0x40)!=0,"real native Protect consumer halves physical damage");
    Check(effects.AfterStatus(base,(flags&0x40)!=0,false,0,200,0)==450,"rank ten applies one bounded reduction after native Protect");
    Check(effects.AfterStatus(base,false,false,0,200,0)==500,"inactive status gives no refinement benefit");
    Check(effects.AfterStatus(base,true,true,0,200,0)==500,"Protect does not refine magic damage");
    Check(effects.AfterStatus(base,true,false,0,200,1)==500,"other owner cannot inherit the effect");
    state=Fixture(0x8054,10);effects.Begin(true,true,state);cursor=0;
    reinterpret_cast<int(__cdecl*)(int)>(image+0x39C610)(0);
    Check((actor[0x632]&8)!=0,"full native aggregator applies fifth Auto-Shell flag");
    command[0x20]=2;statuses[0xA]=(actor[0x632]&8)?1:0;flags=0;divisor=0;
    auto shell=reinterpret_cast<int(__cdecl*)(const void*,unsigned*,int*,const void*,int)>(image+0x38AE80);
    const auto magic=shell(command.data(),&flags,&divisor,statuses.data(),1000);
    Check(magic==500 && (flags&0x20)!=0 && effects.AfterStatus(magic,true,true,0,200,0)==450,"native Shell and its rank ten reduction compose once");
    std::array<unsigned char,24> foreign{};bool foreignOk=true;
    std::thread thread([&]{foreignOk=effects.Gear(0,state.pieces[0].native,foreign);effects.End();});thread.join();
    Check(!foreignOk && effects.Gear(0,state.pieces[0].native,gear),"foreign thread cannot read or retire owner views");
    auto changed=state.pieces[0];changed.native[0]^=1;
    Check(!effects.Gear(0,changed.native,gear),"changed native identity rejects effect views");
    effects.End();effects.End();Check(!effects.Gear(0,state.pieces[0].native,gear),"idempotent teardown removes admission");
    // Exercise actual regular-inventory producers, with extension identities
    // attached only to a verified completed event and exact before/after bytes.
    auto* records=image+0xD30F2C;std::memset(records,0,4400);
    // A zeroed PlySave names slot0 as equipped. Native swap repairs record owner
    // bytes from all18 character slots, so the fixture must express "none" as FF.
    for(unsigned i=0;i<18;++i){ply[i*0x94+0x2D]=255;ply[i*0x94+0x2E]=255;}
    std::array<unsigned char,22> templateGear{};templateGear[2]=1;templateGear[6]=255;templateGear[11]=4;
    for(unsigned i=0;i<4;++i){templateGear[14+2*i]=100;templateGear[15+2*i]=128;}
    std::array<std::uint16_t,112> inventoryItems{};State inventoryState{};
    Check(Import(records,inventoryItems.data(),1,inventoryState)==Error::Ok,"empty native inventory imports");
    Lifecycle lifecycle;Check(!lifecycle.Begin(false,true,inventoryState),"native lifecycle adapter defaults off");
    Check(lifecycle.Begin(true,true,inventoryState),"verified native lifecycle admission");
    auto create=reinterpret_cast<unsigned(__cdecl*)(const void*)>(image+0x3AB930);
    auto swap=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(image+0x3ABA10);
    auto remove=reinterpret_cast<int(__cdecl*)(unsigned)>(image+0x3ABCC0);
    auto equip=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(image+0x3AB990);
    std::array<unsigned char,4400> before{};
    for(unsigned slot=0;slot<2;++slot){
        std::memcpy(before.data(),records,4400);const auto result=create(templateGear.data());
        Check(result==0x5000+slot && lifecycle.Observe(InventoryEvent::Created,slot,0,0,inventoryState.revision,before.data(),records),"actual native register gives each identical piece its own identity");
        lifecycle.Snapshot(inventoryState);
    }
    inventoryState.pieces[0].mode=2;inventoryState.pieces[0].ranks[0]=7;Check(lifecycle.Begin(true,true,inventoryState),"refined lifecycle fixture validates before native swap");
    const auto identity=inventoryState.pieces[0].id;std::memcpy(before.data(),records,4400);swap(0x5000,0x5001);
    if(std::memcmp(before.data(),records,4400)!=0){unsigned shown=0;for(unsigned i=0;i<4400 && shown<8;++i)if(before[i]!=records[i]){std::printf("SWAP_DIFF %u %02X %02X\n",i,before[i],records[i]);++shown;}}
    Check(std::memcmp(before.data(),records,4400)==0 && lifecycle.Observe(InventoryEvent::Swapped,0,1,0,inventoryState.revision,before.data(),records),"byte-identical native swap still moves identities by producer event");
    lifecycle.Snapshot(inventoryState);Check(inventoryState.pieces[1].id==identity && inventoryState.pieces[1].ranks[0]==7,"native swap preserves only the correct piece's rank");
    ply[0x2D]=255;std::memcpy(before.data(),records,4400);equip(0,0,0x5001);
    Check(lifecycle.Observe(InventoryEvent::Equipped,1,GearCount,0,inventoryState.revision,before.data(),records),"native equip preserves the instance while observing its owner");
    lifecycle.Snapshot(inventoryState);std::memcpy(before.data(),records,4400);remove(0x5001);
    Check(lifecycle.Observe(InventoryEvent::Removed,1,0,0,inventoryState.revision,before.data(),records),"actual native free retires the equipped extension");
    lifecycle.Snapshot(inventoryState);Check(inventoryState.pieces[1].id==0 && ply[0x2D]==255,"native free and extension both release equipment ownership");
    std::memcpy(before.data(),records,4400);create(templateGear.data());
    Check(lifecycle.Observe(InventoryEvent::Created,1,0,0,inventoryState.revision,before.data(),records),"reused native inventory slot is a fresh creation");
    lifecycle.Snapshot(inventoryState);Check(inventoryState.pieces[1].id!=identity && inventoryState.pieces[1].ranks[0]==0,"native replacement never inherits a retired rank");
    std::memcpy(before.data(),records,4400);swap(0x5000,0x5001);records[44]^=1;
    Check(!lifecycle.Observe(InventoryEvent::Swapped,0,1,0,inventoryState.revision,before.data(),records) && !lifecycle.Snapshot(inventoryState),"foreign mutation quarantines the entire extension lane");
    if(argc==4){
        const auto snapshot=Read(argv[3]);
        Check(snapshot.size()==sizeof(State),"saved host snapshot has exact v1 size");
        if(snapshot.size()!=sizeof(State))return 2;
        std::memcpy(&state,snapshot.data(),sizeof(State));
        Check(Validate(state)==Error::Ok,"saved host snapshot validates through the production core");
        activeSlot=GearCount;
        for(unsigned slot=0;slot<GearCount;++slot)if(state.pieces[slot].fifthUnlocked && state.pieces[slot].fifth==0x8055){activeSlot=slot;break;}
        if(activeSlot==GearCount)return 2;
        auto& selected=state.pieces[activeSlot];const unsigned owner=selected.native[4];
        if(owner>6 || selected.native[5]>1)return 2;
        // Test-only equip context for the saved piece; source snapshot is read only.
        selected.native[6]=static_cast<unsigned char>(owner);
        auto* ownerPly=ply+owner*0x94;std::memset(ownerPly,0,0x94);ownerPly[0x2D]=ownerPly[0x2E]=255;
        ownerPly[0x2D+selected.native[5]]=static_cast<unsigned char>(activeSlot);
        Put32(ownerPly+0x24,1000);Put32(ownerPly+0x28,100);
        Put32(image+0xD334CC,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(actor.data())-owner*0xF90));
        effects.Begin(true,true,state);cursor=0;
        reinterpret_cast<int(__cdecl*)(int)>(image+0x39C610)(static_cast<int>(owner));
        command[0x20]=1;statuses[0xB]=(actor[0x632]&0x10)?1:0;flags=0;divisor=0;
        const auto protectedDamage=protect(command.data(),&flags,&divisor,statuses.data(),1000);
        const unsigned rank=selected.mode==1?selected.rank:selected.ranks[4];
        const auto finalDamage=effects.AfterStatus(protectedDamage,(flags&0x40)!=0,false,
            selected.native[5]==0?activeSlot:GearCount,selected.native[5]==1?activeSlot:GearCount,owner);
        Check(protectedDamage==500 && finalDamage==static_cast<int>(500*(100-rank)/100),"UI-saved fifth ability and rank reach native damage consumers");
        std::printf("WORKSHOP_HOST_SNAPSHOT piece=%llu rank=%u damage=%d\n",static_cast<unsigned long long>(selected.id),rank,finalDamage);
        effects.End();
    }
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("WORKSHOP_NATIVE %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
