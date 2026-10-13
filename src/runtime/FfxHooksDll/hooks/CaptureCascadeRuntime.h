#pragma once
#include "CaptureCascadeCore.h"
#include "ArcanaFieldPresentation.h"
#include "SinNaturalCore.h"
#include "SinRamScalingCore.h"
#include "CaptureCascadeBridge.h"
#include <cstdint>

namespace FfxHooks::CaptureCascade::Runtime {
enum class Code {Off,Waiting,Ready,Conflict,StorageError,Unsupported,Stopped};
void Prime(std::uintptr_t base,bool requested,bool validateOnly,void(*log)(const char*),bool awaken=true) noexcept;
void Stop() noexcept;
bool Requested() noexcept;
bool TutorialsRequested() noexcept;
bool Ready() noexcept;
Code Status() noexcept;
const char* Detail() noexcept;
bool Snapshot(State&,std::uint64_t&) noexcept;
bool CaptureNotice(std::uint16_t room,bool stable,Arcana::Presentation::View&,std::uint64_t&) noexcept;
Arcana::Error AcknowledgeNotice(std::uint64_t,const Arcana::Presentation::View&,Arcana::Presentation::Navigation) noexcept;
bool FormatNotice(const Arcana::Presentation::View&,char*,std::size_t) noexcept;
bool EncodeNotice(const char*,unsigned char*,std::size_t,std::size_t&) noexcept;
Arcana::FieldPresentation::SupplementalSource NoticeSource() noexcept;
bool Awakened(unsigned field) noexcept;
SinRam::Config EffectiveSin(unsigned field,const SinRam::Config&) noexcept;
bool ObserveNatural(const SinNatural::Evidence&,std::uint32_t,Bridge::CurrentRequest) noexcept;
bool ConsumeNatural(SinNatural::Evidence&,std::uint32_t& entropy) noexcept;
bool RecordCapture(unsigned model) noexcept;
}
