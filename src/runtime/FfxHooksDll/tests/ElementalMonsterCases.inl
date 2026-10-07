// Jarvis-HOOK: monster affinity admission is tied to the loaded native file.
static std::string MonsterPack(const std::vector<unsigned char>& bank,const std::vector<unsigned char>& file){
    std::string text=CorePack(bank);
    const auto marker=text.find("],\"equipment\"");
    const auto profile=",{\"key\":\"monster.fixture\",\"kind\":\"monster\",\"id\":4438,\"file_bytes\":"+
        std::to_string(file.size())+",\"file_sha256\":\""+Hash(file.data(),file.size())+
        "\",\"imperil_limit\":2,\"affinities\":[{\"key\":\"tests.e8\",\"base_bp\":20000},"
        "{\"key\":\"tests.e9\",\"base_bp\":-10000,\"locked\":true}]}";
    text.insert(marker,profile);return text;
}
static void MonsterCases(std::uintptr_t base,std::vector<unsigned char>& actors,
                         std::vector<unsigned char>& bank,std::vector<unsigned char>& file){
    coreImage=base;amount=1000;W::DamageProducerForTests(reinterpret_cast<void*>(&CoreEndpoint));
    const auto producer=reinterpret_cast<FfxHooks::SharedDamage::DamageFn>(base+(::FfxHooks::ExecutableProfile::Rva<0x38E680>()));
    std::array<unsigned char,44> info{};
    auto* target=actors.data()+18*0xF90;
    const auto hit=[&](unsigned id,unsigned slot=18){return static_cast<int>(producer(0,actors.data(),slot,
        actors.data()+slot*0xF90,bank.data()+20+96*id,0x3000+id,info.data(),0,0,0,0));};
    Check(hit(88)==2000,"a proved monster profile reaches the actual affinity consumer");
    Check(hit(89)==-1000,"a locked monster absorption value preserves signed native damage");
    Check(hit(88,19)==1000,"an unlisted monster never inherits a profile from another enemy");
    E::ElementalView view{};
    Check(E::ReadElement(18,9,view)&&view.locked&&view.baseBp==-10000,"the numerical reader exposes the monster's actual locked baseline");
    file[0x60]^=1;
    Check(hit(88)==1000&&!E::ReadElement(18,8,view),"changed native stat data closes its profile rather than displaying stale values");
    file[0x60]^=1;
    Check(hit(88)==2000,"restoring the exact admitted immutable bytes restores the valid profile");
    W16(target+0xE,342);
    Check(hit(88)==1000,"species-like aliases do not bypass the explicit raw native identity");W16(target+0xE,4438);
    auto replacement=file;const auto pointer=reinterpret_cast<std::uintptr_t>(replacement.data());
    std::memcpy(target+0x48,&pointer,4);
    Check(hit(88)==1000,"a same-byte replacement allocation is unavailable until admission runs");
    E::TickMainThread();Check(hit(88)==2000,"fresh main-thread admission binds the replacement allocation");
    replacement[0x30]^=1;B::Reset(B::ResetReason::NativeLoad);E::TickMainThread();
    Check(hit(88)==1000&&!E::ReadElement(18,8,view),"a wrong full-file fingerprint is rejected even when the stat prefix matches");
    E::RequestStop();Check(hit(88)==1000,"stopped monster consumers use native behavior");
}
