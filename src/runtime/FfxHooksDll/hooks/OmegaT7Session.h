#pragma once
#include "OmegaT7Store.h"
#include <filesystem>
namespace FfxHooks::OmegaT7::Session {
using Log=void(*)(const char*);
void Prime(std::uintptr_t,const std::filesystem::path&,bool requested,bool validateOnly,Log) noexcept;
void Stop() noexcept;
bool Ready() noexcept;
bool Prepared() noexcept;
void Tick() noexcept;
std::uint64_t BattleSeed(std::uint64_t battleGeneration) noexcept;
bool Snapshot(Campaign&,std::uint64_t& session) noexcept;
bool SetLoreSeen(std::uint64_t session) noexcept;
bool ClearRenewal(std::uint64_t session,std::uint64_t epoch) noexcept;
void Field(bool stable) noexcept;
const char* Detail() noexcept;
}
