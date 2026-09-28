#pragma once
#include <cstdint>
#include <string_view>

namespace FfxHooks::ElementalDominion {
using RuntimeLog=void(*)(const char*);
struct RuntimeOptions {bool core=false,tactics=false,gravity=false,magicBdl=false;};
enum class RuntimeCode : unsigned {
    Disabled,ValidationOnly,WaitingForData,Ready,Unsupported,PackMissing,
    PackInvalid,DataMismatch,Conflict,Stopped
};
struct RuntimeStatus {
    RuntimeCode code=RuntimeCode::Disabled;
    unsigned capabilities=0,bindings=0,ownerThread=0;
    std::uint64_t generation=0;
};
struct ElementalView {
    const char* key=nullptr;
    const char* label=nullptr;
    unsigned rgb=0,imperil=0,ward=0,nul=0;
    unsigned imperilTurns=0,wardTurns=0,nulTurns=0,imperilResistanceBp=0;
    std::int32_t baseBp=10000,effectiveBp=10000,equipmentBp=0;
    bool locked=false,imperilImmune=false;
};
unsigned DescriptorCount() noexcept;
bool ReadElement(unsigned actor,unsigned element,ElementalView&) noexcept;
// Prepare is worker-only; Activate follows shared producer/clamp startup.
// Tick belongs to the native main-thread pump. None is called from DllMain.
bool Prepare(std::uintptr_t image,bool validateOnly,RuntimeLog log);
bool PrepareText(std::uintptr_t image,RuntimeOptions,std::string_view manifest,
                 bool validateOnly,RuntimeLog log);
bool Activate();
void TickMainThread() noexcept;
// Loader-lock fallback: close admission only; normal teardown owns unsubscribe.
void RequestDetachStop() noexcept;
void RequestStop() noexcept;
RuntimeStatus RuntimeState() noexcept;
const char* RuntimeDetail() noexcept;
} // namespace FfxHooks::ElementalDominion
