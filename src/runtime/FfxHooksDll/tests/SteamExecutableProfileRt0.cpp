// Jarvis-HOOK: independently pinned new PE bytes exercise the selected profile and address ledger.
#include "../shared/ExecutableProfile.h"
#include "../hooks/F8RuntimeCore.h"
#include "../shared/ffx_addresses.h"
#include "../hooks/SeymourExitEvidence.h"
#include "../hooks/F7DifficultyCore.h"
#include "../hooks/BootSkipHook.h"
#include "../hooks/RonsoPoolEvidence.h"
#include "../hooks/RecoveryEvidence.generated.h"
#include "../hooks/SeymourOverdriveEvidence.generated.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* text) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", text); }
}
std::uint32_t Word(const std::vector<unsigned char>& image, std::size_t at) {
    std::uint32_t value = 0;
    if (at + sizeof(value) <= image.size()) std::memcpy(&value, image.data() + at, sizeof(value));
    return value;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)), {});
    if (image.size() != 0x237d000) return 3;
    // These literals come from the inspected current PE, not from the parser or generated map.
    Check(Word(image, Word(image, 0x3c) + 8) == 0x6aa2219c, "fixture is the new Steam timestamp");
    FfxHooks::F8Runtime::ExecutableIdentity identity{};
    Check(FfxHooks::F8Runtime::ParseExecutableIdentity(image.data(), image.size(), &identity) ==
        FfxHooks::F8Runtime::ProfileResult::Supported, "selected Steam profile recognizes actual new PE header");
    Check(RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX == 0x394020, "actor accessor resolves to the inspected new entry");
    Check(RVA_FFX_BATTLE_COMPUTE_HIT_DAMAGE == 0x38e630, "complete damage-frame correspondence selects the new entry");
    Check(RVA_FFX_GRANT_COMMAND_TO_CHARACTER == 0x385c50, "command grant resolves to its complete-body new entry");
    Check(RVA_FFX_BATTLE_PLAYER_LIST == 0xd334d4, "actor table pointer uses the new HIGHLOW target");
    const unsigned char actorPrefix[]{0x55,0x8b,0xec,0x8b,0x45,0x08,0x25,0xff,0x00,0x00,0x00,0x83,0xf8,0x1f};
    Check(std::memcmp(image.data() + RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX, actorPrefix, sizeof(actorPrefix)) == 0,
        "selected actor entry points at its instruction-aligned prefix");
    Check(Word(image, RVA_FFX_BATTLE_GET_ACTOR_BY_INDEX + 24) == 0x400000 + RVA_FFX_BATTLE_PLAYER_LIST,
        "accessor operand and selected 32-bit global agree in the actual PE");
    constexpr auto exit = FfxHooks::ExecutableProfile::Rva<0x386080>();
    Check(FfxHooks::SeymourBattle::ExitEvidence::ActorTableOperandMatches(image.data() + exit, 25, 0x400000),
        "Seymour exit validator accepts the actual native party table operand");
    constexpr auto populate = FfxHooks::ExecutableProfile::Rva<0x384010>();
    Check(std::memcmp(image.data() + populate, FfxHooks::F7Difficulty::kActorPopulatePreferredBody.data(),
        FfxHooks::F7Difficulty::kActorPopulatePreferredBody.size()) == 0,
        "difficulty population witness matches the complete actual new native loop");
    Check(RVA_FFX_SEYMOUR_PATCH_PAGE == (RVA_FFX_SEYMOUR_PATCH_SITE1 & ~0xFFFu) &&
        (RVA_FFX_SEYMOUR_PATCH_SITE2 & ~0xFFFu) == RVA_FFX_SEYMOUR_PATCH_PAGE,
        "Seymour protection page is aligned and contains both actual patch sites");
    for(const auto& span:FfxHooks::Fastload::kCodeSpans){
        const auto* actual=image.data()+span.rva;
        const auto status=FfxHooks::Fastload::ValidateTarget(span.target,actual,span.size,0x400000);
        if(status!=FfxHooks::Fastload::TargetStatus::Match)
            std::printf("FASTLOAD_WITNESS target=%u rva=%08X result=%u\n",
                static_cast<unsigned>(span.target),static_cast<unsigned>(span.rva),static_cast<unsigned>(status));
        Check(status==FfxHooks::Fastload::TargetStatus::Match,"Fastload witness accepts the actual current native instructions");
        auto changed=std::vector<unsigned char>(actual,actual+span.size);
        for(std::size_t offset=0;offset<changed.size();++offset){
            changed[offset]^=1u;
            Check(FfxHooks::Fastload::ValidateTarget(span.target,changed.data(),changed.size(),0x400000)!=
                FfxHooks::Fastload::TargetStatus::Match,"Every changed native Fastload witness byte is rejected");
            changed[offset]^=1u;
        }
    }
    for(const auto& span:FfxHooks::RonsoPool::Evidence::kSpans){
        const auto* actual=image.data()+span.rva;
        const bool match=FfxHooks::RonsoPool::Evidence::Matches(span,actual,span.size,0x400000);
        if(!match)std::printf("RONSO_WITNESS rva=%08X length=%u\n",span.rva,static_cast<unsigned>(span.size));
        Check(match,"Ronso witness accepts the actual current native body or caller");
        auto changed=std::vector<unsigned char>(actual,actual+span.size);
        for(std::size_t offset=0;offset<changed.size();++offset){
            changed[offset]^=1u;
            Check(!FfxHooks::RonsoPool::Evidence::Matches(span,changed.data(),changed.size(),0x400000),
                "Every changed native Ronso witness byte is rejected");
            changed[offset]^=1u;
        }
    }
    const FfxHooks::RecoveryEvidence::Proof* recovery[]={
        &FfxHooks::RecoveryEvidence::WardWriteback,
        &FfxHooks::RecoveryEvidence::WardFrame,
        &FfxHooks::RecoveryEvidence::WardLoop,
        &FfxHooks::RecoveryEvidence::WardAftermath,
        &FfxHooks::RecoveryEvidence::WardHitLoop,
        &FfxHooks::RecoveryEvidence::WardActor,
        &FfxHooks::RecoveryEvidence::GridGrant,
        &FfxHooks::RecoveryEvidence::GridBuildMenu,
        &FfxHooks::RecoveryEvidence::GridHasCommand,
        &FfxHooks::RecoveryEvidence::GridCommandEntry,
        &FfxHooks::RecoveryEvidence::GridPrepare,
        &FfxHooks::RecoveryEvidence::LoadCallA,
        &FfxHooks::RecoveryEvidence::LoadCallB,
        &FfxHooks::RecoveryEvidence::LoadCallC,
        &FfxHooks::RecoveryEvidence::GridLoad,
        &FfxHooks::RecoveryEvidence::GridRenderConstructor,
        &FfxHooks::RecoveryEvidence::PhotoPosition,
        &FfxHooks::RecoveryEvidence::PhotoUpdate,
    };
    for(const auto* proof:recovery)Check(std::memcmp(image.data()+proof->rva,proof->bytes,proof->size)==0,
        "Recovery witness matches the actual current native code");
    for(const auto& proof:FfxHooks::SeymourOverdrive::Functions){
        Check(std::memcmp(image.data()+proof.rva,proof.bytes,proof.size)==0,
            "Seymour Overdrive full body matches the actual current native code");
        Check(image[proof.rva+proof.boundOffset]==proof.oldBound,
            "Seymour Overdrive extends the actual native loop bound");
        for(unsigned i=0;i<proof.callCount;++i){
            const auto& call=proof.calls[i];const auto at=proof.rva+call.offset;
            const auto destination=static_cast<std::uint32_t>(at+5+static_cast<std::int32_t>(Word(image,at+1)));
            Check(image[at]==0xE8&&destination==call.targetRva,
                "Seymour Overdrive call target agrees with the actual native E8 edge");
        }
    }
    std::printf("STEAM PROFILE RT0: %u/%u passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
