#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "../hooks/SinAiHook.h"
#include "../hooks/SinMetadataCore.h"
#include "MusicPeFixture.inc"
namespace A=FfxHooks::SinAi;
namespace S=FfxHooks::SinSpread;
namespace M=FfxHooks::SinMetadata;
static int checks,failures;
static A::Context context{};
static bool admitted=true;
static unsigned reported=0;
static std::uintptr_t imageBase=0;
static LONG WINAPI Fatal(EXCEPTION_POINTERS* error){std::printf("SIN_NATIVE_EXCEPTION code=0x%08lX pc=%p rva=0x%08lX esp=0x%08lX ebp=0x%08lX\n",error->ExceptionRecord->ExceptionCode,error->ExceptionRecord->ExceptionAddress,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(error->ExceptionRecord->ExceptionAddress)-imageBase),error->ContextRecord->Esp,error->ContextRecord->Ebp);auto frame=error->ContextRecord->Ebp;for(unsigned i=0;i<8 && frame;++i){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(frame),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))break;const auto* p=reinterpret_cast<const DWORD*>(frame);std::printf("SIN_NATIVE_FRAME ret=0x%08lX rva=0x%08lX\n",p[1],static_cast<unsigned long>(p[1]-imageBase));if(p[0]<=frame)break;frame=p[0];}return EXCEPTION_EXECUTE_HANDLER;}
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static bool Context(bool,A::Context* out){*out=context;return admitted;}
static void Report(unsigned slot,std::uint64_t generation){if(generation==context.generation && slot<8)reported|=1u<<slot;}
static void Put32(void* base,unsigned offset,std::uintptr_t value){*reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(base)+offset)=static_cast<std::uint32_t>(value);}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    SetUnhandledExceptionFilter(Fatal);
    if(argc!=5)return 2;
    std::printf("SIN_NATIVE_STAGE map\n");auto* image=MapPe(Read(argv[1]));if(!image){std::printf("SIN_NATIVE_MAP_REJECTED\n");return 2;}imageBase=reinterpret_cast<std::uintptr_t>(image);
    const auto base=reinterpret_cast<std::uintptr_t>(image);const auto pack=Read(argv[2]);auto commands=Read(argv[3]);
    if(pack.size()<12||commands.size()<20){std::puts("SIN_NATIVE_FIXTURE_MISSING pack or monster2 command table");return 2;}
    using NativeEntry=const unsigned char*(__cdecl*)(unsigned,const unsigned char*,unsigned);
    const auto lookupCommand=reinterpret_cast<NativeEntry>(image+FfxHooks::ExecutableProfile::Rva<0x3AB890>());
    for(unsigned id:{263u,264u,265u,272u,303u,309u,315u,318u}){
        const auto* row=lookupCommand(id,commands.data(),0);
        const auto* expected=commands.data()+A::U32(commands.data()+16)+id*A::U16(commands.data()+12);
        Check(row==expected,"actual native lookup selects the exact Mana Storm or Counter March record");
        if(row!=expected)return 2;
        if(id==265)Check(A::U16(row+16)==86&&A::U16(row+18)==87&&row[21]==3,
            "the final Mana Storm uses the native Thundara cast/impact pair with its matching caster animation");
        if(id==272)Check(A::U16(row+16)==774&&A::U16(row+18)==775,
            "Counter March resolves its own authored visual pair rather than player Delay Attack");
        if(id==315)Check(A::U16(row+16)==811&&A::U16(row+18)==811&&row[21]==1,
            "Rhythm Impact preserves its own overlay while selecting physical caster motion");
        if(id==318)Check(A::U16(row+16)==814&&A::U16(row+18)==814&&row[21]==10,
            "Demolishing Fist preserves its own overlay and the native Blast Punch caster contract");
        if(id==309)Check(A::U16(row+16)==805&&A::U16(row+18)==876&&row[21]==23,
            "Dragging Strike uses separate owned cast/impact phases and the native Delay Attack caster contract");
        if(id==303)Check(row[0x46]==0&&row[0x53]==0,
            "Silica Shards cannot inherit Slow absent from the UNI-014 design");
    }
    if(failures)return 1;
    std::vector<unsigned char> actors(8*0xF90);
    Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34460>(),reinterpret_cast<std::uintptr_t>(actors.data()));Put32(image,FfxHooks::ExecutableProfile::Rva<0xD2A934>(),reinterpret_cast<std::uintptr_t>(commands.data()));
    const auto original=reinterpret_cast<A::RegisterFn>(image+A::kRegisterRva);
    Check(!A::PrepareForTests(0,pack.data(),pack.size(),Context,Report,original),"wrong executable identity cannot prepare scripts");
    Check(A::PrepareForTests(base,pack.data(),pack.size(),Context,Report,original),"exact profile and immutable script pack prepare");
    std::printf("SIN_NATIVE_STAGE profiles\n");
    using PrivateBytes=unsigned(__cdecl*)(const void*);
    const auto privateBytes=reinterpret_cast<PrivateBytes>(image+FfxHooks::ExecutableProfile::Rva<0x46C050>());
    std::vector<unsigned char> lastMon;const void* lastAi=nullptr;const void* retained=nullptr;unsigned lastMonster=0;
    for(const auto& proof:A::kProofs){
        char leaf[64]{};sprintf_s(leaf,"\\_m%03u\\m%03u.bin",proof.monster,proof.monster);
        auto mon=Read((std::string(argv[4])+leaf).c_str());Check(!mon.empty(),"native monster fixture loads");if(mon.empty())return 2;
        const auto* ai=mon.data()+A::U32(mon.data()+4);
        const S::MonsterEntry* definition=nullptr;
        for(const auto& entry:S::kMonsters)if(entry.id==proof.monster&&(entry.allowedMask&(std::uint64_t(1)<<(proof.curse-1)))){definition=&entry;break;}
        Check(definition!=nullptr,"profile belongs to an exact reviewed area/monster/UNI combination");if(!definition)return 2;
        std::uint16_t field=0;
        for(const auto& pair:S::kNaturalPairs)if(pair.monster==proof.monster&&S::AreaForField(pair.field)==definition->field){field=pair.field;break;}
        Check(field!=0,"profile fixture uses an actual natural field/monster pair");if(!field)return 2;
        context={true,field,0,1,S::Distribution::Most,context.generation+1};
        bool found=false;
        for(;context.seed<100000;++context.seed){const auto selection=S::BuildAssignment(context.field,context.seed,context.visit,context.distribution,true);if(selection.Find(proof.monster)->curse==proof.curse){found=true;break;}}
        Check(found,"deterministic seed reaches every compatible curated curse");if(!found)return 2;
        std::fill(actors.begin(),actors.end(),static_cast<unsigned char>(0));reported=0;
        Put32(actors.data(),2*0xF90+0x48,reinterpret_cast<std::uintptr_t>(mon.data()));
        Put32(actors.data(),2*0xF90+0xF78,reinterpret_cast<std::uintptr_t>(ai));
        *reinterpret_cast<std::uint16_t*>(actors.data()+2*0xF90+0xE)=S::NativeMonsterId(proof.monster);
        Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54,0);
        char name[8]{};sprintf_s(name,"m%03u",proof.monster);
        const auto beforeHash=A::Hash(ai,proof.originalSize);std::printf("SIN_NATIVE_STAGE register monster=%u original=%p private=%p\n",proof.monster,reinterpret_cast<void*>(original),reinterpret_cast<void*>(privateBytes));
        std::printf("SIN_NATIVE_STAGE original-only\n");Check(original(1,name,ai)==0,"direct actual register ABI");Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54,0);std::printf("SIN_NATIVE_STAGE dispatch\n");
        Check(A::RegisterForTests(1,name,ai,A::kRegisterCallerRva)==0,"real native channel registration accepts the selected script");
        const auto registered=*reinterpret_cast<std::uint32_t*>(image+FfxHooks::ExecutableProfile::Rva<0xD3447C>()+0x54);
        const auto* view=reinterpret_cast<const std::uint8_t*>(registered);
        Check(view!=ai && registered%16==0 && A::Hash(view,proof.size)==proof.hash,"native pending list contains the aligned exact curated AI");
        Check(*reinterpret_cast<std::uint32_t*>(image+FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54)==1 && reported==(1u<<2),"native original executes once and only the matching actor receives stat admission");
        Check(A::Hash(ai,proof.originalSize)==beforeHash && *reinterpret_cast<std::uint32_t*>(actors.data()+2*0xF90+0xF78)==reinterpret_cast<std::uintptr_t>(ai),"original actor file and AI pointer remain untouched");
        if(proof.monster==89){
            const auto previousField=context.field;
            for(unsigned calmField:{611u,612u,613u}){
                context.field=static_cast<std::uint16_t>(calmField);reported=0;
                Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54,0);
                A::RegisterForTests(1,name,ai,A::kRegisterCallerRva);
                const auto actual=*reinterpret_cast<std::uint32_t*>(image+FfxHooks::ExecutableProfile::Rva<0xD3447C>()+0x54);
                Check(actual==registered&&reported==(1u<<2),"Coeurl obtains its exact admitted curse on the real Calm Lands 611/612/613 routes");
            }
            context.field=previousField;
        }
        std::printf("SIN_NATIVE_STAGE private-size\n");const auto oldPrivate=privateBytes(ai),newPrivate=privateBytes(view);
        Check(newPrivate>=oldPrivate && newPrivate<=oldPrivate+16,"actual native allocator accounts for bounded private latch storage");
        lastMon=std::move(mon);lastAi=lastMon.data()+A::U32(lastMon.data()+4);retained=view;lastMonster=proof.monster;
    }
    char name[8]{};sprintf_s(name,"m%03u",lastMonster);
    auto runOriginal=[&](std::uint32_t caller){reported=0;Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54,0);const int result=A::RegisterForTests(1,name,lastAi,caller);return result==0&&*reinterpret_cast<std::uint32_t*>(image+FfxHooks::ExecutableProfile::Rva<0xD3447C>()+0x54)==reinterpret_cast<std::uintptr_t>(lastAi)&&reported==0;};
    Check(std::memcmp(image+FfxHooks::SinNatural::kStepRva,FfxHooks::SinNatural::kPrefix.data(),FfxHooks::SinNatural::kPrefix.size())==0 &&
        std::memcmp(image+FfxHooks::SinNatural::kCallerRva-5,FfxHooks::SinNatural::kCall.data(),5)==0,
        "the real random-step consumer and its sole walking caller match the supported PE");
    Check(std::memcmp(image+M::kRewardRva,M::kRewardPrefix.data(),M::kRewardPrefix.size())==0,
        "the real reward consumer reads its loot pointer as the third stack argument");
    auto* actor=actors.data()+2*0xF90;
    const auto* loot=lastMon.data()+A::U32(lastMon.data()+0x14);
    Put32(actor,static_cast<unsigned>(M::kLootPointerOffset),reinterpret_cast<std::uintptr_t>(loot));
    std::array<std::uint8_t,M::kNameSize> baseName{};
    const std::uint8_t encodedName[]={0x5C,0x84,0x81,0x84,0x82,0x82,0x84,0};
    std::memcpy(baseName.data(),encodedName,sizeof(encodedName));std::memcpy(actor+M::kNameOffset,baseName.data(),baseName.size());
    actor[M::kNameOffset+M::kNameSize]=0xCC;
    A::RefreshLabels(context.generation);
    const auto lastAssignment=S::BuildAssignment(context.field,context.seed,context.visit,context.distribution,true);
    const auto* curse=lastAssignment.Find(lastMonster);std::array<std::uint8_t,M::kNameSize> marked{};
    Check(curse && M::NameView(baseName,curse->curse,curse->threat,&marked) && std::memcmp(actor+M::kNameOffset,marked.data(),marked.size())==0,
        "a registered actor receives its own bounded native name indicator after population");
    A::RefreshLabels(context.generation);
    Check(std::memcmp(actor+M::kNameOffset,marked.data(),marked.size())==0 && actor[M::kNameOffset+M::kNameSize]==0xCC,
        "repeated name refresh cannot append twice or cross the forty-byte name boundary");
    const auto originalHash=A::Hash(lastMon.data(),lastMon.size());
    const auto* reward=static_cast<const std::uint8_t*>(A::RewardViewForTests(loot));
    Check(reward!=loot && A::U16(reward)==M::ScaledReward(A::U16(loot),curse->threat) &&
        A::U16(reward+2)==M::ScaledReward(A::U16(loot+2),curse->threat) && A::U16(reward+4)==M::ScaledReward(A::U16(loot+4),curse->threat),
        "the native adapter supplies scaled per-monster Gil and both AP variants");
    Check(std::memcmp(reward+6,loot+6,M::kLootSize-6)==0 && A::Hash(lastMon.data(),lastMon.size())==originalHash &&
        *reinterpret_cast<std::uint32_t*>(actor+M::kLootPointerOffset)==reinterpret_cast<std::uintptr_t>(loot),
        "native source files, actor resource ownership and non-reward loot bytes stay exact");
    const auto firstRewards=std::array<unsigned,3>{A::U16(reward),A::U16(reward+2),A::U16(reward+4)};
    const auto* repeated=static_cast<const std::uint8_t*>(A::RewardViewForTests(loot));
    Check(A::U16(repeated)==firstRewards[0] && A::U16(repeated+2)==firstRewards[1] && A::U16(repeated+4)==firstRewards[2] &&
        A::RewardViewForTests(reward)==reward,"reward reads are idempotent and an existing view cannot be scaled again");
    // Execute the real native normal/overkill selector and multiplier instructions.
    // A private-image tail returns the computed value before item/save consumers.
    std::uint8_t oldTail[7]{};std::memcpy(oldTail,image+M::kRewardRva+0x64,sizeof(oldTail));
    // EBX is also pushed at 799133, after the AP branch; both nonvolatile
    // registers must be restored by this test-only shortened epilogue.
    std::uint8_t returnValue[]={0x8B,0x45,0xFC,0x5B,0x5F,0xC9,0xC3};
    std::memcpy(image+M::kRewardRva+0x64,returnValue,sizeof(returnValue));FlushInstructionCache(GetCurrentProcess(),image+M::kRewardRva+0x64,sizeof(returnValue));
    auto nativeReward=reinterpret_cast<unsigned(__cdecl*)(unsigned,unsigned,const void*,unsigned)>(image+M::kRewardRva);
    image[FfxHooks::ExecutableProfile::Rva<0xD2A912>()]=image[FfxHooks::ExecutableProfile::Rva<0xD2A913>()]=0;
    Check(nativeReward(0,0,loot,0)==A::U16(loot+2) && nativeReward(0,0,reward,0)==firstRewards[1],"real native normal AP reads the extra reward view");
    Check(nativeReward(0,0,reward,1)==firstRewards[2],"real native overkill AP uses the scaled overkill field");
    image[FfxHooks::ExecutableProfile::Rva<0xD2A912>()]=1;
    Check(nativeReward(0,0,reward,0)==firstRewards[1]*100u,"native global AP multiplication composes after the curse reward");
    image[M::kRewardRva+0x66]=0xF8;image[FfxHooks::ExecutableProfile::Rva<0xD2A913>()]=1;FlushInstructionCache(GetCurrentProcess(),image+M::kRewardRva+0x64,sizeof(returnValue));
    Check(nativeReward(0,0,reward,0)==firstRewards[0]*100u,"native global Gil multiplication composes after the curse reward");
    std::memcpy(image+M::kRewardRva+0x64,oldTail,sizeof(oldTail));image[FfxHooks::ExecutableProfile::Rva<0xD2A912>()]=image[FfxHooks::ExecutableProfile::Rva<0xD2A913>()]=0;FlushInstructionCache(GetCurrentProcess(),image+M::kRewardRva+0x64,sizeof(oldTail));
    ++context.generation;Check(A::RewardViewForTests(loot)==loot,"a stale encounter cannot award bonus rewards");--context.generation;
    admitted=false;Check(A::RewardViewForTests(loot)==loot,"unadmitted encounters keep their native rewards");admitted=true;
    const void* foreignReward=nullptr;
    std::thread other([&]{foreignReward=A::RewardViewForTests(loot);A::RefreshLabels(context.generation);A::EndEncounter(context.generation);});other.join();
    Check(foreignReward==loot && A::RewardViewForTests(loot)!=loot && std::memcmp(actor+M::kNameOffset,marked.data(),marked.size())==0,
        "foreign threads cannot apply rewards, edit names or retire the owner's metadata");
    A::EndEncounter(context.generation);
    Check(std::memcmp(actor+M::kNameOffset,baseName.data(),baseName.size())==0 && A::RewardViewForTests(loot)==loot,
        "encounter cleanup restores the owned name and retires bonus reward admission");
    reported=0;Put32(image,FfxHooks::ExecutableProfile::Rva<0xD34468>()+0x54,0);A::RegisterForTests(1,name,lastAi,A::kRegisterCallerRva);
    actor[M::kNameOffset]=0x67;A::EndEncounter(context.generation);
    Check(actor[M::kNameOffset]==0x67,"cleanup preserves a foreign rename instead of overwriting it");
    const auto savedContext=context;
    std::memcpy(actor+M::kNameOffset,baseName.data(),baseName.size());
    context.field=310;++context.generation;
    Check(!runOriginal(A::kRegisterCallerRva)&&A::CurrentStatus().accepted==1,"a supported field obtains a fresh active script after the previous encounter ends");
    A::RefreshLabels(context.generation);
    context.field=999;++context.generation;
    Check(runOriginal(A::kRegisterCallerRva)&&A::CurrentStatus().code==A::StatusCode::Ready&&A::CurrentStatus().accepted==0,
          "a new unsupported event field cannot retain the previous encounter's installed-script status");
    Check(std::memcmp(actor+M::kNameOffset,baseName.data(),baseName.size())==0,
          "owner-thread transition restores the preceding owned name even when the next field has no curse profile");
    Check(A::RewardViewForTests(loot)==loot,"a new unsupported generation cannot reuse an old reward view");
    const auto nextGeneration=context.generation+1;context=savedContext;context.field=312;context.generation=nextGeneration;
    Check(!runOriginal(A::kRegisterCallerRva)&&A::CurrentStatus().accepted==1,
          "returning to the supported route creates a fresh registration without stale counters");
    admitted=false;Check(runOriginal(A::kRegisterCallerRva),"Force/Arena/stale contexts preserve the original script");admitted=true;
    Check(runOriginal(0x1234),"foreign callers cannot obtain a curated script");
    const auto commandEnd=A::U16(commands.data()+10);commands[10]=0;commands[11]=1;
    Check(runOriginal(A::kRegisterCallerRva),"missing Spira command indices never fall back to a different move");commands[10]=static_cast<unsigned char>(commandEnd);commands[11]=static_cast<unsigned char>(commandEnd>>8);
    auto* byte=const_cast<unsigned char*>(static_cast<const unsigned char*>(lastAi));byte[1]^=1;
    Check(runOriginal(A::kRegisterCallerRva),"unrecognized source AI is never silently replaced");byte[1]^=1;
    A::RequestStop();Check(runOriginal(A::kRegisterCallerRva),"stop leaves future registrations native");
    MEMORY_BASIC_INFORMATION protection{};Check(VirtualQuery(retained,&protection,sizeof(protection))&&protection.Protect==PAGE_READONLY,"registered script storage remains read-only and alive after stop");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("SinAiNativeRt1: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
