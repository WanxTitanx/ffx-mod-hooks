#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: complete-file validation occurs on the owner pump. Hits read
// bounded header/stat copies and never allocate or hash a monster file.
struct MonsterProfileSlot {
    std::uint64_t generation=0;
    std::uintptr_t actor=0;
    std::uint32_t file=0;
    unsigned identity=0xFFFF,profile=UINT_MAX;
    MonsterProof proof;
};
std::array<MonsterProfileSlot,ActorCount> monsterProfiles{};
const ActorProfile* ProfileForActor(unsigned slot,const void* actor,bool* rejected=nullptr) noexcept {
    if(rejected)*rejected=false;
    if(slot<18)return CharacterProfile(slot,actor);
    if(slot>=ActorCount||!Actor(slot,actor))return nullptr;
    const auto address=reinterpret_cast<std::uintptr_t>(actor);
    std::uint16_t identity=0xFFFF;
    if(!Read(address+0xE,identity))return nullptr;
    bool expected=false;
    for(const auto& profile:pack.profiles)
        if(profile.kind==ProfileKind::Monster&&profile.id==identity){expected=true;break;}
    if(!expected)return nullptr;
    if(rejected)*rejected=true;
    const auto& entry=monsterProfiles[slot];std::uint32_t file=0;
    if(entry.generation!=generation.load()||entry.actor!=address||entry.identity!=identity||
       entry.profile>=pack.profiles.size()||!Read(address+0x48,file)||file!=entry.file)return nullptr;
    std::array<Byte,MonsterProof::HeaderBytes> header{};
    std::array<Byte,MonsterProof::StatCapacity> stats{};
    if(!Copy(header.data(),reinterpret_cast<const void*>(std::uintptr_t(file)),header.size())||
       !Copy(stats.data(),reinterpret_cast<const void*>(std::uintptr_t(file)+entry.proof.StatOffset()),entry.proof.StatBytes())||
       !entry.proof.MatchesView(slot,identity,reinterpret_cast<const void*>(std::uintptr_t(file)),
                               header.data(),header.size(),stats.data(),entry.proof.StatBytes()))return nullptr;
    if(rejected)*rejected=false;return &pack.profiles[entry.profile];
}
void RefreshMonsterProfiles() noexcept {
    if(!options.core&&!options.tactics&&!options.gravity)return;
    std::uint32_t actors=0;
    if(!Read(module + (::FfxHooks::ExecutableProfile::Rva<0xD334CC>()),actors)||actors<0x10000||actors>UINT32_MAX-ActorCount*0xF90u)return;
    const auto epoch=generation.load();
    for(unsigned slot=18;slot<ActorCount;++slot){
        const auto address=std::uintptr_t(actors)+slot*0xF90u;
        std::uint16_t index=0xFFFF,identity=0xFFFF;std::uint32_t file=0;Byte exists=0;
        auto& entry=monsterProfiles[slot];
        if(SharedActor::Busy(slot)){entry={};continue;}
        if(!Read(address+0xC,index)||index!=slot||!Read(address+0xE,identity)||identity==0xFFFF||
           !Read(address+0x48,file)||!Read(address+0xDC8,exists)||!exists){entry={};continue;}
        if(entry.generation==epoch&&entry.actor==address&&entry.file==file&&entry.identity==identity)continue;
        entry={};entry.generation=epoch;entry.actor=address;entry.file=file;entry.identity=identity;
        bool expected=false;
        for(const auto& profile:pack.profiles)
            if(profile.kind==ProfileKind::Monster&&profile.id==identity){expected=true;break;}
        if(!expected)continue;
        std::uint32_t size=0;
        if(file<0x10000||file>UINT32_MAX-MonsterProof::HeaderBytes||!Read(file+0x20u,size)||
           size<MonsterProof::HeaderBytes||size>8u*1024u*1024u||file>UINT32_MAX-size)continue;
        try {
            std::vector<Byte> bytes(size);
            if(Copy(bytes.data(),reinterpret_cast<const void*>(std::uintptr_t(file)),size)){
                for(unsigned i=0;i<pack.profiles.size();++i){
                    const auto& profile=pack.profiles[i];
                    if(profile.kind!=ProfileKind::Monster||profile.id!=identity||profile.fileBytes!=size)continue;
                    MonsterProof candidate;MonsterProofError error{};
                    if(candidate.Admit(profile,slot,identity,reinterpret_cast<const void*>(std::uintptr_t(file)),
                                       bytes.data(),bytes.size(),Digest,error)&&generation.load()==epoch){
                        entry.proof=candidate;entry.profile=i;break;
                    }
                }
            }
        }catch(...){entry.profile=UINT_MAX;}
        if(entry.profile==UINT_MAX&&logger)
            logger("[ffx-hooks] Elemental monster profile inactive: native file fingerprint mismatch\n");
        // One whole-file candidate per pump bounds startup work. Other selected
        // actors remain unavailable until their own proof is admitted.
        return;
    }
}
