#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "../shared/ExecutableProfile.h"
#include "../hooks/OmegaT7Session.h"
#include "../hooks/NativeSaveEvents.h"
#include "../hooks/EquipmentWorkshopStore.h"
#include "../hooks/RonsoPoolStore.h"
#include "../hooks/RonsoPoolSave.h"
#include <cstdio>
#include <fstream>
#include <chrono>
namespace O=FfxHooks::OmegaT7;namespace S=O::Session;namespace E=FfxHooks::NativeSaveEvents;namespace P=FfxHooks::ExecutableProfile;
namespace FfxHooks::EquipmentWorkshop {
bool Fingerprint(const void* p,std::size_t size,Hash& out){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD object=0,received=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object),sizeof(object),&received,0)>=0;
    std::vector<unsigned char> state(object);
    ok=ok&&BCryptCreateHash(algorithm,&hash,state.data(),object,nullptr,0,0)>=0&&BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(p)),static_cast<ULONG>(size),0)>=0&&BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}}
namespace FfxHooks::RonsoPool {bool OwnerStore::IsSavePath(const std::wstring& path) noexcept {return path.find(L"ffx_")!=std::wstring::npos;}}
static unsigned checks=0,failures=0;static std::uintptr_t base=0;static std::uint64_t cookie=0;
static void Check(bool v,const char* text){++checks;if(!v){++failures;std::printf("FAIL %s\n",text);}}
static void Log(const char* text){std::fputs(text,stdout);}
static void Time(unsigned value){std::memcpy(reinterpret_cast<void*>(base+P::Rva<0xD2CA90>()+0xBC),&value,4);}
static void Write(const std::filesystem::path& p,const FfxHooks::RonsoPool::SaveImage& b){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());}
static void Load(const std::filesystem::path& path,FfxHooks::RonsoPool::SaveImage& bytes){
    E::ReadCompleted(path.c_str(),bytes.data(),bytes.data(),bytes.size());const auto token=++cookie;
    E::Dispatch([&](const E::Observer& observer){if(observer.loadStarting)observer.loadStarting(token,reinterpret_cast<void*>(base+P::Rva<0xD2CA90>()),bytes.data());});
    std::memcpy(reinterpret_cast<void*>(base+P::Rva<0xD2CA90>()),bytes.data()+64,0x6034);
    E::Dispatch([&](const E::Observer& observer){if(observer.loadCompleted)observer.loadCompleted(token,true);});
}
int main(int argc,char** argv){
    if(argc!=2)return 2;std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto root=std::filesystem::path(argv[1])/std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());std::filesystem::create_directories(root);
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0xD40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!base)return 2;
    const auto save=root/L"ffx_000",other=root/L"ffx_001";FfxHooks::RonsoPool::SaveImage bytes{};
    const unsigned stamp=100;std::memcpy(bytes.data()+64+0xBC,&stamp,4);bytes[64]=2;bytes[65]=1;Write(save,bytes);const auto original=bytes;
    S::Prime(base,root,false,false,Log);Check(!S::Prepared(),"OFF does not acquire save callbacks");S::Prime(base,root,true,true,Log);Check(!S::Prepared(),"validate-only has no metadata writer");
    S::Prime(base,root,true,false,Log);Check(S::Prepared()&&!S::Ready(),"save callbacks wait for verified native identity");
    Load(save,bytes);O::Campaign initial;std::uint64_t session=0;Check(S::Ready()&&S::Snapshot(initial,session),"native load binds and persists a campaign");
    const auto seed=initial.seed.seed;Check(S::BattleSeed(77)==seed,"battle pins the campaign seed");
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2A8E0>())=1;Time(100+initial.seed.duration);S::Tick();O::Campaign expired;std::uint64_t same=0;S::Snapshot(expired,same);
    Check(expired.seed.due&&expired.seed.seed==seed&&S::BattleSeed(77)==seed,"expiry cannot change an ongoing battle");
    *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2A8E0>())=0;S::Field(true);O::Campaign renewed;S::Snapshot(renewed,same);
    Check(renewed.seed.epoch==2&&renewed.seed.seed!=seed&&renewed.seed.renewalPending,"field resumption publishes a renewal");
    Load(save,bytes);O::Campaign reloaded;std::uint64_t secondSession=0;S::Snapshot(reloaded,secondSession);
    Check(reloaded.seed.seed==renewed.seed.seed&&reloaded.seed.epoch==2&&secondSession!=session,"reloading the unchanged old save retains the new campaign seed");
    Check(!S::SetLoreSeen(session)&&S::SetLoreSeen(secondSession),"old dialog receipt cannot acknowledge another load; current receipt persists");
    S::Snapshot(reloaded,same);Check(reloaded.seed.loreSeen,"lore receipt belongs to the persistent campaign");
    bytes[900]=4;Write(other,bytes);const auto write=++cookie;E::WriteStaged(write,other.c_str(),bytes.data(),bytes.size());E::WriteVerified(write,other.c_str(),bytes.data(),bytes.size());
    Load(other,bytes);O::Campaign inherited;S::Snapshot(inherited,same);Check(inherited.id==renewed.id&&inherited.seed.seed==renewed.seed.seed&&inherited.seed.loreSeen,"verified save-as inherits seed, deadline and lore");
    bytes=original;Load(save,bytes);O::Campaign back;S::Snapshot(back,same);Check(back.id==inherited.id&&back.seed.seed==inherited.seed.seed,"switching between campaign slots cannot reroll");
    Check(S::ClearRenewal(same,back.seed.epoch),"renewal acknowledgment is epoch-bound");
    E::ReadRejected();Check(!S::Ready(),"rejected native loads close admission without fresh adoption");
    Load(save,bytes);Check(S::Ready(),"a later verified load recovers the same known campaign");
    O::Store store(root/L"modules"/L"config"/L"omega-t7");std::ofstream corrupt(store.CampaignPath(back.id),std::ios::binary);corrupt<<"corrupt";corrupt.close();
    Load(save,bytes);Check(!S::Ready(),"corrupt campaign closes admission instead of granting a fresh seed");
    std::ifstream input(save,std::ios::binary);FfxHooks::RonsoPool::SaveImage actual{};input.read(reinterpret_cast<char*>(actual.data()),actual.size());Check(actual==original,"all seed and notice writes preserve the native save file");
    S::Stop();Check(!S::Ready()&&!S::Prepared(),"stop retires only this metadata consumer");
    std::printf("OmegaT7SessionRt1: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
