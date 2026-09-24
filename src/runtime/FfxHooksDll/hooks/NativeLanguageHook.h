#pragma once
#include "NativeLanguageCore.h"
namespace FfxHooks::NativeLanguage {
enum class Result {Default=0,Applied,Unsupported,Conflict,ThreadUnavailable,ProtectFailed,RestorePending};
bool Start(std::uintptr_t base,Settings settings);
void RequestStop();
void Stop();
Result Status();
Settings Applied();
const char* StatusText();
}
