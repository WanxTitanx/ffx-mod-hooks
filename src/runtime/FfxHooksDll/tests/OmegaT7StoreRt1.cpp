#include "../hooks/OmegaT7Store.h"
#include <chrono>
#include <cstdio>
#include <fstream>
namespace O=FfxHooks::OmegaT7;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* text){++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}}
static void Write(const std::filesystem::path& p,const char* text){std::ofstream f(p,std::ios::binary);f<<text;}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const auto root=std::filesystem::path(argv[1])/std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);O::Store store(root/"campaigns");O::Hash id{},oldHash{},newHash{};id[0]=1;oldHash[1]=2;newHash[2]=3;
    auto campaign=O::AdoptCampaign(id);O::Bytes bytes{};O::Campaign decoded;
    Check(O::Encode(campaign,bytes)&&O::Decode(bytes,decoded)&&decoded.seed.seed==campaign.seed.seed,"campaign wire roundtrip");
    for(unsigned bit=0;bit<bytes.size()*8;++bit){auto bad=bytes;bad[bit/8]^=1u<<(bit%8);Check(!O::Decode(bad,decoded),"every one-bit corruption is rejected");}
    O::Reference reference{oldHash,id},loaded;
    Check(O::Encode(reference,bytes)&&O::Decode(bytes,loaded)&&loaded.native==oldHash&&loaded.campaign==id,"save reference binds the native fingerprint to a campaign");
    for(unsigned bit=0;bit<bytes.size()*8;++bit){auto bad=bytes;bad[bit/8]^=1u<<(bit%8);Check(!O::Decode(bad,loaded),"reference corruption cannot relabel a campaign");}
    const auto save=root/"ffx_000";Write(save,"native save bytes remain owned by the game");
    Check(store.ReadCampaign(id,decoded)==O::StoreCode::Missing&&store.SaveCampaign(campaign),"first campaign is created explicitly");
    Check(store.AdoptReference(save,reference)&&store.AdoptReference(save,reference),"first legacy adoption is idempotent");
    Check(store.ReadReference(save,oldHash,loaded)==O::StoreCode::Found&&store.ReadCampaign(loaded.campaign,decoded)==O::StoreCode::Found,"load resolves the authoritative campaign separately from the save snapshot");
    O::Seed::Clock clock;clock.Anchor(0);clock.Tick(campaign.seed,campaign.seed.duration,false);++campaign.revision;campaign.seed.loreSeen=true;
    Check(store.SaveCampaign(campaign)&&store.ReadReference(save,oldHash,loaded)==O::StoreCode::Found&&store.ReadCampaign(loaded.campaign,decoded)==O::StoreCode::Found&&decoded.seed.seed==campaign.seed.seed&&decoded.seed.epoch==2,
        "reload of the same older native save keeps the renewed seed and lore receipt");
    auto stale=O::AdoptCampaign(id);Check(!store.SaveCampaign(stale),"older state cannot overwrite a newer campaign revision");
    stale=campaign;stale.seed.loreSeen=false;Check(!store.SaveCampaign(stale),"same revision with different state is a conflict");
    const auto second=root/"ffx_001";Write(second,"another native save image");const O::Reference next{newHash,id};
    Check(store.PrepareReference(second,next)&&!store.CommitReference(second,oldHash),"a prepared save-as cannot commit against unverified native bytes");
    Check(store.ReadReference(second,newHash,loaded)==O::StoreCode::Recovered&&loaded.campaign==id,"matching staged reference survives an interrupted native-save commit");
    Check(store.CommitReference(second,newHash)&&store.ReadReference(second,newHash,loaded)==O::StoreCode::Found,"verified save-as inherits the existing campaign");
    Check(store.ReadCampaign(loaded.campaign,decoded)==O::StoreCode::Found&&decoded.seed.seed==campaign.seed.seed,"save-as cannot force a reroll");
    Check(store.PrepareReference(save,next)&&!store.AbortReference(save,reference)&&store.AbortReference(save,next),"abort removes only its own exact pending record");
    Check(store.ReadReference(save,oldHash,loaded)==O::StoreCode::Found,"aborted write preserves the earlier native-save association");
    Check(store.PrepareReference(save,next)&&store.CommitReference(save,newHash)&&store.ReadReference(save,oldHash,loaded)==O::StoreCode::Recovered,"previous reference remains recoverable for the previous exact save hash");
    O::Hash foreign{};foreign[31]=9;Check(store.ReadReference(save,foreign,loaded)==O::StoreCode::Foreign,"a foreign save cannot silently inherit or replace a campaign");
    Write(store.CampaignPath(id),"corrupt");Check(store.ReadCampaign(id,decoded)==O::StoreCode::Invalid&&!store.SaveCampaign(campaign),"corrupt campaign is retained, never replaced with a fresh reroll");
    std::ifstream input(save,std::ios::binary);std::string text((std::istreambuf_iterator<char>(input)),{});
    Check(text=="native save bytes remain owned by the game","all metadata writes leave the native save untouched");
    std::printf("OmegaT7StoreRt1: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
