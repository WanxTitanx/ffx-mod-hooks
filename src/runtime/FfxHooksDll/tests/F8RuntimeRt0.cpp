#include "../shared/Config.h"
#include "../shared/ffx_addresses.h"
#include "../hooks/F8FlagCatalog.h"
#include "../hooks/F8RuntimeCore.h"
#include "../hooks/F7UnsafePrototypePolicy.h"
#include "../hooks/ResolverOwnerPolicy.h"
#include "../hooks/DialogSkipHook.h"
#include "../hooks/MaechenCore.h"
#include "../hooks/MaechenHook.h"
#include "../hooks/InGameMenuDashboard.h"
#include "../hooks/SpeedHackHook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

// Match the production include environment so Win32 macro collisions remain compile failures.
#include "../hooks/F8FlagsUiState.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Keep the address-ledger RED executable even before Task 6 adds the named production
// constants. A missing production definition resolves to an impossible sentinel and fails.
#ifndef FFX_PREFERRED_IMAGE_BASE
#define FFX_PREFERRED_IMAGE_BASE 0u
#endif
#ifndef FFX_DEBUG_STRUCT_SIZE
#define FFX_DEBUG_STRUCT_SIZE 0u
#endif
#ifndef FFX_DEBUG_INVINCIBLE_ENEMIES_OFFSET
#define FFX_DEBUG_INVINCIBLE_ENEMIES_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_INVINCIBLE_PARTY_OFFSET
#define FFX_DEBUG_INVINCIBLE_PARTY_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_OVERDRIVE_OFFSET
#define FFX_DEBUG_ALWAYS_OVERDRIVE_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_CRITICAL_OFFSET
#define FFX_DEBUG_ALWAYS_CRITICAL_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_DEAL_1_OFFSET
#define FFX_DEBUG_ALWAYS_DEAL_1_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_DEAL_10000_OFFSET
#define FFX_DEBUG_ALWAYS_DEAL_10000_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_DEAL_99999_OFFSET
#define FFX_DEBUG_ALWAYS_DEAL_99999_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_ALWAYS_RARE_REWARD_OFFSET
#define FFX_DEBUG_ALWAYS_RARE_REWARD_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_AP_100X_OFFSET
#define FFX_DEBUG_AP_100X_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_GIL_100X_OFFSET
#define FFX_DEBUG_GIL_100X_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_DEBUG_PERMANENT_SENSOR_OFFSET
#define FFX_DEBUG_PERMANENT_SENSOR_OFFSET 0xFFFFFFFFu
#endif
#ifndef RVA_FFX_AP_MULTIPLIER_SIGNATURE
#define RVA_FFX_AP_MULTIPLIER_SIGNATURE 0u
#endif
#ifndef RVA_FFX_AP_MULTIPLIER_SITE
#define RVA_FFX_AP_MULTIPLIER_SITE 0u
#endif
#ifndef RVA_FFX_AP_MULTIPLIER_IMMEDIATE
#define RVA_FFX_AP_MULTIPLIER_IMMEDIATE 0u
#endif
#ifndef RVA_FFX_AP_MULTIPLIER_RESUME
#define RVA_FFX_AP_MULTIPLIER_RESUME 0u
#endif
#ifndef RVA_FFX_GIL_MULTIPLIER_SIGNATURE
#define RVA_FFX_GIL_MULTIPLIER_SIGNATURE 0u
#endif
#ifndef RVA_FFX_GIL_MULTIPLIER_SITE
#define RVA_FFX_GIL_MULTIPLIER_SITE 0u
#endif
#ifndef RVA_FFX_GIL_MULTIPLIER_IMMEDIATE
#define RVA_FFX_GIL_MULTIPLIER_IMMEDIATE 0u
#endif
#ifndef RVA_FFX_GIL_MULTIPLIER_RESUME
#define RVA_FFX_GIL_MULTIPLIER_RESUME 0u
#endif
#ifndef RVA_FFX_PARTY_STRUCT_BASE
#define RVA_FFX_PARTY_STRUCT_BASE 0u
#endif
#ifndef RVA_FFX_PARTY_IN_PARTY_BASE
#define RVA_FFX_PARTY_IN_PARTY_BASE 0u
#endif
#ifndef FFX_PARTY_IN_PARTY_OFFSET
#define FFX_PARTY_IN_PARTY_OFFSET 0xFFFFFFFFu
#endif
#ifndef FFX_PARTY_SLOT_COUNT
#define FFX_PARTY_SLOT_COUNT 0u
#endif
#ifndef RVA_FFX_SEYMOUR_PATCH_SITE1
#define RVA_FFX_SEYMOUR_PATCH_SITE1 0u
#endif
#ifndef RVA_FFX_SEYMOUR_PATCH_SITE2
#define RVA_FFX_SEYMOUR_PATCH_SITE2 0u
#endif
#ifndef RVA_FFX_SEYMOUR_PATCH_PAGE
#define RVA_FFX_SEYMOUR_PATCH_PAGE 0u
#endif
#ifndef FFX_SEYMOUR_PATCH_SPAN_LENGTH
#define FFX_SEYMOUR_PATCH_SPAN_LENGTH 0u
#endif
#ifndef FFX_PARTY_SEYMOUR_MASK
#define FFX_PARTY_SEYMOUR_MASK 0u
#endif

using FfxHooks::Config::BoolGateResult;
using FfxHooks::Config::BoolGateSpec;
using FfxHooks::Config::BoolSource;
using FfxHooks::Config::GetBool;
using FfxHooks::Config::GetFloat;
using FfxHooks::Config::GetInt;
using FfxHooks::Config::GetString;
using FfxHooks::Config::IntReadState;
using FfxHooks::Config::Load;
using FfxHooks::Config::LoadTextForTests;
using FfxHooks::Config::ReadIntExact;
using FfxHooks::Config::ResetForTests;
using FfxHooks::Config::ResolveBoolGate;
using FfxHooks::Config::SetAuthoritativeBool;
using FfxHooks::Config::SetBool;
using FfxHooks::Config::SetInt;
using FfxHooks::Config::SetProvidersForTests;
using FfxHooks::Config::SetString;
using FfxHooks::Config::TestProviders;
using FfxHooks::Config::TryGetBoolExact;
using FfxHooks::F8Activation;
using FfxHooks::F8ApplyMode;
using FfxHooks::F8EditCode;
using FfxHooks::F8RuntimeAvailability;
using FfxHooks::F8ScalarEditCode;
using FfxHooks::F8ScalarState;

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

struct FakeState {
    std::mutex mutex;
    std::condition_variable condition;
    std::unordered_map<std::string, bool> environment;
    std::unordered_map<std::string, BoolSource> flags;
    std::string persistedPath;
    std::string persistedText;
    std::vector<std::string> persistedGenerations;
    bool persistSucceeds = true;
    bool archiveSucceeds = true;
    std::unordered_map<std::string,BoolSource> archivedMusicFlags;
    int persistCalls = 0;
    const char* inspectKey = nullptr;
    bool observedDuringPersist = false;
    bool blockFirstPersist = false;
    bool firstPersistEntered = false;
    bool releaseFirstPersist = false;
    bool secondPersistEntered = false;
};

struct PublicationProgress {
    std::mutex mutex;
    std::condition_variable condition;
    bool ready = false;
    bool start = false;
    bool aboutToPublish = false;
    bool returned = false;
    bool result = false;
};

template <typename Predicate>
bool WaitForCondition(std::condition_variable& condition, std::unique_lock<std::mutex>& lock,
                      Predicate predicate) {
    return condition.wait_for(lock, std::chrono::seconds(5), predicate);
}

template <typename Predicate>
bool WaitForConditionMillis(std::condition_variable& condition,
                            std::unique_lock<std::mutex>& lock,
                            int milliseconds,
                            Predicate predicate) {
    return condition.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate);
}

void PublishAfterBarrier(PublicationProgress& progress, const char* canonicalKey,
                         F8RuntimeAvailability availability, bool hasAppliedValue,
                         bool appliedValue) {
    {
        std::unique_lock<std::mutex> lock(progress.mutex);
        progress.ready = true;
        progress.condition.notify_all();
        progress.condition.wait(lock, [&]() { return progress.start; });
        progress.aboutToPublish = true;
        progress.condition.notify_all();
    }

    const bool result = FfxHooks::PublishF8RuntimeStatus(
        canonicalKey, availability, hasAppliedValue, appliedValue);
    {
        std::lock_guard<std::mutex> lock(progress.mutex);
        progress.result = result;
        progress.returned = true;
        progress.condition.notify_all();
    }
}

void ArmPublication(PublicationProgress& progress) {
    std::unique_lock<std::mutex> lock(progress.mutex);
    Expect(WaitForCondition(progress.condition, lock, [&]() { return progress.ready; }),
           "publisher must reach its explicit ready barrier");
    progress.start = true;
    progress.condition.notify_all();
    Expect(WaitForCondition(progress.condition, lock, [&]() { return progress.aboutToPublish; }),
           "publisher must reach the call boundary");
}

bool FakeTryEnvBool(void* context, const char* name, bool* valueOut) {
    if (!context || !name || !valueOut) return false;
    FakeState& state = *static_cast<FakeState*>(context);
    std::lock_guard<std::mutex> guard(state.mutex);
    const auto found = state.environment.find(name);
    if (found == state.environment.end()) return false;
    *valueOut = found->second;
    return true;
}

bool FakeFlagExists(void* context, const char* name, BoolSource* sourceOut) {
    if (!context || !name || !sourceOut) return false;
    FakeState& state = *static_cast<FakeState*>(context);
    std::lock_guard<std::mutex> guard(state.mutex);
    const auto found = state.flags.find(name);
    if (found == state.flags.end()) return false;
    *sourceOut = found->second;
    return true;
}

bool FakePersistText(void* context, const char* path, const char* text) {
    if (!context || !path || !text) return false;
    FakeState& state = *static_cast<FakeState*>(context);
    const bool observed = state.inspectKey ? GetBool(state.inspectKey, false) : false;

    std::unique_lock<std::mutex> lock(state.mutex);
    const int call = ++state.persistCalls;
    state.persistedPath = path;
    state.persistedText = text;
    state.persistedGenerations.emplace_back(text);
    if (state.inspectKey) state.observedDuringPersist = observed;
    if (call == 1 && state.blockFirstPersist) {
        state.firstPersistEntered = true;
        state.condition.notify_all();
        state.condition.wait(lock, [&]() { return state.releaseFirstPersist; });
    } else if (call == 2) {
        state.secondPersistEntered = true;
        state.condition.notify_all();
    }
    return state.persistSucceeds;
}

bool FakeArchiveArenaMusicOff(void* context, bool restore) {
    auto& state=*static_cast<FakeState*>(context);
    if(restore){for(const auto& flag:state.archivedMusicFlags)state.flags.emplace(flag);state.archivedMusicFlags.clear();return true;}
    if(!state.archiveSucceeds)return false;
    const auto it=state.flags.find("arena_plus_music.flag.off");
    if(it!=state.flags.end()){state.archivedMusicFlags.emplace(*it);state.flags.erase(it);}
    return true;
}
void SetTestProviders(FakeState& state, bool fakePersistence) {
    const TestProviders providers = {
        &state,
        &FakeTryEnvBool,
        &FakeFlagExists,
        fakePersistence ? &FakePersistText : nullptr,
        &FakeArchiveArenaMusicOff,
    };
    SetProvidersForTests(providers);
}

void Configure(FakeState& state, const char* iniText) {
    ResetForTests();
    SetTestProviders(state, true);
    Expect(LoadTextForTests(iniText, "C:\\rt0\\_isolated\\ffx-hooks.ini"),
           "controlled INI text must load");
    FfxHooks::PublishF8RuntimeStatus("arena_plus.unlock_all",
        FfxHooks::F8RuntimeAvailability::Available, true,
        FfxHooks::ResolveF8Flag(*FfxHooks::FindF8Flag("arena_plus.unlock_all")).value);
}

std::string RealRt0IniPath() {
    char executable[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, executable, MAX_PATH) == 0) return {};
    char* slash = strrchr(executable, '\\');
    if (!slash) return {};
    *slash = '\0';
    const std::string directory = std::string(executable) + "\\_isolated";
    if (CreateDirectoryA(directory.c_str(), nullptr) == FALSE &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return directory + "\\ffx-hooks.ini";
}

bool DeleteOwnRt0Ini(const std::string& path) {
    if (path.empty()) return false;
    if (DeleteFileA(path.c_str()) != FALSE) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool WriteWholeFile(const std::string& path, const char* data, DWORD size) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) != FALSE && written == size;
    const bool closed = CloseHandle(file) != FALSE;
    return ok && closed;
}

bool ReadWholeFile(const std::string& path, std::string& textOut) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 && size.QuadPart <= 1024 * 1024;
    std::string text;
    if (ok) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        const DWORD expected = static_cast<DWORD>(text.size());
        ok = ReadFile(file, text.data(), expected, &read, nullptr) != FALSE && read == expected;
    }
    if (CloseHandle(file) == FALSE) ok = false;
    // Source contracts use hand-authored LF signatures; normalize checkout bytes at the reader
    // boundary so Git's CRLF policy cannot change the parsed C++ structure.
    if (ok) {
        std::string normalized;
        normalized.reserve(text.size());
        for (size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                continue;
            }
            normalized.push_back(text[index]);
        }
        text = std::move(normalized);
    }
    if (ok) textOut = std::move(text);
    return ok;
}

std::string TrackedIniPath() {
    std::string path = __FILE__;
    const size_t fileSlash = path.find_last_of("\\/");
    if (fileSlash == std::string::npos) return {};
    path.resize(fileSlash);
    const size_t testsSlash = path.find_last_of("\\/");
    if (testsSlash == std::string::npos) return {};
    path.resize(testsSlash);
    return path + "\\ffx-hooks.ini";
}

std::string RuntimeSourcePath(const char* relativePath) {
    if (!relativePath || !relativePath[0]) return {};
    std::string path = __FILE__;
    const size_t fileSlash = path.find_last_of("\\/");
    if (fileSlash == std::string::npos) return {};
    path.resize(fileSlash);
    const size_t testsSlash = path.find_last_of("\\/");
    if (testsSlash == std::string::npos) return {};
    path.resize(testsSlash);
    return path + "\\" + relativePath;
}

void ExpectSourceExcludes(const std::string& source, const char* token, const char* message) {
    Expect(source.find(token) == std::string::npos, message);
}

void ExpectSourceIncludes(const std::string& source, const char* token, const char* message) {
    Expect(source.find(token) != std::string::npos, message);
}

std::string SourceCodeOnly(const std::string& source);

std::string BoundedSourceSection(const std::string& source, const char* beginToken,
                                 const char* endToken) {
    const std::string code = SourceCodeOnly(source);
    const size_t begin = code.find(beginToken);
    if (begin == std::string::npos) return {};
    const size_t end = code.find(endToken, begin + strlen(beginToken));
    if (end == std::string::npos) return {};
    return source.substr(begin, end - begin);
}

struct SourceBlock {
    size_t open = std::string::npos;
    size_t close = std::string::npos;
    std::string body;

    bool Valid() const { return open != std::string::npos && close != std::string::npos; }
};

size_t FindMatchingSourceBrace(const std::string& source, size_t open) {
    if (open >= source.size() || source[open] != '{') return std::string::npos;
    enum class LexState { Normal, LineComment, BlockComment, String, Character };
    LexState state = LexState::Normal;
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (state == LexState::LineComment) {
            if (c == '\n') state = LexState::Normal;
            continue;
        }
        if (state == LexState::BlockComment) {
            if (c == '*' && next == '/') {
                state = LexState::Normal;
                ++i;
            }
            continue;
        }
        if (state == LexState::String || state == LexState::Character) {
            if (c == '\\') {
                ++i;
            } else if ((state == LexState::String && c == '"') ||
                       (state == LexState::Character && c == '\'')) {
                state = LexState::Normal;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            state = LexState::LineComment;
            ++i;
        } else if (c == '/' && next == '*') {
            state = LexState::BlockComment;
            ++i;
        } else if (c == '"') {
            state = LexState::String;
        } else if (c == '\'') {
            state = LexState::Character;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}' && --depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

SourceBlock SourceBlockAfterToken(const std::string& source, const char* token,
                                  size_t searchFrom = 0) {
    SourceBlock result;
    if (!token) return result;
    const size_t tokenPosition = source.find(token, searchFrom);
    if (tokenPosition == std::string::npos) return result;
    result.open = source.find('{', tokenPosition + strlen(token));
    if (result.open == std::string::npos) return SourceBlock{};
    result.close = FindMatchingSourceBrace(source, result.open);
    if (result.close == std::string::npos) return SourceBlock{};
    result.body = source.substr(result.open + 1, result.close - result.open - 1);
    return result;
}

SourceBlock SourceFunctionBody(const std::string& source, const char* signature) {
    SourceBlock result;
    if (!signature) return result;
    size_t searchFrom = 0;
    while (true) {
        const size_t signaturePosition = source.find(signature, searchFrom);
        if (signaturePosition == std::string::npos) return SourceBlock{};
        size_t cursor = signaturePosition + strlen(signature);
        while (cursor < source.size() && isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
        if (cursor < source.size() && source[cursor] == '{') {
            result.open = cursor;
            result.close = FindMatchingSourceBrace(source, cursor);
            if (result.close == std::string::npos) return SourceBlock{};
            result.body = source.substr(cursor + 1, result.close - cursor - 1);
            return result;
        }
        searchFrom = signaturePosition + strlen(signature);
    }
}

size_t CountSourceToken(const std::string& source, const char* token) {
    if (!token || !token[0]) return 0;
    size_t count = 0;
    size_t cursor = 0;
    while ((cursor = source.find(token, cursor)) != std::string::npos) {
        ++count;
        cursor += strlen(token);
    }
    return count;
}

std::string SourceCodeOnly(const std::string& source) {
    enum class LexState { Normal, LineComment, BlockComment, String, Character };
    LexState state = LexState::Normal;
    std::string code(source.size(), ' ');
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (state == LexState::LineComment) {
            if (c == '\n') {
                code[i] = c;
                state = LexState::Normal;
            }
            continue;
        }
        if (state == LexState::BlockComment) {
            if (c == '*' && next == '/') {
                state = LexState::Normal;
                ++i;
            }
            continue;
        }
        if (state == LexState::String || state == LexState::Character) {
            if (c == '\\') {
                ++i;
            } else if ((state == LexState::String && c == '"') ||
                       (state == LexState::Character && c == '\'')) {
                state = LexState::Normal;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            state = LexState::LineComment;
            ++i;
        } else if (c == '/' && next == '*') {
            state = LexState::BlockComment;
            ++i;
        } else if (c == '"') {
            state = LexState::String;
        } else if (c == '\'') {
            state = LexState::Character;
        } else {
            code[i] = c;
        }
    }
    return code;
}

std::string CompactSourceCode(const std::string& source) {
    const std::string code = SourceCodeOnly(source);
    std::string compact;
    compact.reserve(code.size());
    for (char c : code) {
        if (!isspace(static_cast<unsigned char>(c))) compact.push_back(c);
    }
    return compact;
}

bool SourceTokensInOrder(const std::string& source,
                         std::initializer_list<const char*> tokens) {
    size_t cursor = 0;
    for (const char* token : tokens) {
        const size_t found = source.find(token, cursor);
        if (found == std::string::npos) return false;
        cursor = found + strlen(token);
    }
    return true;
}

bool ReplaceFirstSourceToken(std::string& source, const char* from, const char* to) {
    if (!from || !to) return false;
    const size_t position = source.find(from);
    if (position == std::string::npos) return false;
    source.replace(position, strlen(from), to);
    return true;
}

bool ValidateTask6DetachBody(const std::string& body) {
    return CompactSourceCode(body) ==
           "caseDLL_PROCESS_DETACH:FfxHooks::EquipmentWorkshop::RequestStop();FfxHooks::NativePorts::RequestStop();FfxHooks::NativeLanguage::RequestStop();FfxHooks::SinAi::RequestStop();FfxHooks::FmvSpeed::RequestStop();FfxHooks::Fastload::RequestFastloadStop();FfxHooks::RequestNovaSuperDamageStop();FfxHooks::RequestSeymourBattleStop();"
           "FfxHooks::F7_RequestStop();"
           "FfxHooks::F7AiSwap_RequestStop();"
           "FfxHooks::RequestSpeedHackStop();"
           "FfxHooks::RequestDialogSkipStop();FfxHooks::RequestUnXBoosterStop();";
}

bool ValidateLoaderLockSafeAttachBody(const std::string& body) {
    const std::string code = SourceCodeOnly(body);
    const char* forbiddenCalls[] = {
        "EarlyLogLine(", "OpenLog(", "Log(", "LogLine(", "CloseLog(",
        "InstallHooks(", "RemoveHooks(", "Sleep(", "WaitFor", "GetTempPath",
        "CreateFile", "fopen(", "fclose(", "Config::Load(", "FindWindow(",
        "PostMessage(", "SendMessage(",
    };
    for (const char* call : forbiddenCalls) {
        if (code.find(call) != std::string::npos) return false;
    }

    const size_t debugSignals = CountSourceToken(code, "OutputDebugStringA(");
    if (debugSignals > 3 || CountSourceToken(code, "g_module = hMod;") != 1 ||
        CountSourceToken(code, "DWORD tid = 0;") != 1 ||
        CountSourceToken(
            code,
            "CreateThread(nullptr, 0, HooksWorkerThread, nullptr, 0, &tid)") != 1 ||
        CountSourceToken(code, "if (thread)") != 1 ||
        CountSourceToken(code, "CloseHandle(thread)") != 1 ||
        CountSourceToken(code, "(") != 3 + debugSignals ||
        CountSourceToken(code, ";") != 4 + debugSignals) {
        return false;
    }

    const SourceBlock threadOwnedHandle = SourceBlockAfterToken(code, "if (thread)");
    return threadOwnedHandle.Valid() &&
           CountSourceToken(threadOwnedHandle.body, "CloseHandle(thread)") == 1;
}

bool ValidateWorkerOwnsLoggingBoundary(const std::string& body) {
    const std::string code = SourceCodeOnly(body);
    const std::string compact = CompactSourceCode(body);
    if (CountSourceToken(code, "OpenLog(") != 1 ||
        compact.rfind("OpenLog();", 0) != 0) {
        return false;
    }
    const size_t open = code.find("OpenLog(");
    const char* deferredPaths[] = {
        "EarlyLogLine(", "Log(", "LogLine(", "AuroraConfigExists(",
        "AuroraConfigInt(", "AuroraConfigString(", "ModuleFlagEnabled(",
        "FfxHooks::Config::Load(", "InstallHooks(",
    };
    for (const char* call : deferredPaths) {
        size_t cursor = 0;
        while ((cursor = code.find(call, cursor)) != std::string::npos) {
            if (cursor < open) return false;
            cursor += strlen(call);
        }
    }
    return true;
}

bool ValidateF7BattleCheatsArbitration(const std::string& body) {
    const char* forbidden[] = {
        "reinterpret_cast<volatile uint8_t*>", "0xD2A8F8", "0x0092A8F8",
        "RVA_FFX_DEBUG_FLAGS", "= val ? 1 : 0",
    };
    for (const char* token : forbidden) {
        if (body.find(token) != std::string::npos) return false;
    }
    return SourceTokensInOrder(body, {
        "FindF8Flag(\"cheats.invincible_party\")",
        "SetF8FlagValue",
        "g_rowValue",
        "result.effective.value",
        "BoolSourceName(result.effective.source)",
        "F8AvailabilityName(result.runtime.availability)",
        "result.runtime.hasAppliedValue",
        "result.runtime.appliedValue",
    });
}

bool ValidatePresentReadyPublication(const std::string& body) {
    const size_t assignment = body.find("const bool hooked = g_auroraD3DPresentDetour->hook()");
    const size_t completion = body.find("CompletePresentHookInstall", assignment);
    const size_t publication = body.find("PublishAuroraD3DPresentResult", completion);
    return assignment != std::string::npos && completion != std::string::npos &&
           publication != std::string::npos && assignment < completion &&
           completion < publication &&
           body.find("NotifyUnXBoosterPresentProducer(") == std::string::npos;
}

bool ValidateIdleApGatePublication(const std::string& body) {
    return body.find("F8RuntimeAvailability::Available") != std::string::npos &&
           body.find("FailureReason::None") != std::string::npos &&
           body.find("EdgeState::Pending") != std::string::npos &&
           body.find("F8RuntimeAvailability::Pending") == std::string::npos &&
           body.find("FailureReason::BattleGate") == std::string::npos;
}

bool ValidateAdapterOwnsPostStartStatus(const std::string& source,
                                        const std::string& installBody) {
    const size_t start = installBody.find("StartUnXBoosterHook(g_base, LogLine)");
    const size_t nextOwner = installBody.find("AddVectoredExceptionHandler", start);
    if (start == std::string::npos || nextOwner == std::string::npos || start >= nextOwner) {
        return false;
    }
    const std::string postStart = installBody.substr(start, nextOwner - start);
    return source.find("PublishFutureUnXRowsUnavailable") == std::string::npos &&
           CountSourceToken(postStart, "PublishResolvedF8Status(") == 1 &&
           postStart.find("\"arena_plus.compose_f7\"") != std::string::npos &&
           postStart.find("ProducerUnavailable") != std::string::npos &&
           postStart.find("boosters.entire_party_earns_ap") == std::string::npos &&
           postStart.find("cheats.invincible_party") == std::string::npos;
}

bool ValidateUnsupportedAdapterStatusSticky(const std::string& startBody,
                                            const std::string& notifyBody) {
    const size_t terminal = notifyBody.find("if (terminalFailure)");
    const size_t validated = notifyBody.find("AdapterState::Validated", terminal);
    const size_t unavailable = notifyBody.find(
        "PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable", terminal);
    return SourceTokensInOrder(startBody, {
               "AdapterState::Unsupported", "F8RuntimeAvailability::UnsupportedBuild",
               "PublishAllRuntimeRows(availability, false, false)",
           }) && terminal != std::string::npos && validated != std::string::npos &&
           unavailable != std::string::npos && terminal < validated && validated < unavailable &&
           CountSourceToken(notifyBody,
                            "PublishAllRuntimeRows(F8RuntimeAvailability::ProducerUnavailable") == 1;
}

bool ValidateExactDebugBindingPairs(const std::string& source) {
    const std::string table = BoundedSourceSection(
        source,
        "std::array<DebugBinding, kOwnedDebugFieldCount> g_debugBindings = {{",
        "}};");
    if (table.empty() || CountSourceToken(table, "{\"") != 7) return false;
    const char* expectedPairs[] = {
        "{\"boosters.permanent_sensor\", FFX_DEBUG_PERMANENT_SENSOR_OFFSET}",
        "{\"cheats.invincible_party\", FFX_DEBUG_INVINCIBLE_PARTY_OFFSET}",
        "{\"cheats.invincible_enemies\", FFX_DEBUG_INVINCIBLE_ENEMIES_OFFSET}",
        "{\"cheats.always_overdrive\", FFX_DEBUG_ALWAYS_OVERDRIVE_OFFSET}",
        "{\"cheats.always_critical\", FFX_DEBUG_ALWAYS_CRITICAL_OFFSET}",
        "{\"cheats.damage_value\", FFX_DEBUG_ALWAYS_DEAL_99999_OFFSET}",
        "{\"cheats.always_rare_drop\", FFX_DEBUG_ALWAYS_RARE_REWARD_OFFSET}",
    };
    for (const char* pair : expectedPairs) {
        if (CountSourceToken(table, pair) != 1) return false;
    }
    return true;
}

bool ValidateExactApLoopStructure(const std::string& body) {
    const char* loopHeader =
        "for (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot)";
    if (CountSourceToken(body, "for (size_t slot = 0;") != 4 ||
        CountSourceToken(body, loopHeader) != 4) {
        return false;
    }
    std::array<SourceBlock, 4> loops{};
    size_t cursor = 0;
    for (SourceBlock& loop : loops) {
        loop = SourceBlockAfterToken(body, loopHeader, cursor);
        if (!loop.Valid()) return false;
        cursor = loop.close + 1;
    }
    const std::string& snapshot = loops[0].body;
    const std::string& participationWrite = loops[1].body;
    const std::string& earnWrite = loops[2].body;
    const std::string& readback = loops[3].body;
    return CountSourceToken(snapshot, "GuardedReadByte(") == 2 &&
           snapshot.find("g_adapter.inParty[slot], &inParty[slot]") != std::string::npos &&
           snapshot.find("g_adapter.participationBytes[slot], &participation[slot]") !=
               std::string::npos &&
           snapshot.find("GuardedStoreByte(") == std::string::npos &&
           CountSourceToken(participationWrite, "GuardedStoreByte(") == 1 &&
           participationWrite.find("g_adapter.participationBytes[slot]") != std::string::npos &&
           participationWrite.find("updates[slot].participation") != std::string::npos &&
           participationWrite.find("g_adapter.earnBytes[slot]") == std::string::npos &&
           CountSourceToken(earnWrite, "GuardedStoreByte(") == 1 &&
           earnWrite.find("g_adapter.earnBytes[slot]") != std::string::npos &&
           earnWrite.find("updates[slot].earn") != std::string::npos &&
           earnWrite.find("g_adapter.participationBytes[slot]") == std::string::npos &&
           CountSourceToken(readback, "GuardedReadByte(") == 2 &&
           readback.find("g_adapter.participationBytes[slot], &participationReadback[slot]") !=
               std::string::npos &&
           readback.find("g_adapter.earnBytes[slot], &earnReadback[slot]") !=
               std::string::npos &&
           CountSourceToken(
               readback,
               "participationReadback[slot] == updates[slot].participation") == 1 &&
           CountSourceToken(readback, "earnReadback[slot] == updates[slot].earn") == 1 &&
           CountSourceToken(readback, "readbackMatches") == 2 &&
           SourceTokensInOrder(readback, {
               "readbackMatches = participationRead && earnRead &&",
               "participationReadback[slot] == updates[slot].participation &&",
               "earnReadback[slot] == updates[slot].earn && readbackMatches;",
           }) &&
           readback.find("GuardedStoreByte(") == std::string::npos;
}

bool ValidateFallbackAttemptGate(const std::string& lateBody) {
    const SourceBlock attempt = SourceBlockAfterToken(
        lateBody, "bool attemptedPresentHook = false;");
    const SourceBlock terminal = SourceBlockAfterToken(
        lateBody, "if (attemptedPresentHook)");
    return attempt.Valid() && terminal.Valid() && attempt.close < terminal.open &&
           attempt.body.find("attemptedPresentHook = true") != std::string::npos &&
           attempt.body.find("AuroraD3DHookPresentFromVtable") != std::string::npos &&
           CountSourceToken(lateBody, "TryPublishAuroraD3DPresentTerminal()") == 1 &&
           CountSourceToken(terminal.body, "TryPublishAuroraD3DPresentTerminal()") == 1;
}

bool ValidatePresentArbiterIntegration(const std::string& source,
                                       const std::string& hookBody,
                                       const std::string& lateBody,
                                       const std::string& terminalBody,
                                       const std::string& publisherBody) {
    const bool pureState =
        source.find("AtomicPresentHookArbiter g_auroraD3DPresentHookArbiter") !=
            std::string::npos &&
        source.find("g_auroraD3DPresentHookState") == std::string::npos &&
        source.find("kAuroraD3DPresentHookTerminal") == std::string::npos;
    const bool completionOwnsPublication = SourceTokensInOrder(hookBody, {
        "TryBeginPresentHookInstall", "g_auroraD3DPresentDetour->hook()",
        "CompletePresentHookInstall", "PublishAuroraD3DPresentResult",
    });
    const bool requestOnly = SourceTokensInOrder(terminalBody, {
        "RequestPresentHookTerminal", "PublishAuroraD3DPresentResult",
    }) && terminalBody.find("Interlocked") == std::string::npos &&
        terminalBody.find("NotifyUnXBoosterPresentProducer") == std::string::npos;
    const bool onePublisher =
        CountSourceToken(publisherBody, "NotifyUnXBoosterPresentProducer(true, false)") == 1 &&
        CountSourceToken(publisherBody, "NotifyUnXBoosterPresentProducer(false, true)") == 1;
    return pureState && completionOwnsPublication && requestOnly && onePublisher &&
           ValidateFallbackAttemptGate(lateBody);
}

struct IfElseSourceBlocks {
    SourceBlock direct;
    SourceBlock fallback;
};

bool ExtractIfElseSourceBlocks(const std::string& branch, const char* ifToken,
                               IfElseSourceBlocks& result) {
    const size_t ifPosition = branch.find(ifToken);
    if (ifPosition == std::string::npos) return false;
    result.direct = SourceBlockAfterToken(branch, ifToken, ifPosition);
    if (!result.direct.Valid()) return false;
    size_t cursor = result.direct.close + 1;
    while (cursor < branch.size() && isspace(static_cast<unsigned char>(branch[cursor]))) ++cursor;
    if (branch.compare(cursor, 4, "else") != 0) return false;
    cursor += 4;
    while (cursor < branch.size() && isspace(static_cast<unsigned char>(branch[cursor]))) ++cursor;
    if (branch.compare(cursor, 2, "if") == 0 || cursor >= branch.size() || branch[cursor] != '{') {
        return false;
    }
    result.fallback.open = cursor;
    result.fallback.close = FindMatchingSourceBrace(branch, cursor);
    if (result.fallback.close == std::string::npos) return false;
    result.fallback.body = branch.substr(
        cursor + 1, result.fallback.close - result.fallback.open - 1);
    return true;
}

bool ValidateDirectFlagsExitBranch(const std::string& branch) {
    const size_t capture = branch.find("const bool wasDirectF8Flags");
    const size_t close = branch.find("F7Sub_CloseMenu()");
    IfElseSourceBlocks split;
    if (capture == std::string::npos || close == std::string::npos || capture >= close ||
        !ExtractIfElseSourceBlocks(branch, "if (wasDirectF8Flags)", split) ||
        split.direct.open <= close) {
        return false;
    }
    return CountSourceToken(branch, "F8ReturnFlagsToGame()") == 1 &&
           CountSourceToken(split.direct.body, "F8ReturnFlagsToGame()") == 1 &&
           CountSourceToken(split.fallback.body, "F8ReturnFlagsToGame()") == 0 &&
           CountSourceToken(branch, "InterlockedExchange(&g_nativeWantSpawn, 1)") == 1 &&
           CountSourceToken(split.direct.body, "InterlockedExchange(&g_nativeWantSpawn, 1)") == 0 &&
           CountSourceToken(split.fallback.body, "InterlockedExchange(&g_nativeWantSpawn, 1)") == 1;
}

bool ValidateSpawnCursorLifecycle(const std::string& body) {
    const size_t allocation = body.find("NativeMenu::Alloc()");
    const SourceBlock failure = SourceBlockAfterToken(body, "if (!obj)");
    const SourceBlock acquisition = SourceBlockAfterToken(body, "if (isFlags)");
    if (allocation == std::string::npos || !failure.Valid() || !acquisition.Valid() ||
        allocation >= failure.open || failure.close >= acquisition.open) {
        return false;
    }
    const size_t acquire = body.find("F8AcquireCursorVisibility()");
    return CountSourceToken(failure.body, "F8ReturnFlagsToGame()") == 1 &&
           CountSourceToken(failure.body, "return NativeMenu::Menu{ 0 }") == 1 &&
           CountSourceToken(body, "F8AcquireCursorVisibility()") == 1 &&
           acquire > acquisition.open && acquire < acquisition.close;
}

bool EveryF8OpenLatchReferenceIsAtomic(const std::string& source) {
    constexpr char kLatch[] = "g_f8MenuOpen";
    size_t cursor = 0;
    size_t references = 0;
    while ((cursor = source.find(kLatch, cursor)) != std::string::npos) {
        ++references;
        const size_t after = cursor + sizeof(kLatch) - 1;
        const size_t lineBegin = source.rfind('\n', cursor);
        const size_t lineEnd = source.find('\n', cursor);
        const std::string line = source.substr(
            lineBegin == std::string::npos ? 0 : lineBegin + 1,
            (lineEnd == std::string::npos ? source.size() : lineEnd) -
                (lineBegin == std::string::npos ? 0 : lineBegin + 1));
        const bool declaration =
            line.find("F8Ui::AtomicOpenLatch g_f8MenuOpen") != std::string::npos;
        const bool method = source.compare(after, 6, ".Load(") == 0 ||
                            source.compare(after, 7, ".Store(") == 0 ||
                            source.compare(after, 10, ".Exchange(") == 0;
        if (!declaration && !method) return false;
        cursor = after;
    }
    return references != 0;
}

bool ValidateGapRejectBeforeTabUse(const std::string& body, bool requireTabName) {
    const size_t offset = body.find("const float offsetInCell");
    const size_t reject = body.find("if (offsetInCell >= tabW) return;");
    const size_t select = body.find("g_f7Tab =");
    const size_t build = body.find("F7_BuildRows(F7_MENU_FLAGS)");
    const size_t tabName = body.find("F8TabName(");
    if (offset == std::string::npos || reject == std::string::npos ||
        select == std::string::npos || build == std::string::npos ||
        CountSourceToken(body, "if (offsetInCell >= tabW) return;") != 1 ||
        !(offset < reject && reject < select && reject < build)) {
        return false;
    }
    return !requireTabName || (tabName != std::string::npos && reject < tabName);
}

bool EveryNativeCloseHasPriorLatchPublication(const std::string& body) {
    const char* const closeToken = "WrB(obj, 65, 1)";
    const char* const confirmToken = "g_f7CloseLatch.RequestConfirm(";
    const char* const cancelToken = "g_f7CloseLatch.RequestCancel()";
    size_t segmentStart = 0;
    size_t close = std::string::npos;
    size_t closeCount = 0;
    while ((close = body.find(closeToken, segmentStart)) != std::string::npos) {
        const std::string segment = body.substr(segmentStart, close - segmentStart);
        const size_t publications = CountSourceToken(segment, confirmToken) +
                                    CountSourceToken(segment, cancelToken);
        if (publications != 1) return false;
        ++closeCount;
        segmentStart = close + strlen(closeToken);
    }
    return closeCount > 0 &&
           closeCount == CountSourceToken(body, confirmToken) +
                             CountSourceToken(body, cancelToken);
}

void TestF8FlagsCloseLatchAndGeometry() {
    using FfxHooks::F8Ui::CloseKind;
    using FfxHooks::F8Ui::Layout;

    FfxHooks::F8Ui::CloseLatch latch;
    FfxHooks::F8Ui::CloseEvent event = latch.Consume();
    Expect(event.kind == CloseKind::None && event.row == -1,
           "a fresh FLAGS close latch must have no event");

    latch.RequestConfirm(7);
    event = latch.Consume();
    Expect(event.kind == CloseKind::Confirm && event.row == 7,
           "FLAGS close latch must preserve the confirmed row");
    event = latch.Consume();
    Expect(event.kind == CloseKind::None && event.row == -1,
           "FLAGS close latch must consume a confirm exactly once");

    latch.RequestCancel();
    event = latch.Consume();
    Expect(event.kind == CloseKind::Cancel && event.row == -1,
           "FLAGS close latch must distinguish cancel from confirm");
    event = latch.Consume();
    Expect(event.kind == CloseKind::None && event.row == -1,
           "FLAGS close latch must consume a cancel exactly once");

    latch.RequestConfirm(3);
    latch.Reset();
    event = latch.Consume();
    Expect(event.kind == CloseKind::None && event.row == -1,
           "reset must discard a pending FLAGS close event");

    constexpr float panelTop = Layout::MainPanelTop;
    constexpr float tabBottom = Layout::TabTop + Layout::TabHeight;
    constexpr float ninthBottom = Layout::RowTop
        + static_cast<float>(Layout::VisibleRows - 1) * Layout::RowStep
        + Layout::RowHeight;

    Expect(Layout::VisibleRows == 9, "F8 must retain nine visible rows");
    Expect(Layout::TabTop - panelTop >= 0.010f,
           "F8 tabs must begin visibly inside the main panel");
    Expect(Layout::RowTop - tabBottom >= 0.010f,
           "F8 first row must retain a visible gap below tabs");
    Expect(Layout::DetailTop - ninthBottom >= 0.012f,
           "F8 ninth row must retain space before technical detail");

    Expect(Layout::TabTop + Layout::TabHeight < Layout::RowTop,
           "FLAGS rows must begin below the tab bar");
    Expect(Layout::RowTop + (Layout::VisibleRows - 1) * Layout::RowStep +
               Layout::RowHeight < Layout::DetailTop,
           "all visible FLAGS rows must end before the detail line");
    Expect(Layout::DetailTop < Layout::MainPanelTop + Layout::MainPanelHeight &&
               Layout::MainPanelTop + Layout::MainPanelHeight < Layout::FooterTop,
           "FLAGS detail must stay inside the main panel before the footer");
}

struct FakeCursorCounter {
    int value;
    int showCalls = 0;
    int hideCalls = 0;
};

int FakeShowCursor(bool show, void* context) noexcept {
    if (!context) return 0;
    FakeCursorCounter& counter = *static_cast<FakeCursorCounter*>(context);
    counter.value += show ? 1 : -1;
    if (show) {
        ++counter.showCalls;
    } else {
        ++counter.hideCalls;
    }
    return counter.value;
}

void TestF8FinalPolishPortableContracts() {
    using namespace FfxHooks::F8Ui;

    const float snappedRow = ResolveSelectionRowY(true, 40.0f, 100.0f);
    const float optInEasedRow = ResolveSelectionRowY(false, 40.0f, 100.0f);
    Expect(snappedRow == 100.0f,
           "the shared F7Sub selection highlight must snap to the selected row in the same frame");
    Expect(optInEasedRow > 57.99f && optInEasedRow < 58.01f,
           "the portable helper must retain its explicit opt-in 30-percent easing branch");

    static const char kLongestTechnicalStatus[] =
        "REQ OFF / EFF OFF | LIVE PRODUCER UNAVAILABLE | APPLIED UNKNOWN | EDIT REJECTED UNAVAILABLE";
    char technical[TechnicalStatusCharacterBudget + 1] = {};
    const TechnicalStatusParts longest = {
        "REQ OFF / EFF OFF",
        "LIVE PRODUCER UNAVAILABLE",
        "APPLIED UNKNOWN",
        "EDIT REJECTED UNAVAILABLE",
    };
    Expect(BuildTechnicalStatus(longest, technical, sizeof(technical)) &&
               strcmp(technical, kLongestTechnicalStatus) == 0 &&
               strlen(technical) <= TechnicalStatusCharacterBudget,
           "the longest supported F8 technical state must fit the explicit character budget in authority order");

    // R7-UX-B1: overlong combinations degrade by dropping tail tokens (edit, applied,
    // live) — they must never collapse to an empty line that the adapter renders as
    // STATUS UNAVAILABLE while the request/effective half still fits.
    const TechnicalStatusParts overflowing = {
        "EXT OFF: FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7",
        "LIVE PRODUCER UNAVAILABLE",
        "ARMED OFF",
        "EDIT REJECTED UNAVAILABLE",
    };
    memset(technical, 0, sizeof(technical));
    Expect(BuildTechnicalStatus(overflowing, technical, sizeof(technical)) &&
               strcmp(technical,
                      "EXT OFF: FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7 | LIVE PRODUCER UNAVAILABLE | ARMED OFF") == 0,
           "an overlong technical status must drop the edit token first and keep the artifact name");
    static const char kLongExtToken[] =
        "EXT OFF: ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMN";
    const TechnicalStatusParts extremeOverflow = {
        kLongExtToken,
        "LIVE PRODUCER UNAVAILABLE",
        "APPLIED UNKNOWN",
        "EDIT REJECTED UNAVAILABLE",
    };
    memset(technical, 0, sizeof(technical));
    static const char kExtremeExpected[] =
        "EXT OFF: ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMN | LIVE PRODUCER UNAVAILABLE";
    Expect(BuildTechnicalStatus(extremeOverflow, technical, sizeof(technical)) &&
               strcmp(technical, kExtremeExpected) == 0 &&
               strlen(technical) <= TechnicalStatusCharacterBudget,
           "extreme overflow keeps request/effective and live, shedding applied and edit");

    Expect(strcmp(FooterText(FooterMode::Toggle),
                  "U/D Navigate | L/R Tabs | Confirm Toggle | Back/F8 Exit") == 0,
           "toggle rows must use the exact compact footer");
    Expect(strcmp(FooterText(FooterMode::Configure),
                  "U/D Navigate | L/R Tabs | Confirm Configure | Back/F8 Exit") == 0,
           "scalar rows must use the exact compact configure footer");
    Expect(strcmp(FooterText(FooterMode::ScalarEdit),
                  "L/R +/-1 | U/D +/-10 | Confirm Save | Back Cancel | F8 Exit") == 0,
           "scalar edit mode must use the exact compact footer");
    Expect(strcmp(FooterText(FooterMode::Bulk),
                  "U/D Navigate | L/R Tabs | Confirm Apply Tab | Back/F8 Exit") == 0,
           "bulk rows must explicitly identify their tab-wide action");

    char speedLabel[64] = {};
    Expect(BuildSpeedIndicatorLabel(
               1, SpeedIndicatorLabelMode::Armed, speedLabel, sizeof(speedLabel)) &&
               strcmp(speedLabel, "Speed Hack 1x - Armed [Ctrl+Shift+K]") == 0 &&
               strstr(speedLabel, "Armed") != nullptr &&
               strstr(speedLabel, "Ctrl+Shift+K") != nullptr &&
               strstr(speedLabel, "Gameplay") == nullptr,
           "ready 1x Speed Hack must be labeled as armed with its chord, not as gameplay acceleration");
    Expect(BuildSpeedIndicatorLabel(
               2, SpeedIndicatorLabelMode::StandardBoost, speedLabel, sizeof(speedLabel)) &&
               strcmp(speedLabel, "Speed Hack 2x - Standard boost ARMED") == 0,
           "native Standard boost must remain visibly distinguished without claiming callback application");
    Expect(BuildSpeedIndicatorLabel(
               8, SpeedIndicatorLabelMode::FastFieldScenes, speedLabel, sizeof(speedLabel)) &&
               strcmp(speedLabel, "Speed Hack 8x - Fast field scenes APPLIED") == 0,
           "applied 8x acceleration must identify its reviewed field-scene scope");

    const SwitchboardRowMapper hiddenAurora(false, 2);
    Expect(hiddenAurora.Count() == 3,
           "developer gate OFF must expose Refresh plus plugins and zero Aurora rows");
    Expect(hiddenAurora.At(0).kind == SwitchboardRowKind::Refresh &&
               hiddenAurora.At(1).kind == SwitchboardRowKind::Plugin &&
               hiddenAurora.At(1).pluginIndex == 0 &&
               hiddenAurora.At(2).kind == SwitchboardRowKind::Plugin &&
               hiddenAurora.At(2).pluginIndex == 1,
           "developer gate OFF must map row zero to Refresh and the remaining rows directly to plugins");
    for (int row = 0; row < hiddenAurora.Count(); ++row) {
        const SwitchboardRow mapped = hiddenAurora.At(row);
        const char* label = SwitchboardStaticLabel(mapped.kind);
        Expect(mapped.kind != SwitchboardRowKind::AuroraActor &&
                   mapped.kind != SwitchboardRowKind::AuroraDetail &&
                   (!label || strstr(label, "Aurora") == nullptr),
               "developer gate OFF must expose neither an Aurora label nor an Aurora activation target");
    }

    const SwitchboardRowMapper visibleAurora(true, 2);
    Expect(visibleAurora.Count() == 5 &&
               visibleAurora.At(0).kind == SwitchboardRowKind::AuroraActor &&
               visibleAurora.At(1).kind == SwitchboardRowKind::AuroraDetail &&
               visibleAurora.At(2).kind == SwitchboardRowKind::Refresh &&
               visibleAurora.At(3).kind == SwitchboardRowKind::Plugin &&
               visibleAurora.At(3).pluginIndex == 0,
           "developer gate ON must prepend the two Aurora rows without shifting Refresh or plugin identity incorrectly");

    float menuX = -1.0f;
    float menuY = -1.0f;
    Expect(ConvertClientScreenPointToMenu(
               420, 440, 100, 200, 640, 480, 1024.0f, 768.0f, &menuX, &menuY) &&
               menuX == 512.0f && menuY == 384.0f,
           "tab hit testing must convert a screen cursor relative to the client origin and client extent");

    FakeCursorCounter counter{-3};
    CursorVisibilityBalance cursor;
    Expect(cursor.Acquire(FakeShowCursor, &counter) &&
               cursor.OwnedIncrements() == 3 && counter.value == 0 &&
               counter.showCalls == 3,
           "cursor acquisition must increment until visible and record every increment owned by F8");
    Expect(cursor.Acquire(FakeShowCursor, &counter) &&
               cursor.OwnedIncrements() == 3 && counter.showCalls == 3,
           "repeated acquisition while owned must not change the shared ShowCursor balance");
    Expect(cursor.Release(FakeShowCursor, &counter) &&
               cursor.OwnedIncrements() == 0 && counter.value == -3 &&
               counter.hideCalls == 3,
           "cursor release must reverse exactly the increments owned by F8");
    Expect(cursor.Release(FakeShowCursor, &counter) && counter.hideCalls == 3,
           "repeated cursor release must be idempotent");
}

void TestF8WakePendingRejectsAndRollsBackDirectOpen() {
    using namespace FfxHooks::F8Ui;

    constexpr LONG kFlagsKind = 4;
    volatile LONG wantSpawn = 1;
    volatile LONG wantKind = kFlagsKind;
    volatile LONG ownerPublished = 0;
    AtomicOpenLatch f8MenuOpen;
    f8MenuOpen.Store(true);
    bool mouseWasDown = true;
    bool maechenWakePending = true;
    bool sharedForce = true;
    int allocations = 0;
    ScalarEditor editor;
    CloseLatch closeLatch;
    Expect(editor.Begin(2, 25, 0, 100),
           "the rejected-open fixture must begin with an unfinished local scalar draft");
    closeLatch.RequestCancel();

    const LONG spawnRequest = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
        &wantSpawn, 0, &ownerPublished);
    const LONG pendingKind = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
        &wantKind, -1, &ownerPublished);
    const PendingOpenDisposition rejected = DecidePendingOpen(
        !maechenWakePending, pendingKind == kFlagsKind);
    if (spawnRequest != 0 && rejected == PendingOpenDisposition::Allocate) {
        ++allocations;
    } else if (rejected == PendingOpenDisposition::RejectDirectF8) {
        RollbackRejectedDirectOpen(&editor, &mouseWasDown, &closeLatch);
        f8MenuOpen.Store(false);
    }
    const CloseEvent rejectedClose = closeLatch.Consume();
    Expect(allocations == 0 && wantSpawn == 0 && wantKind == -1 &&
               !f8MenuOpen.Load() && !editor.Active() && !mouseWasDown &&
               rejectedClose.kind == CloseKind::None &&
               maechenWakePending && sharedForce && ownerPublished == 1,
           "wake-pending rejection must consume both requests, allocate nothing, roll back only F8-local state, and retain Maechen wake/force");

    // Model the normal end-of-pump reservation refresh, then the next physical
    // edge after the terminal wake has been reaped.
    ownerPublished = 0;
    maechenWakePending = false;
    if (!f8MenuOpen.Exchange(false) && !maechenWakePending) {
        f8MenuOpen.Store(true);
        wantKind = kFlagsKind;
        wantSpawn = 1;
    }
    const LONG nextSpawn = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
        &wantSpawn, 0, &ownerPublished);
    const LONG nextKind = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
        &wantKind, -1, &ownerPublished);
    const PendingOpenDisposition accepted = DecidePendingOpen(
        true, nextKind == kFlagsKind);
    if (nextSpawn != 0 && accepted == PendingOpenDisposition::Allocate) {
        ++allocations;
    }
    Expect(allocations == 1 && wantSpawn == 0 && wantKind == -1 &&
               f8MenuOpen.Load() && !maechenWakePending && sharedForce,
           "after reap, the next valid F8 edge must consume clean requests and allocate normally");
}

void TestF8RejectedOpenAtomicPublicationAcrossThreads() {
    using namespace FfxHooks::F8Ui;

    AtomicOpenLatch latch;
    latch.Store(true);
    ScalarEditor editor;
    bool mouseWasDown = true;
    CloseLatch closeLatch;
    Expect(editor.Begin(3, 40, 0, 100),
           "the publication fixture must begin with a Pump-local draft");
    closeLatch.RequestCancel();

    std::atomic<bool> start{false};
    bool nextPresentChoseOpen = false;
    bool pumpCleanupVisible = false;
    std::thread pump([&]() {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        RollbackRejectedDirectOpen(&editor, &mouseWasDown, &closeLatch);
        latch.Store(false);
    });
    std::thread present([&]() {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(500);
        while (latch.Load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (!latch.Load()) {
            nextPresentChoseOpen = !latch.Exchange(true);
            const CloseEvent event = closeLatch.Consume();
            pumpCleanupVisible = !editor.Active() && !mouseWasDown &&
                                 event.kind == CloseKind::None;
        }
    });
    start.store(true, std::memory_order_release);
    pump.join();
    present.join();

    Expect(nextPresentChoseOpen && pumpCleanupVisible && latch.Load(),
           "Pump's atomic rejected-open clear must publish local cleanup before the next Present edge chooses open instead of close");
}

void TestF8FlagsCloseAndModalSourceContracts() {
    std::string source;
    std::string nativeMenuHeader;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for FLAGS close contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("..\\NativeMenuShell\\NativeMenuShell.h"),
                         nativeMenuHeader),
           "NativeMenuShell header must be readable for modal ownership contracts");

    ExpectSourceIncludes(source, "#include \"hooks/F8FlagsUiState.h\"",
                         "dllmain must consume the portable FLAGS close/layout contract");
    ExpectSourceExcludes(source, "g_f7ClosedFlag",
                         "the dead FLAGS closed flag must be removed");
    ExpectSourceExcludes(source, "g_f7ConfirmRow",
                         "the split FLAGS confirm row must be removed");

    const SourceBlock input = SourceFunctionBody(source, "static int __cdecl F7Sub_InputCb(int obj)");
    Expect(input.Valid() && EveryNativeCloseHasPriorLatchPublication(input.body),
           "every F7/FLAGS native close byte must follow exactly one close-latch publication");

    const size_t flagsStart = input.body.rfind("if (g_f7MenuKind == F7_MENU_FLAGS)");
    const SourceBlock flags = SourceBlockAfterToken(
        input.body, "if (g_f7MenuKind == F7_MENU_FLAGS)", flagsStart);
    const size_t postEditorNavigation = flags.body.find("if ((dir & 0x8000)");
    const SourceBlock confirm = SourceBlockAfterToken(
        flags.body, "if (confirmPressed)", postEditorNavigation);
    const SourceBlock back = SourceBlockAfterToken(confirm.body, "if (R.type == F7RT_BACK)");
    const SourceBlock cancel = SourceBlockAfterToken(flags.body, "else if (cancelPressed)", postEditorNavigation);
    Expect(flags.Valid() && back.Valid() && SourceTokensInOrder(back.body, {
               "g_f7CloseLatch.RequestConfirm(sel)", "WrB(obj, 65, 1)"}),
           "FLAGS Back must publish its confirm row before requesting native close");
    Expect(flags.Valid() && cancel.Valid() && SourceTokensInOrder(cancel.body, {
               "g_f7CloseLatch.RequestCancel()", "WrB(obj, 65, 1)"}),
           "FLAGS Cancel must publish cancel before requesting native close");

    const SourceBlock spawn = SourceFunctionBody(
        source, "static NativeMenu::Menu F7Sub_SpawnMenu(int kind)");
    Expect(spawn.Valid() && CountSourceToken(spawn.body, "g_f7CloseLatch.Reset()") == 1,
           "submenu spawn must reset the close latch exactly once");

    const SourceBlock poll = SourceFunctionBody(
        source, "static NativeMenu::Poll F7Sub_PollMenu(const NativeMenu::Menu& m)");
    Expect(poll.Valid() && CountSourceToken(poll.body, "g_f7CloseLatch.Consume()") == 1 &&
               poll.body.find("F8Ui::CloseKind::Confirm") != std::string::npos &&
               poll.body.find("F8Ui::CloseKind::Cancel") != std::string::npos,
           "submenu pump poll must consume each close event once and preserve its kind");

    const SourceBlock close = SourceFunctionBody(source, "static void F7Sub_CloseMenu()");
    Expect(close.Valid() && SourceTokensInOrder(close.body, {
               "const int closingObj = g_f7Menu.obj", "WrB(closingObj, 65, 1)",
               "NativeMenu::ReleaseModalIfOwned(closingObj)", "g_f7Menu.obj = 0",
           }),
           "submenu close must request native close and release only its modal before clearing ownership");

    const SourceBlock release = SourceFunctionBody(
        nativeMenuHeader, "static inline void ReleaseModalIfOwned(int obj)");
    Expect(release.Valid() &&
               release.body.find("*currentPopup == obj") != std::string::npos &&
               CountSourceToken(release.body, "*currentPopup = 0") == 1,
           "modal release must clear VA_CurrentPopup only while the closing menu still owns it");
}

void TestF8FlagsReadableLayoutSourceContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for FLAGS layout contracts");

    const SourceBlock draw = SourceFunctionBody(source, "static int __cdecl F7Sub_DrawCb(int obj)");
    const SourceBlock mouse = SourceFunctionBody(source, "static void F8MouseTabHitTest(int obj)");
    const SourceBlock status = SourceFunctionBody(
        source, "static void F8BuildSelectedStatus(int sel, char* out, size_t outSize)");
    Expect(draw.Valid() && mouse.Valid() && status.Valid(),
           "FLAGS draw, mouse-tab, and technical-status bodies must be structurally readable");

    const char* const layoutTokens[] = {
        "F8Ui::Layout::TabTop", "F8Ui::Layout::TabHeight", "F8Ui::Layout::RowTop",
        "F8Ui::Layout::RowStep", "F8Ui::Layout::RowHeight", "F8Ui::Layout::DetailTop",
        "F8Ui::Layout::MainPanelTop", "F8Ui::Layout::MainPanelHeight",
        "F8Ui::Layout::FooterTop", "F8Ui::Layout::VisibleRows",
    };
    for (const char* token : layoutTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "FLAGS draw path must consume normalized layout token %s", token);
        ExpectSourceIncludes(draw.body, token, message);
    }
    Expect(mouse.body.find("F8Ui::Layout::TabTop") != std::string::npos &&
               mouse.body.find("F8Ui::Layout::TabHeight") != std::string::npos,
           "tab-only mouse hit testing must share the normalized tab geometry");

    const size_t selectionUpdate =
        draw.body.find("F8Ui::ResolveSelectionRowY(true");
    const size_t selectionBackground =
        draw.body.find("DrawSolidRect(vLeft, ey, vWidth, vBarH, lift0, lift1)");
    const size_t rowLoop = draw.body.find("for (int r = 0; r < page");
    const size_t rowLabel = draw.body.find("DrawString(_lbl", rowLoop);
    const size_t technicalDetail = draw.body.find("F8BuildSelectedStatus(sel");
    Expect(selectionUpdate != std::string::npos &&
               selectionBackground != std::string::npos && rowLoop != std::string::npos &&
               rowLabel != std::string::npos && technicalDetail != std::string::npos &&
               selectionUpdate < selectionBackground && selectionBackground < rowLoop &&
               rowLoop < rowLabel && rowLabel < technicalDetail,
            "the shared FLAGS/F7Sub path must resolve its same-frame row before highlight, labels, and technical detail rendering");
    Expect(CountSourceToken(draw.body, "F8Ui::ResolveSelectionRowY(") == 1 &&
               source.find("F8Ui::ResolveSelectionRowY(false") == std::string::npos,
           "production F7Sub rendering must keep one shared snap call and no stale eased call");
    const SourceBlock rows = SourceBlockAfterToken(draw.body, "for (int r = 0; r < page");
    Expect(rows.Valid() && CountSourceToken(rows.body, "DrawString(_lbl") == 1,
           "FLAGS rows must keep exactly one label draw in the row loop");
    Expect(CountSourceToken(draw.body, "DrawStringSub(encoded") == 1 &&
               draw.body.find("F8Ui::Layout::DetailTop") != std::string::npos &&
               draw.body.find("F8Ui::TechnicalStatusCharacterBudget + 1") != std::string::npos,
            "FLAGS must keep one bounded technical detail draw inside the normalized main panel");
    Expect(draw.body.find("subTxt = g_f7Rows[sel].desc") != std::string::npos,
            "FLAGS selected help must reuse the existing header subtitle draw");
    Expect(draw.body.find("F8Ui::FooterText(") != std::string::npos,
           "FLAGS draw must consume the portable exact compact footer contract");
    Expect(status.body.find("F8Ui::BuildTechnicalStatus(") != std::string::npos,
           "FLAGS status adapter must use the portable bounded authority-order composer");

    const SourceBlock configPolled = SourceBlockAfterToken(
        status.body, "if (flag.applyMode == FfxHooks::F8ApplyMode::ConfigPolled)");
    const SourceBlock acknowledged = SourceBlockAfterToken(
        status.body, "else if (flag.applyMode == FfxHooks::F8ApplyMode::RuntimeAcknowledged)");
    Expect(configPolled.Valid() && configPolled.body.find("ARMED") != std::string::npos &&
               configPolled.body.find("APPLIED") == std::string::npos,
           "ConfigPolled FLAGS state must say ARMED rather than claiming application");
    Expect(acknowledged.Valid() && acknowledged.body.find("APPLIED") != std::string::npos,
           "RuntimeAcknowledged FLAGS state must retain producer-applied readback");
    Expect(status.body.find("flag.help") == std::string::npos,
           "the FLAGS detail builder must emit technical state only");
}

void TestF8BulkUiSourceContracts() {
    std::string catalogHeader, catalogSource, adapter;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\F8FlagCatalog.h"), catalogHeader) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\F8FlagCatalog.cpp"), catalogSource) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), adapter),
           "F8 catalog and adapter sources must be readable for bulk-action contracts");
    Expect(catalogHeader.find("struct F8BulkEditResult") != std::string::npos &&
               catalogHeader.find("SetF8TabValues") != std::string::npos &&
               catalogHeader.find("enum class F8BulkRowCode") != std::string::npos &&
               catalogHeader.find("ExternalOverride") != std::string::npos &&
               catalogHeader.find("EffectiveMismatch") != std::string::npos &&
               catalogHeader.find("InvalidParameter") != std::string::npos &&
               catalogHeader.find("F8BulkRowResult rows[") != std::string::npos &&
               catalogHeader.find("F8GateSourceDetail") != std::string::npos,
           "F8 must expose per-row bulk outcomes distinguishing unavailable, external "
           "override, invalid parameter, and post-write mismatch");
    const SourceBlock bulk = SourceFunctionBody(
        catalogSource, "F8BulkEditResult SetF8TabValues(const char* tab, bool requestedValue)");
    Expect(bulk.Valid() && bulk.body.find("F8Activation::NotWired") != std::string::npos &&
               bulk.body.find("Config::SetAuthoritativeBools") != std::string::npos &&
               CountSourceToken(bulk.body, "SetAuthoritativeBools") == 1,
           "bulk edits must skip NOT WIRED rows and persist the accepted tab set atomically once");
    Expect(bulk.body.find("F8BulkRowCode::Unavailable") != std::string::npos &&
               bulk.body.find("F8BulkRowCode::ExternalOverride") != std::string::npos &&
               bulk.body.find("F8BulkRowCode::EffectiveMismatch") != std::string::npos &&
               bulk.body.find("++result.blocked") == std::string::npos,
           "the bulk transaction must classify rows instead of collapsing them into blocked");
    const SourceBlock build = SourceFunctionBody(adapter, "static void F7_BuildRows(int kind)");
    const SourceBlock apply = SourceFunctionBody(
        adapter, "static void F8ApplyTabBulk(bool requestedValue, int selectedRow)");
    const SourceBlock input = SourceFunctionBody(adapter, "static int __cdecl F7Sub_InputCb(int obj)");
    Expect(build.Valid() && build.body.find("\"Enable Supported\"") != std::string::npos &&
               build.body.find("\"Disable Supported\"") != std::string::npos &&
               build.body.find("F7RT_BULK") != std::string::npos && apply.Valid() &&
               apply.body.find("SetF8TabValues") != std::string::npos && input.Valid() &&
               input.body.find("F8ApplyTabBulk") != std::string::npos,
           "editable multi-flag tabs must expose explicit Enable/Disable Supported actions");
    Expect(apply.body.find("F8GateSourceDetail") != std::string::npos &&
               apply.body.find("result.externalOverride") != std::string::npos &&
               apply.body.find("result.unavailable") != std::string::npos,
           "the bulk summary must name the first actionable blocker and its artifact");
    Expect(apply.body.find("PlaySfx(clean ? 4 : 3)") != std::string::npos &&
               apply.body.find("result.unavailable == 0") == std::string::npos,
           "expected unavailable skips must keep the success tone; only real failures warn");
    // R8-U1: per-row verdict writeback — rows skipped by the bulk run must explain
    // themselves in the header help when selected, not only in the log.
    Expect(apply.body.find("g_f8RowVerdicts") != std::string::npos &&
               apply.body.find("g_f7FlagSpecs[r] != entry.flag") != std::string::npos &&
               apply.body.find("g_f7Rows[r].desc = g_f8RowVerdicts[r]") != std::string::npos &&
               apply.body.find("bulk: %s%s%s") != std::string::npos,
           "non-trivial bulk rows must carry their verdict on the row desc until rebuild");
    Expect(build.body.find("memset(g_f8RowVerdicts") != std::string::npos,
           "verdict buffers must reset when the tab rebuilds so stale verdicts never leak");
}

// R4: the F8 menu's cursor balance, scalar editor, close latch, and pending-open
// flags must all drain through the shared focus-loss close — and must never leave
// a sticky background menu alive after focus returns.
void TestF8FocusLossDrainContract() {
    std::string adapter;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), adapter),
           "dllmain source must be readable for the F8 focus-loss drain contract");

    const SourceBlock subClose = SourceFunctionBody(
        adapter, "static void F7Sub_CloseMenu()");
    Expect(subClose.Valid() &&
               subClose.body.find("ReleaseModalIfOwned") != std::string::npos &&
               subClose.body.find("F8ReleaseCursorOwnership()") != std::string::npos &&
               subClose.body.find("g_f8ScalarEditor.Cancel()") != std::string::npos &&
               subClose.body.find("g_f8MenuOpen.Store(false)") != std::string::npos &&
               subClose.body.find("g_f8MouseWasDown = false") != std::string::npos &&
               subClose.body.find("g_f7CloseLatch.Reset()") != std::string::npos,
           "F7Sub close must release modal/cursor/scalar-editor/menu-open/latch "
           "ownership for the FLAGS family");

    const SourceBlock returnToGame = SourceFunctionBody(
        adapter, "static void F8ReturnFlagsToGame()");
    Expect(returnToGame.Valid() &&
               returnToGame.body.find("g_f8ScalarEditor.Cancel()") != std::string::npos &&
               returnToGame.body.find("F8ReleaseCursorOwnership()") != std::string::npos &&
               returnToGame.body.find("g_f8MenuOpen.Store(false)") != std::string::npos &&
               returnToGame.body.find("g_nativeWantSpawn, 0") != std::string::npos &&
               returnToGame.body.find("g_nativeWantClose, 0") != std::string::npos &&
               returnToGame.body.find("g_f7WantOpenKind, -1") != std::string::npos &&
               returnToGame.body.find("g_forceSubsystem, 0") != std::string::npos &&
               returnToGame.body.find("NativeMenuForceGateClear()") != std::string::npos,
           "direct-F8 return must drain editor, cursor, open flag, pending opens, "
           "and the force gate");

    const SourceBlock subInput = SourceFunctionBody(
        adapter, "static int __cdecl F7Sub_InputCb(int obj)");
    Expect(subInput.Valid() &&
               subInput.body.find("F7IsForegroundWindow()") != std::string::npos &&
               subInput.body.find("FfxHooks::F7Ui::CloseSource::FocusLost") !=
                   std::string::npos,
           "the FLAGS input callback must request a focus-lost close when the game "
           "window is no longer foreground");

    // The direct-F8 menu (F8 key, no F7 hub) must close on focus loss too: its input
    // gate must still require the foreground window, and the shared close transition
    // must drain it through the F8 return path instead of returning early.
    Expect(subInput.body.find("inputAdmitted = directF8Flags") != std::string::npos &&
               subInput.body.find("!directF8Flags &&") == std::string::npos,
           "a direct-F8 menu must not bypass the foreground gate; focus loss closes it");
    const SourceBlock closeTransition = SourceFunctionBody(
        adapter, "FfxHooks::F7Ui::CloseDestination destination)");
    Expect(closeTransition.Valid() &&
               closeTransition.body.find("F7Sub_CloseMenu();") != std::string::npos &&
               closeTransition.body.find("F8ReturnFlagsToGame();") != std::string::npos &&
               closeTransition.body.find("if (directF8Flags) return;") ==
                   std::string::npos,
           "a focus-lost close on a direct-F8 menu must drain F8 ownership rather "
           "than returning early");
}

void TestF8CatalogHelpIsPlayerFacing() {
    for (size_t index = 0; index < FfxHooks::F8FlagCount(); ++index) {
        const FfxHooks::F8FlagSpec& flag = FfxHooks::F8FlagAt(index);
        const char* prefix = flag.activation == F8Activation::Live
            ? "LIVE - "
            : flag.activation == F8Activation::RestartRequired
                ? "RESTART REQUIRED - "
                : flag.activation == F8Activation::ReadOnly ? "READ ONLY - " : "NOT WIRED - ";
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "catalog row %zu help must preserve its activation prefix", index);
        Expect(flag.help && strncmp(flag.help, prefix, strlen(prefix)) == 0, message);
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "catalog row %zu help must fit the 63-byte subtitle payload", index);
        Expect(flag.help && strlen(flag.help) <= 63, message);

        const char* const internalTerms[] = {
            "consumer", "evidence only", "effective source", "probe", "scaffolding", "resolver",
        };
        std::string normalizedHelp = flag.help ? flag.help : "";
        for (char& c : normalizedHelp) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        for (const char* term : internalTerms) {
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                        "catalog row %zu help must not expose internal term %s", index, term);
            Expect(flag.help && normalizedHelp.find(term) == std::string::npos, message);
        }
    }
    const FfxHooks::F8FlagSpec* seymour =
        FfxHooks::FindF8Flag("boosters.playable_seymour");
    Expect(seymour && seymour->activation == F8Activation::Live &&
               seymour->applyMode == F8ApplyMode::RuntimeAcknowledged &&
               strstr(seymour->help, "battle roster") != nullptr &&
               strstr(seymour->help, "Sphere Grid") != nullptr,
           "Playable Seymour must state the experimental battle-only LIVE boundary");
}

void TestActiveF8ConsumersUseTheCatalog() {
    std::string dllmainSource;
    std::string dashboardSource;
    std::string composeSource;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmainSource),
           "dllmain source must be readable from the RT0 test source path");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\InGameMenuDashboard.cpp"), dashboardSource),
           "dashboard source must be readable from the RT0 test source path");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\ArenaPlusComposePick.cpp"), composeSource),
           "Compose source must be readable from the RT0 test source path");

    const std::string activeConsumers = dllmainSource + dashboardSource + composeSource;
    const char* staleDllmainTokens[] = {
        "s_flagTabRows", "s_flagTabNames", "kFlagTabCount", "g_f7FlagKeys",
    };
    for (const char* token : staleDllmainTokens) {
        char message[160] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "active F8 source must not retain duplicate token %s", token);
        ExpectSourceExcludes(dllmainSource, token, message);
    }
    ExpectSourceExcludes(dashboardSource, "TabAdd(",
                         "dashboard edge adapter must not retain TabAdd calls");
    ExpectSourceExcludes(dashboardSource, "BuildTabs(",
                         "dashboard edge adapter must not retain BuildTabs calls");
    Expect(CountSourceToken(dllmainSource, "StartUnXBoosterHook(") == 1,
           "Task 6 must add exactly one canonical UnX booster start call");

    const char* requiredCatalogTokens[] = {
        "F8FlagAt", "F8TabCount", "F8TabName", "SetF8FlagValue", "PublishF8RuntimeStatus",
    };
    for (const char* token : requiredCatalogTokens) {
        char message[176] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "active F8 consumers must use catalog API %s", token);
        ExpectSourceIncludes(activeConsumers, token, message);
    }
}

void TestDirectF8FlagsLifecycleSourceContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for direct F8 lifecycle contracts");
    Expect(EveryF8OpenLatchReferenceIsAtomic(source),
           "every production F8 open-latch reference must use the one AtomicOpenLatch API without plain reads, writes, or aliases");

    const SourceBlock pump = SourceFunctionBody(
        source, "static int __cdecl NativeMenu_PumpHook(unsigned int a1)");
    const SourceBlock submenu = SourceBlockAfterToken(pump.body, "else if (g_f7Menu.obj)");
    const SourceBlock confirm = SourceBlockAfterToken(
        submenu.body, "if (p.what == NativeMenu::POLL_CONFIRM)");
    const SourceBlock cancel = SourceBlockAfterToken(
        submenu.body, "else if (p.what == NativeMenu::POLL_CANCEL)");
    Expect(pump.Valid() && submenu.Valid() && confirm.Valid() &&
               ValidateDirectFlagsExitBranch(confirm.body),
           "direct F8 FLAGS Back must close to game while ordinary F7 Back still returns to hub");
    Expect(pump.Valid() && submenu.Valid() && cancel.Valid() &&
               ValidateDirectFlagsExitBranch(cancel.body),
           "direct F8 FLAGS Cancel must close to game while ordinary F7 Cancel still returns to hub");

    const SourceBlock returnToGame = SourceFunctionBody(
        source, "static void F8ReturnFlagsToGame()");
    const SourceBlock rollbackRejected = SourceFunctionBody(
        source, "static void F8RollbackRejectedDirectOpen()");
    const bool clearsMouseEdge =
        returnToGame.body.find("g_f8MouseWasDown = false") != std::string::npos ||
        returnToGame.body.find("s_f7MouseLWasDown = false") != std::string::npos;
    Expect(returnToGame.Valid() &&
               returnToGame.body.find("g_f8MenuOpen.Store(false)") != std::string::npos &&
               clearsMouseEdge &&
               returnToGame.body.find("InterlockedExchange(&g_nativeWantSpawn, 0)") != std::string::npos &&
               returnToGame.body.find("InterlockedExchange(&g_forceSubsystem, 0)") != std::string::npos &&
               returnToGame.body.find("NativeMenuForceGateClear()") != std::string::npos,
           "direct F8 FLAGS return-to-game must clear ownership, input, force, gate, and keep-alive");
    Expect(rollbackRejected.Valid() &&
               rollbackRejected.body.find("F8Ui::RollbackRejectedDirectOpen(") != std::string::npos &&
               rollbackRejected.body.find("F8ReleaseCursorOwnership()") != std::string::npos &&
               rollbackRejected.body.find("g_f8MenuOpen.Store(false)") != std::string::npos &&
               SourceTokensInOrder(rollbackRejected.body, {
                   "F8Ui::RollbackRejectedDirectOpen(",
                   "F8ReleaseCursorOwnership()",
                   "g_f8MenuOpen.Store(false)"}) &&
               rollbackRejected.body.find("Interlocked") == std::string::npos &&
               rollbackRejected.body.find("g_forceSubsystem") == std::string::npos &&
               rollbackRejected.body.find("Maechen") == std::string::npos &&
               rollbackRejected.body.find("F7Sub_CloseMenu") == std::string::npos,
           "Pump rejection rollback must clear only F8-local pre-open state and never another owner's force, wake, or menu");
    Expect(pump.Valid() && SourceTokensInOrder(pump.body, {
               "FfxHooks::Maechen_PumpTick(F7IsForegroundWindow())",
               "const bool directF8FlagsRequest",
               "F8Ui::DecidePendingOpen(",
               "F8Ui::PendingOpenDisposition::RejectDirectF8",
               "F8RollbackRejectedDirectOpen()",
               "NativeMenuPublishOwnerReservationFromPumpState()"}) &&
               CountSourceToken(pump.body, "F8RollbackRejectedDirectOpen()") == 1,
           "Pump must decide after Maechen, consume once, and locally roll back a rejected direct F8 before reservation refresh");
}

void TestF8FlagsCursorAndBoundsSourceContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for F8 cursor and bounds contracts");

    const SourceBlock spawn = SourceFunctionBody(
        source, "static NativeMenu::Menu F7Sub_SpawnMenu(int kind)");
    Expect(spawn.Valid() && ValidateSpawnCursorLifecycle(spawn.body),
            "FLAGS cursor ownership must begin only after allocation and allocation failure must reset F8");
    const SourceBlock acquire = SourceFunctionBody(
        source, "static void F8AcquireCursorVisibility()");
    const SourceBlock release = SourceFunctionBody(
        source, "static void F8ReleaseCursorOwnership()");
    Expect(acquire.Valid() && release.Valid() &&
               acquire.body.find("g_f8CursorBalance.Acquire(F8ShowCursorAdapter") !=
                   std::string::npos &&
               release.body.find("g_f8CursorBalance.Release(F8ShowCursorAdapter") !=
                   std::string::npos,
            "FLAGS cursor adapter must acquire until visible and release the exact portable balance");

    const SourceBlock rollback = SourceFunctionBody(
        source, "static void F8RollbackRejectedDirectOpen()");
    const SourceBlock returnToGame = SourceFunctionBody(
        source, "static void F8ReturnFlagsToGame()");
    const SourceBlock close = SourceFunctionBody(source, "static void F7Sub_CloseMenu()");
    Expect(rollback.Valid() && returnToGame.Valid() && close.Valid() &&
               CountSourceToken(rollback.body, "F8ReleaseCursorOwnership()") == 1 &&
               CountSourceToken(returnToGame.body, "F8ReleaseCursorOwnership()") == 1 &&
               CountSourceToken(close.body, "F8ReleaseCursorOwnership()") == 1,
            "rejected open, direct return, and native close must all discharge cursor ownership idempotently");

    const SourceBlock f8HitTest = SourceFunctionBody(
        source, "static void F8MouseTabHitTest(int obj)");
    if (f8HitTest.Valid()) {
        Expect(ValidateGapRejectBeforeTabUse(f8HitTest.body, true) &&
                   f8HitTest.body.find("GetClientRect(hwnd, &client)") != std::string::npos &&
                   f8HitTest.body.find("ClientToScreen(hwnd, &clientOrigin)") != std::string::npos &&
                   f8HitTest.body.find("F8Ui::ConvertClientScreenPointToMenu(") !=
                       std::string::npos &&
                   f8HitTest.body.find("GetWindowRect") == std::string::npos,
                "F8MouseTabHitTest must use client coordinates and reject gaps before catalog tab selection, name, or use");
    } else {
        // The intentionally dirty checkout has a broader unstaged mouse implementation. Keep its
        // source contract branch-scoped too; clean/committed snapshots take the strict path above.
        const SourceBlock dirtyMouse = SourceFunctionBody(source, "static void F7_MouseNav(int obj)");
        Expect(dirtyMouse.Valid() && ValidateGapRejectBeforeTabUse(dirtyMouse.body, false),
               "active dirty FLAGS mouse path must reject gaps before tab selection or use");
    }

    const std::string labelStorage = BoundedSourceSection(
        source, "static int              g_f7Vals[32]",
        "static int              g_f7ConfirmTimer");
    Expect(labelStorage.find("g_f7Labels[32][64]") != std::string::npos &&
               labelStorage.find("g_f7SubLabels[32][48]") != std::string::npos,
           "encoded F7 label capacity must match the 32-row FLAGS storage");
}

void TestSourceCheckerMutationPressure() {
    const std::string validDirectExit =
        "const bool wasDirectF8Flags = true;\n"
        "F7Sub_CloseMenu();\n"
        "if (wasDirectF8Flags) { F8ReturnFlagsToGame(); }\n"
        "else { InterlockedExchange(&g_nativeWantSpawn, 1); }\n";
    Expect(ValidateDirectFlagsExitBranch(validDirectExit),
           "direct-exit validator must accept the hand-checked mutually-exclusive fixture");
    const std::string unconditionalHubSpawn =
        "const bool wasDirectF8Flags = true;\n"
        "F7Sub_CloseMenu();\n"
        "if (wasDirectF8Flags) { F8ReturnFlagsToGame(); }\n"
        "InterlockedExchange(&g_nativeWantSpawn, 1);\n";
    Expect(!ValidateDirectFlagsExitBranch(unconditionalHubSpawn),
           "mutation proof: unconditional hub spawn after direct FLAGS return must be rejected");

    const std::string validCursorLifecycle =
        "int obj = NativeMenu::Alloc();\n"
        "if (!obj) { F8ReturnFlagsToGame(); return NativeMenu::Menu{ 0 }; }\n"
        "if (isFlags) { F8AcquireCursorVisibility(); }\n";
    Expect(ValidateSpawnCursorLifecycle(validCursorLifecycle),
           "cursor validator must accept the hand-checked post-allocation fixture");
    const std::string cursorBeforeFailurePath =
        "int obj = NativeMenu::Alloc();\n"
        "if (isFlags) { F8AcquireCursorVisibility(); }\n"
        "if (!obj) { F8ReturnFlagsToGame(); return NativeMenu::Menu{ 0 }; }\n"
        "\n";
    Expect(!ValidateSpawnCursorLifecycle(cursorBeforeFailurePath),
           "mutation proof: cursor acquisition before allocation failure handling must be rejected");

    const std::string validGapReject =
        "const float offsetInCell = 0.0f;\n"
        "if (offsetInCell >= tabW) return;\n"
        "const char* tabName = F8TabName(0);\n"
        "g_f7Tab = 0;\n"
        "F7_BuildRows(F7_MENU_FLAGS);\n";
    Expect(ValidateGapRejectBeforeTabUse(validGapReject, true),
           "gap validator must accept the hand-checked reject-before-use fixture");
    const std::string lateGapReject =
        "const float offsetInCell = 0.0f;\n"
        "const char* tabName = F8TabName(0);\n"
        "g_f7Tab = 0;\n"
        "F7_BuildRows(F7_MENU_FLAGS);\n"
        "if (offsetInCell >= tabW) return;\n";
    Expect(!ValidateGapRejectBeforeTabUse(lateGapReject, true),
           "mutation proof: gap rejection after tab selection/use must be rejected");
    const std::string removedGapReject =
        "const float offsetInCell = 0.0f;\n"
        "const char* tabName = F8TabName(0);\n"
        "g_f7Tab = 0;\n"
        "F7_BuildRows(F7_MENU_FLAGS);\n";
    Expect(!ValidateGapRejectBeforeTabUse(removedGapReject, true),
           "mutation proof: removed gap rejection must be rejected");
}

void TestTask6AddressLedger() {
    Expect(FFX_PREFERRED_IMAGE_BASE == 0x00400000u,
           "preferred FFX image base must remain 0x00400000");
    Expect(RVA_FFX_DEBUG_FLAGS == 0x00D2A8F8u,
           "Debug base must use the UnX RVA directly");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_DEBUG_FLAGS == 0x0112A8F8u,
           "Debug preferred VA must be 0x0112A8F8");
    Expect(RVA_FFX_PARTY_STRUCT_BASE == 0x00D32060u,
           "party structure base RVA must match offline evidence");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_PARTY_STRUCT_BASE == 0x01132060u,
           "party structure preferred VA must match offline evidence");
    Expect(RVA_FFX_PARTY_IN_PARTY_BASE == 0x00D32088u,
           "slot-zero in_party RVA must match offline evidence");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_PARTY_IN_PARTY_BASE == 0x01132088u,
           "slot-zero in_party preferred VA must match offline evidence");
    Expect(RVA_FFX_BATTLE_ACTIVE_FLAG == 0x00D2A8E0u,
           "battle-active RVA must remain exact");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_BATTLE_ACTIVE_FLAG == 0x0112A8E0u,
           "battle-active preferred VA must remain exact");
    Expect(RVA_FFX_BATTLE_PARTICIPATION == 0x01F10EA0u,
           "participation RVA must use the UnX module-relative value directly");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_BATTLE_PARTICIPATION == 0x02310EA0u,
           "participation preferred VA must match offline evidence");
    Expect(RVA_FFX_AP_EARN == 0x01F10EC4u,
           "AP-earned RVA must use the UnX module-relative value directly");
    Expect(FFX_PREFERRED_IMAGE_BASE + RVA_FFX_AP_EARN == 0x02310EC4u,
           "AP-earned preferred VA must match offline evidence");
    Expect(FFX_DEBUG_STRUCT_SIZE == 0x20u,
           "Debug structure size must remain 32 bytes");

    const uint32_t debugOffsets[] = {
        FFX_DEBUG_INVINCIBLE_ENEMIES_OFFSET,
        FFX_DEBUG_INVINCIBLE_PARTY_OFFSET,
        FFX_DEBUG_ALWAYS_OVERDRIVE_OFFSET,
        FFX_DEBUG_ALWAYS_CRITICAL_OFFSET,
        FFX_DEBUG_ALWAYS_DEAL_1_OFFSET,
        FFX_DEBUG_ALWAYS_DEAL_10000_OFFSET,
        FFX_DEBUG_ALWAYS_DEAL_99999_OFFSET,
        FFX_DEBUG_ALWAYS_RARE_REWARD_OFFSET,
        FFX_DEBUG_AP_100X_OFFSET,
        FFX_DEBUG_GIL_100X_OFFSET,
        FFX_DEBUG_PERMANENT_SENSOR_OFFSET,
    };
    const uint32_t expectedOffsets[] = {
        0x00u, 0x01u, 0x14u, 0x15u, 0x16u, 0x17u,
        0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Du,
    };
    bool exactOffsets = true;
    for (size_t i = 0; i < sizeof(debugOffsets) / sizeof(debugOffsets[0]); ++i) {
        exactOffsets = exactOffsets && debugOffsets[i] == expectedOffsets[i];
    }
    Expect(exactOffsets, "all eleven verified Debug byte offsets must be exact");
    Expect(FFX_PARTY_IN_PARTY_OFFSET == 0x28u,
           "in_party must be structure offset 0x28");
    Expect(FFX_PARTY_SLOT_STRIDE == 148u,
           "party character stride must remain 148 bytes");
    Expect(FFX_PARTY_SLOT_COUNT == 7u,
           "AP processing must cover exactly seven party slots");
}

void TestTask6BoosterSourceContracts() {
    std::string header;
    std::string source;
    std::string addresses;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.h"), header),
           "UnX booster header must be readable for Task 6 contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), source),
           "UnX booster source must be readable for Task 6 contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("shared\\ffx_addresses.h"), addresses),
           "address ledger must be readable for Task 6 contracts");

    const char* apiTokens[] = {
        "using BoosterLogFn = void (*)(const char*)",
        "bool StartUnXBoosterHook(", "uintptr_t moduleBase", "BoosterLogFn log",
        "void NotifyUnXBoosterPresentProducer(bool ready, bool terminalFailure)",
        "void UnXBoosterFrameTick(uint32_t nowMs)", "void RequestUnXBoosterStop()",
    };
    for (const char* token : apiTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "Task 6 public adapter API must contain %s", token);
        ExpectSourceIncludes(header, token, message);
    }

    const std::string staleSurface = header + source + addresses;
    const char* staleTokens[] = {
        "SetTimer", "KillTimer", "TIMERPROC", "BoosterTimerProc",
        "DWORD* participation", "DWORD* apEarn", "RVA_FFX_PERMANENT_SENSOR",
        "0x0092A8F8", "0x01B10EA0", "0x01B10EC4",
        "FindWindow", "ApplyPlayableSeymour", "PatchAntiSeymour",
    };
    for (const char* token : staleTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "Task 6 runtime surfaces must remove stale token %s", token);
        ExpectSourceExcludes(staleSurface, token, message);
    }

    const char* profileTokens[] = {
        "ParseExecutableIdentity", "ValidateImageRange", "VirtualQuery",
        "MEM_COMMIT", "MEM_IMAGE", "PAGE_GUARD", "PAGE_NOACCESS",
        "IsReadableProtection", "IsWritableProtection", "AllocationBase",
    };
    for (const char* token : profileTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "validated adapter must contain profile/span token %s", token);
        ExpectSourceIncludes(source, token, message);
    }
    const std::string rawByteWrite = BoundedSourceSection(
        source, "WriteResult GuardedWriteByte(", "bool GuardedStoreByte(");
    Expect(!rawByteWrite.empty() && rawByteWrite.find("VirtualProtect(") == std::string::npos,
           "ordinary booster data writes must never change page protection");
    ExpectSourceIncludes(source, "WriteEffect::MayHaveChanged",
                         "guarded byte writes must truthfully report possibly-mutated state");
    ExpectSourceExcludes(source, "WriteEffect::Verified",
                         "a raw guarded store is not verified until readback");

    const char* ownedKeys[] = {
        "boosters.permanent_sensor", "cheats.invincible_party",
        "cheats.invincible_enemies", "cheats.always_overdrive",
        "cheats.always_critical", "cheats.damage_value",
        "cheats.always_rare_drop",
    };
    for (const char* key : ownedKeys) {
        char message[176] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "owned Debug binding must include canonical key %s", key);
        ExpectSourceIncludes(source, key, message);
    }
    ExpectSourceIncludes(source, "kOwnedDebugFieldCount = 7",
                         "generic runtime owner must bind exactly seven persistent Debug bytes");
    ExpectSourceIncludes(source, "std::array<DebugBinding, kOwnedDebugFieldCount>",
                         "seven generic Debug bindings must share one owned-byte adapter table");
    Expect(ValidateExactDebugBindingPairs(source),
            "all seven generic canonical Debug keys must bind to their exact recovered offsets");
    ExpectSourceIncludes(source, "ResolveF8Flag(*binding.flag)",
                         "each Debug binding must resolve its catalog gate in the due frame");
    ExpectSourceIncludes(source, "UpdateOwnedByte",
                         "Debug writes must pass through the runtime ownership state machine");
    ExpectSourceIncludes(source, "PublishF8RuntimeStatus",
                         "runtime consumer must publish row-specific availability/readback");
    ExpectSourceIncludes(source,
                         "[f8-runtime] key=%s effective=%d source=%s state=%s readback=%02X",
                         "runtime edge logs must use the stable canonical anchor");

    const SourceBlock ap = SourceFunctionBody(source, "void ApplyEntirePartyAp()");
    Expect(ap.Valid() && SourceTokensInOrder(ap.body, {
               "ResolveF8Flag(*g_apFlag)", "g_adapter.battleActive",
               "inParty", "participation", "HasSeededBattleParticipant",
               "ComputeApUpdates", "earn", "write", "readback",
           }),
           "AP updater must resolve once, gate, snapshot, compute, write, and read back in order");
    Expect(ap.Valid() && ValidateExactApLoopStructure(ap.body) &&
               ap.body.find("uint8_t") != std::string::npos &&
               ap.body.find("DWORD") == std::string::npos &&
               ap.body.find("* 2") == std::string::npos,
           "AP updater must contain one exact seven-slot snapshot, participation write, earn write, and readback loop");
    const SourceBlock outOfBattle = SourceBlockAfterToken(ap.body, "if (battleActive == 0)");
    const SourceBlock noSeed = SourceBlockAfterToken(
        ap.body, "if (!FfxHooks::F8Runtime::HasSeededBattleParticipant");
    Expect(ap.Valid() && outOfBattle.Valid() && ValidateIdleApGatePublication(outOfBattle.body),
           "ordinary out-of-battle AP idle must stay Available without a persistent failure");
    Expect(ap.Valid() && noSeed.Valid() && ValidateIdleApGatePublication(noSeed.body),
           "ordinary no-seeded-participant AP idle must stay Available without a persistent failure");
    ExpectSourceExcludes(source, "g_apOriginal",
                         "battle scratch arrays must never be retained for restoration");

    const SourceBlock requestStop = SourceFunctionBody(
        source, "void FfxHooks::RequestUnXBoosterStop()");
    Expect(requestStop.Valid() &&
               requestStop.body.find("compare_exchange") != std::string::npos &&
               requestStop.body.find("Log") == std::string::npos &&
               requestStop.body.find("PublishF8RuntimeStatus") == std::string::npos &&
               requestStop.body.find("read") == std::string::npos &&
               requestStop.body.find("write") == std::string::npos &&
               requestStop.body.find("Wait") == std::string::npos &&
               requestStop.body.find("Sleep") == std::string::npos,
           "detach stop request must be a lock-free state transition with no I/O, status, or wait");
    const SourceBlock tick = SourceFunctionBody(source, "void FfxHooks::UnXBoosterFrameTick(uint32_t nowMs)");
    Expect(tick.Valid() && CountSourceToken(tick.body, "TryEnterFrame") == 1 &&
               CountSourceToken(source, "LeaveFrame") == 1 &&
               tick.body.find("TryBeginTick(&g_tickGate, nowMs, 33u)") != std::string::npos,
           "frame admission must be scope-balanced once and cadence-throttled to 33 ms");
}

void TestTask6DllmainIntegrationContracts() {
    std::string source;
    std::string boosterSource;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for Task 6 integration contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), boosterSource),
           "UnX booster source must be readable for Task 6 integration contracts");

    const SourceBlock install = SourceFunctionBody(source, "static void InstallHooks()");
    const size_t start = install.body.find("StartUnXBoosterHook(g_base, LogLine)");
    const size_t dashboard = install.body.find("dashboardEnabled");
    Expect(install.Valid() && start != std::string::npos && dashboard != std::string::npos &&
               start < dashboard && CountSourceToken(install.body, "StartUnXBoosterHook(") == 1,
            "normal InstallHooks must start the booster adapter once, before and outside dashboard setup");
    Expect(install.Valid() && ValidateAdapterOwnsPostStartStatus(source, install.body),
           "adapter Start status must not be clobbered by a blanket UnX publication");
    Expect(install.Valid() && SourceTokensInOrder(install.body, {
               "if (f8RuntimeAdapterReady)", "InstallAuroraD3D11Overlay()",
               "InterlockedExchange(&g_auroraD3DRenderEnabled, 1)",
               "StartAuroraD3DLatePresentFallback()",
           }),
           "runtime-only Present arming must keep the guarded render pump enabled while rows are pending");

    const SourceBlock render = SourceFunctionBody(
        source, "static void AuroraD3DRender(IDXGISwapChain* swapChain)");
    Expect(render.Valid() && SourceTokensInOrder(render.body, {
               "UnXBoosterFrameTick(GetTickCount())",
               "if (!actorVisible && !menuOpen && !speedIndicatorVisible) return",
           }),
           "real guarded Present rendering must tick F8 before visual-overlay early-outs");

    const SourceBlock hookPresent = SourceFunctionBody(
        source, "static bool AuroraD3DHookPresentFromVtable(void** vtable, const char* reason)");
    Expect(hookPresent.Valid() && ValidatePresentReadyPublication(hookPresent.body),
           "producer Ready must publish only after the real Present detour assignment succeeds");
    const SourceBlock installD3d = SourceFunctionBody(
        source, "static bool InstallAuroraD3D11Overlay()");
    const SourceBlock createShim = SourceBlockAfterToken(
        source, "static HRESULT WINAPI AuroraD3D11CreateDeviceAndSwapChainShim(");
    Expect(installD3d.Valid() && createShim.Valid() &&
               installD3d.body.find("NotifyUnXBoosterPresentProducer(true") == std::string::npos &&
               createShim.body.find("NotifyUnXBoosterPresentProducer(true") == std::string::npos,
           "CreateDevice arm success must never be misreported as an actual Present-ready producer");

    const SourceBlock late = SourceFunctionBody(
        source, "static DWORD WINAPI AuroraD3DLatePresentFallbackThreadProc(LPVOID)");
    const SourceBlock terminalArbiter = SourceFunctionBody(
        source, "static bool TryPublishAuroraD3DPresentTerminal()");
    const SourceBlock resultPublisher = SourceBlockAfterToken(
        source, "static void PublishAuroraD3DPresentResult(");
    Expect(hookPresent.Valid() && late.Valid() && terminalArbiter.Valid() &&
               resultPublisher.Valid() && ValidatePresentArbiterIntegration(
                   source, hookPresent.body, late.body, terminalArbiter.body,
                   resultPublisher.body),
           "physical Present installation and sticky UnX terminal publication must use the portable arbiter");
    Expect(late.Valid() && ValidateFallbackAttemptGate(late.body),
           "late fallback must publish terminal failure only after a completed unsuccessful attempt");
    const SourceBlock lateStart = SourceFunctionBody(
        source, "static bool StartAuroraD3DLatePresentFallback()");
    Expect(lateStart.Valid() &&
               lateStart.body.find("TryPublishAuroraD3DPresentTerminal()") != std::string::npos &&
               lateStart.body.find("NotifyUnXBoosterPresentProducer(false, true)") == std::string::npos,
           "failure to arm the late fallback thread must publish immediate terminal failure");
    Expect(install.Valid() &&
               CountSourceToken(install.body, "TryPublishAuroraD3DPresentTerminal()") == 1 &&
               CountSourceToken(install.body,
                                "NotifyUnXBoosterPresentProducer(false, true)") == 1 &&
               CountSourceToken(source,
                                "NotifyUnXBoosterPresentProducer(false, true)") == 2,
           "all PolyHook terminal paths must use the arbiter; only no-PolyHook may notify directly");
    const SourceBlock overlayStart = SourceFunctionBody(
        source, "static bool StartAuroraOverlayIfEnabled()");
    Expect(overlayStart.Valid() && SourceTokensInOrder(overlayStart.body, {
               "InstallAuroraD3D11Overlay()", "StartAuroraD3DLatePresentFallback()",
               "return true",
           }) && install.Valid() && SourceTokensInOrder(install.body, {
               "if (InstallAuroraD3D11Overlay())", "f8PresentProducerArmed = true",
               "StartAuroraD3DLatePresentFallback()",
           }) && overlayStart.body.find("return StartAuroraD3DLatePresentFallback") ==
                    std::string::npos &&
               install.body.find(
                   "f8PresentProducerArmed = StartAuroraD3DLatePresentFallback") ==
                   std::string::npos,
           "CreateDevice detour success must remain the shared physical arm even if UnX fallback arming fails");

    const SourceBlock startAdapter = SourceFunctionBody(
        boosterSource, "bool FfxHooks::StartUnXBoosterHook(uintptr_t moduleBase, BoosterLogFn log)");
    const SourceBlock notifyAdapter = SourceFunctionBody(
        boosterSource,
        "void FfxHooks::NotifyUnXBoosterPresentProducer(bool ready, bool terminalFailure)");
    Expect(startAdapter.Valid() && notifyAdapter.Valid() &&
               ValidateUnsupportedAdapterStatusSticky(startAdapter.body, notifyAdapter.body),
           "unsupported-profile status must survive later producer notifications");

    const SourceBlock dllmain = SourceFunctionBody(
        source, "BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)");
    const std::string attach = BoundedSourceSection(
        dllmain.body, "case DLL_PROCESS_ATTACH:", "break;");
    const SourceBlock worker = SourceFunctionBody(
        source, "static DWORD WINAPI HooksWorkerThread(LPVOID)");
    Expect(dllmain.Valid() && !attach.empty() &&
               ValidateLoaderLockSafeAttachBody(attach),
           "DLL_PROCESS_ATTACH must only publish module state and bootstrap/close the worker handle");
    Expect(worker.Valid() && ValidateWorkerOwnsLoggingBoundary(worker.body),
           "HooksWorkerThread must open the log exactly once before logging, config, or install work");
    const std::string detach = BoundedSourceSection(
        dllmain.body, "case DLL_PROCESS_DETACH:", "break;");
    Expect(dllmain.Valid() && !detach.empty() && ValidateTask6DetachBody(detach),
           "DLL_PROCESS_DETACH must contain only lock-free runtime stop requests");
    ExpectSourceIncludes(source, "Dynamic FreeLibrary is unsupported",
                         "source must document unsupported hot unload explicitly");
    ExpectSourceIncludes(source, "Process termination discards process-owned state",
                         "source must document process-termination semantics explicitly");
}

void TestAuroraDeveloperHotkeyContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for Aurora developer-hotkey contracts");

    const SourceBlock explicitGate = SourceBlockAfterToken(
        source, "static bool AuroraDeveloperUiEnabledFromExplicitSources(");
    Expect(explicitGate.Valid() && SourceTokensInOrder(explicitGate.body, {
               "environmentEnabled", "overlayFileEnabled", "overlayConfigEnabled",
               "d3dFileEnabled", "sniffFileEnabled",
           }) &&
               explicitGate.body.find("dashboardEnabled") == std::string::npos &&
               explicitGate.body.find("inGameMenuEnabled") == std::string::npos &&
               explicitGate.body.find("nativeMenuEnabled") == std::string::npos,
           "Aurora developer enablement must use only explicit overlay environment, config, and file sources");

    const SourceBlock hotkey = SourceFunctionBody(
        source, "static bool AuroraDeveloperHotkeyPressed(int virtualKey)");
    const size_t sampledEdge = hotkey.body.find("AuroraKeyPressed(virtualKey)");
    const size_t developerGate = hotkey.body.find("AuroraDeveloperUiEnabled()");
    const size_t control = hotkey.body.find("GetAsyncKeyState(VK_CONTROL)");
    const size_t alt = hotkey.body.find("GetAsyncKeyState(VK_MENU)");
    const size_t shift = hotkey.body.find("GetAsyncKeyState(VK_SHIFT)");
    Expect(hotkey.Valid() && sampledEdge != std::string::npos &&
               developerGate != std::string::npos && control != std::string::npos &&
               alt != std::string::npos && shift != std::string::npos &&
               sampledEdge < developerGate && developerGate < control && control < alt && alt < shift &&
               hotkey.body.find("pressed && developerEnabled && controlDown && altDown && !shiftDown") !=
                   std::string::npos,
           "Aurora developer hotkeys must sample the edge before requiring enabled Ctrl+Alt with Shift up");

    const SourceBlock start = SourceFunctionBody(
        source, "static bool StartAuroraOverlayIfEnabled()");
    Expect(start.Valid() &&
               SourceTokensInOrder(start.body, {
                   "const bool auroraOverlayFileEnabled", "const bool configEnabled",
                   "const bool d3dFileEnabled", "const bool sniffFileEnabled",
                   "AuroraDeveloperUiEnabledFromExplicitSources(",
                   "InterlockedExchange(&g_auroraDeveloperUiEnabled",
               }),
           "Aurora startup must publish the explicit developer gate before menu-only readiness can continue");
    static const char kExactDeveloperGateCall[] =
        "constboolauroraDeveloperUiEnabled=AuroraDeveloperUiEnabledFromExplicitSources("
        "envEnabled,auroraOverlayFileEnabled,configEnabled,d3dFileEnabled,sniffFileEnabled);";
    Expect(start.Valid() &&
               CompactSourceCode(start.body).find(kExactDeveloperGateCall) != std::string::npos,
           "Aurora startup must call the explicit developer gate with only the five named Aurora sources");

    const SourceBlock d3d = SourceFunctionBody(
        source, "static void AuroraD3DRender(IDXGISwapChain* swapChain)");
    const SourceBlock gdi = SourceFunctionBody(
        source, "static DWORD WINAPI AuroraOverlayThreadProc(LPVOID)");
    Expect(CountSourceToken(source, "AuroraKeyPressed(VK_F9)") == 0 &&
               CountSourceToken(source, "AuroraKeyPressed(VK_F10)") == 0,
           "plain F9/F10 must have no direct AuroraKeyPressed owner");
    Expect(d3d.Valid() && CountSourceToken(d3d.body, "AuroraDeveloperHotkeyPressed(VK_F9)") == 1 &&
               CountSourceToken(d3d.body, "AuroraDeveloperHotkeyPressed(VK_F10)") == 1,
           "D3D Aurora toggles must use the explicit developer chord helper");
    Expect(gdi.Valid() && CountSourceToken(gdi.body, "AuroraDeveloperHotkeyPressed(VK_F9)") == 1 &&
               CountSourceToken(gdi.body, "AuroraDeveloperHotkeyPressed(VK_F10)") == 1,
           "GDI Aurora toggles must use the explicit developer chord helper");
    Expect(d3d.Valid() && SourceTokensInOrder(d3d.body, {
               "UnXBoosterFrameTick(GetTickCount())", "NativeMenu_PresentTick()",
               "InGameMenuHandleInput()", "AuroraDeveloperHotkeyPressed(VK_F9)",
           }),
           "Aurora developer input must preserve the shared Present/native-menu producer order");

    const SourceBlock rowMapper = SourceFunctionBody(
        source,
        "static FfxHooks::F8Ui::SwitchboardRowMapper InGameMenuSwitchboardRows()");
    const SourceBlock rowCount = SourceFunctionBody(source, "static int InGameMenuRowCount()");
    const SourceBlock switchboard = SourceFunctionBody(
        source, "static void InGameMenuActivateSelection()");
    const SourceBlock draw = SourceFunctionBody(
        source, "static bool InGameMenuDraw(HDC hdc, const RECT& rc)");
    const SourceBlock icon = SourceFunctionBody(
        source,
        "static int InGameMenuIconForRow(FfxHooks::F8Ui::SwitchboardRowKind kind, const char* file)");
    Expect(rowMapper.Valid() &&
               rowMapper.body.find("AuroraDeveloperUiEnabled()") != std::string::npos &&
               rowMapper.body.find("g_ingameMenuPluginCount") != std::string::npos &&
               rowCount.Valid() && rowCount.body.find("InGameMenuSwitchboardRows().Count()") !=
                   std::string::npos,
           "one developer-aware row mapper must own switchboard row count");
    Expect(switchboard.Valid() && SourceTokensInOrder(switchboard.body, {
               "const FfxHooks::F8Ui::SwitchboardRow mapped =",
               "InGameMenuSwitchboardRows().At(row)",
               "SwitchboardRowKind::AuroraActor",
               "if (!AuroraDeveloperUiEnabled())",
               "InterlockedCompareExchange(&g_auroraActorOverlayEnabled",
               "SwitchboardRowKind::AuroraDetail",
               "if (!AuroraDeveloperUiEnabled())",
               "InterlockedCompareExchange(&g_auroraOverlayDetail",
               "SwitchboardRowKind::Refresh",
               "InGameMenuRefreshPlugins()",
               "SwitchboardRowKind::Plugin",
               "mapped.pluginIndex",
           }) &&
               switchboard.body.find("row == 0") == std::string::npos &&
               switchboard.body.find("row == 1") == std::string::npos &&
               switchboard.body.find("row - 3") == std::string::npos,
           "switchboard activation must consume the one row mapper so hidden Aurora rows have no activation target");
    Expect(draw.Valid() &&
               draw.body.find("InGameMenuSwitchboardRows().At(row)") != std::string::npos &&
               draw.body.find("SwitchboardStaticLabel(mapped.kind)") != std::string::npos &&
               draw.body.find("mapped.pluginIndex") != std::string::npos &&
               draw.body.find("row - 3") == std::string::npos,
           "switchboard labels, Refresh, and plugin rows must consume the same mapped identity");
    Expect(icon.Valid() &&
               icon.body.find("SwitchboardRowKind::AuroraActor") != std::string::npos &&
               icon.body.find("SwitchboardRowKind::AuroraDetail") != std::string::npos &&
               icon.body.find("SwitchboardRowKind::Refresh") != std::string::npos,
           "switchboard icons must follow mapped row kind instead of fixed physical indexes");
}

void TestTask6F7ArbitrationContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for Task 6 F7 arbitration contracts");
    const SourceBlock lever = SourceFunctionBody(
        source, "static void F7_LeverApply(NativeMenu::ActionId act, int val)");
    const SourceBlock battle = SourceBlockAfterToken(
        lever.body, "case NativeMenu::ACT_BATTLE_CHEATS:");
    Expect(lever.Valid() && battle.Valid() && ValidateF7BattleCheatsArbitration(battle.body),
           "F7 battle-cheat lever must edit catalog desired state and copy effective readback");

    const SourceBlock hydrate = SourceFunctionBody(
        source, "static void HydrateNativeMenuBattleCheats()");
    const SourceBlock spawn = SourceFunctionBody(
        source, "static NativeMenu::Menu SpawnHydratedNativeMenu()");
    Expect(hydrate.Valid() && SourceTokensInOrder(hydrate.body, {
               "FindF8Flag(\"cheats.invincible_party\")", "ResolveF8Flag",
               "ACT_BATTLE_CHEATS", "g_rowValue", "effective.value",
           }) && spawn.Valid() && SourceTokensInOrder(spawn.body, {
               "HydrateNativeMenuBattleCheats()", "NativeMenu::SpawnMenu()",
           }) && CountSourceToken(source, "NativeMenu::SpawnMenu()") == 1,
           "every hub spawn/reopen must hydrate invincible-party effective state first");
}

void TestTask6IdleApGateRemainsEditable() {
    FakeState state;
    Configure(state, "[boosters]\nentire_party_earns_ap=false\n");
    const FfxHooks::F8FlagSpec* ap =
        FfxHooks::FindF8Flag("boosters.entire_party_earns_ap");
    Expect(ap != nullptr, "AP row must exist for ordinary-idle editability coverage");
    if (!ap) return;

    Expect(FfxHooks::PublishF8RuntimeStatus(
               ap->gate.canonicalKey, F8RuntimeAvailability::Available, true, false),
           "out-of-battle idle publication must address the AP row");
    const FfxHooks::F8EditResult enable = FfxHooks::SetF8FlagValue(*ap, true);
    Expect(enable.code == F8EditCode::Saved && enable.effective.value,
           "out-of-battle Available idle must allow enabling the AP row");

    Expect(FfxHooks::PublishF8RuntimeStatus(
               ap->gate.canonicalKey, F8RuntimeAvailability::Available, true, false),
           "no-seed idle publication must address the AP row");
    const FfxHooks::F8EditResult disable = FfxHooks::SetF8FlagValue(*ap, false);
    Expect(disable.code == F8EditCode::Saved && !disable.effective.value,
           "no-seed Available idle must allow disabling the AP row");
}

void TestTask6UnsupportedAdapterStatusRemainsSticky() {
    FakeState state;
    Configure(state, "[boosters]\nentire_party_earns_ap=false\n");
    const FfxHooks::F8FlagSpec* ap =
        FfxHooks::FindF8Flag("boosters.entire_party_earns_ap");
    Expect(ap != nullptr, "AP row must exist for unsupported-status retention coverage");
    if (!ap) return;

    Expect(FfxHooks::PublishF8RuntimeStatus(
               ap->gate.canonicalKey, F8RuntimeAvailability::UnsupportedBuild, false, false),
           "unsupported adapter publication must address the AP row");
    const FfxHooks::F8EditResult edit = FfxHooks::SetF8FlagValue(*ap, true);
    const FfxHooks::F8RuntimeStatus after = FfxHooks::GetF8RuntimeStatus(*ap);
    Expect(edit.code == F8EditCode::RejectedUnavailable && state.persistCalls == 0,
           "unsupported adapter status must reject persistence");
    Expect(edit.runtime.availability == F8RuntimeAvailability::UnsupportedBuild &&
               after.availability == F8RuntimeAvailability::UnsupportedBuild,
           "unsupported adapter status must remain sticky through catalog resolution and edit");
}

void TestTask6SourceValidatorMutationPressure() {
    std::string boosterSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), boosterSource),
           "UnX booster source must be readable for Task 6 mutation pressure");
    const std::string validDetach =
        "case DLL_PROCESS_DETACH:\n"
        "FfxHooks::EquipmentWorkshop::RequestStop();\n"
        "FfxHooks::NativePorts::RequestStop();\n"
        "FfxHooks::NativeLanguage::RequestStop();\n"
        "FfxHooks::SinAi::RequestStop();\n"
        "FfxHooks::FmvSpeed::RequestStop();\n"
        "FfxHooks::Fastload::RequestFastloadStop();\n"
        "FfxHooks::RequestNovaSuperDamageStop();\n"
        "FfxHooks::RequestSeymourBattleStop();\n"
        "FfxHooks::F7_RequestStop();\n"
        "FfxHooks::F7AiSwap_RequestStop();\n"
        "FfxHooks::RequestSpeedHackStop();\n"
        "FfxHooks::RequestDialogSkipStop();\n"
        "FfxHooks::RequestUnXBoosterStop();\n";
    const std::string heavyDetach =
        validDetach + "RemoveHooks();\n";
    Expect(ValidateTask6DetachBody(validDetach),
           "detach validator must accept the complete lock-free stop fixture");
    Expect(!ValidateTask6DetachBody(heavyDetach),
           "mutation proof: full teardown under loader lock must be rejected");

    std::string aiObserverSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\F7AiSwap.cpp"), aiObserverSource),
           "Monster AI observer source must be readable for detach safety coverage");
    const SourceBlock aiStop = SourceFunctionBody(
        aiObserverSource, "void F7AiSwap_RequestStop()");
    Expect(aiStop.Valid() &&
               aiStop.body.find("MonsterAiObserver::RequestStop(&g_lifecycle)") != std::string::npos &&
               aiStop.body.find("Sleep(") == std::string::npos &&
               aiStop.body.find("RemoveDetourPair(") == std::string::npos,
           "Monster AI detach request must remain lock-free and removal-free");

    const auto validateBoundedAttach = [](const std::string& fixture) {
        const std::string body = BoundedSourceSection(
            fixture, "case DLL_PROCESS_ATTACH:", "break;");
        return !body.empty() && ValidateLoaderLockSafeAttachBody(body);
    };
    const auto validateBoundedDetach = [](const std::string& fixture) {
        const std::string body = BoundedSourceSection(
            fixture, "case DLL_PROCESS_DETACH:", "break;");
        return !body.empty() && ValidateTask6DetachBody(body);
    };

    const std::string validAttach =
        "case DLL_PROCESS_ATTACH:\n"
        "g_module = hMod;\n"
        "OutputDebugStringA(\"attach enter\");\n"
        "{\n"
        "DWORD tid = 0;\n"
        "HANDLE thread = CreateThread(nullptr, 0, HooksWorkerThread, nullptr, 0, &tid);\n"
        "if (thread) { CloseHandle(thread); }\n"
        "else { OutputDebugStringA(\"worker create failed\"); }\n"
        "}\n";
    Expect(ValidateLoaderLockSafeAttachBody(validAttach),
           "attach validator must accept only module state, debug signaling, and worker bootstrap");
    std::string openLogInAttach = validAttach;
    Expect(ReplaceFirstSourceToken(openLogInAttach, "g_module = hMod;",
                                   "g_module = hMod; OpenLog();"),
           "attach OpenLog mutation setup must move log opening under loader lock");
    Expect(!ValidateLoaderLockSafeAttachBody(openLogInAttach),
           "mutation proof: OpenLog under DLL_PROCESS_ATTACH must be rejected");
    std::string earlyLogInAttach = validAttach;
    Expect(ReplaceFirstSourceToken(
               earlyLogInAttach, "g_module = hMod;",
               "g_module = hMod; EarlyLogLine(\"loader-lock file write\");"),
           "attach EarlyLogLine mutation setup must move early file logging under loader lock");
    Expect(!ValidateLoaderLockSafeAttachBody(earlyLogInAttach),
           "mutation proof: EarlyLogLine under DLL_PROCESS_ATTACH must be rejected");
    std::string normalLogInAttach = validAttach;
    Expect(ReplaceFirstSourceToken(normalLogInAttach, "g_module = hMod;",
                                   "g_module = hMod; Log(\"loader-lock CRT write\");"),
           "attach Log mutation setup must move CRT logging under loader lock");
    Expect(!ValidateLoaderLockSafeAttachBody(normalLogInAttach),
           "mutation proof: Log under DLL_PROCESS_ATTACH must be rejected");
    const std::string attachCommentBreak =
        validAttach +
        "// break; hides unsafe work from a naive case bound\n"
        "OpenLog();\n"
        "break;\n";
    Expect(!validateBoundedAttach(attachCommentBreak),
           "mutation proof: comment break must not hide OpenLog in DLL_PROCESS_ATTACH");
    const std::string attachStringBreak =
        validAttach +
        "\"break;\";\n"
        "CreateFileA(\"unsafe\", 0, 0, nullptr, 0, 0, nullptr);\n"
        "break;\n";
    Expect(!validateBoundedAttach(attachStringBreak),
           "mutation proof: string-literal break must not hide CreateFile in DLL_PROCESS_ATTACH");
    const std::string detachCommentBreak =
        validDetach +
        "// break; hides teardown from a naive case bound\n"
        "RemoveHooks();\n"
        "break;\n";
    Expect(!validateBoundedDetach(detachCommentBreak),
           "mutation proof: comment break must not hide teardown in DLL_PROCESS_DETACH");
    const std::string detachStringBreak =
        validDetach +
        "\"break;\";\n"
        "RemoveHooks();\n"
        "break;\n";
    Expect(!validateBoundedDetach(detachStringBreak),
           "mutation proof: string-literal break must not hide teardown in DLL_PROCESS_DETACH");

    const std::string validWorker =
        "OpenLog();\n"
        "EarlyLogLine(\"worker start\");\n"
        "if (FfxHooks::Config::Load()) { Log(\"config loaded\"); }\n"
        "InstallHooks();\n"
        "return 0;\n";
    Expect(ValidateWorkerOwnsLoggingBoundary(validWorker),
           "worker validator must accept one OpenLog before every deferred file-backed path");
    std::string workerWithoutOpenLog = validWorker;
    Expect(ReplaceFirstSourceToken(workerWithoutOpenLog, "OpenLog();\n", ""),
           "worker OpenLog omission mutation setup must remove the logging boundary");
    Expect(!ValidateWorkerOwnsLoggingBoundary(workerWithoutOpenLog),
           "mutation proof: HooksWorkerThread without OpenLog must be rejected");
    std::string workerWithLateOpenLog = validWorker;
    Expect(ReplaceFirstSourceToken(
               workerWithLateOpenLog,
               "OpenLog();\nEarlyLogLine(\"worker start\");",
               "EarlyLogLine(\"worker start\");\nOpenLog();"),
           "worker late-OpenLog mutation setup must cross the first file-backed log call");
    Expect(!ValidateWorkerOwnsLoggingBoundary(workerWithLateOpenLog),
           "mutation proof: OpenLog after early logging must be rejected");
    const std::string workerAfterSleep = "Sleep(1);\n" + validWorker;
    Expect(!ValidateWorkerOwnsLoggingBoundary(workerAfterSleep),
           "mutation proof: worker Sleep before OpenLog must be rejected");
    const std::string workerAfterCreateFile =
        "CreateFileA(\"unsafe\", 0, 0, nullptr, 0, 0, nullptr);\n" + validWorker;
    Expect(!ValidateWorkerOwnsLoggingBoundary(workerAfterCreateFile),
           "mutation proof: worker CreateFile before OpenLog must be rejected");
    const std::string workerAfterEnv =
        "EnvInt(\"FFXHOOKS_INSTALL_DELAY_MS\", 0);\n" + validWorker;
    Expect(!ValidateWorkerOwnsLoggingBoundary(workerAfterEnv),
           "mutation proof: worker EnvInt before OpenLog must be rejected");
    const std::string conditionalWorkerOpen =
        "if (enabled) { OpenLog(); }\n"
        "EarlyLogLine(\"worker start\");\n"
        "InstallHooks();\n"
        "return 0;\n";
    Expect(!ValidateWorkerOwnsLoggingBoundary(conditionalWorkerOpen),
           "mutation proof: conditional worker OpenLog must be rejected");
    const std::string unreachableWorkerOpen =
        "return 0;\n"
        "OpenLog();\n"
        "EarlyLogLine(\"worker start\");\n";
    Expect(!ValidateWorkerOwnsLoggingBoundary(unreachableWorkerOpen),
           "mutation proof: unreachable worker OpenLog after return must be rejected");
    const std::string duplicateWorkerOpen = "OpenLog();\n" + validWorker;
    Expect(!ValidateWorkerOwnsLoggingBoundary(duplicateWorkerOpen),
           "mutation proof: duplicate worker OpenLog must remain rejected");

    const std::string validReady =
        "const bool hooked = g_auroraD3DPresentDetour->hook();\n"
        "const auto result = CompletePresentHookInstall(&arbiter, hooked);\n"
        "PublishAuroraD3DPresentResult(result);\n";
    const std::string earlyReady =
        "PublishAuroraD3DPresentResult(result);\n"
        "const bool hooked = g_auroraD3DPresentDetour->hook();\n"
        "const auto result = CompletePresentHookInstall(&arbiter, hooked);\n";
    Expect(ValidatePresentReadyPublication(validReady),
           "Present-ready validator must accept publication after the detour assignment");
    Expect(!ValidatePresentReadyPublication(earlyReady),
           "mutation proof: producer Ready before the Present detour result must be rejected");

    const std::string validIdle =
        "PublishApResult(gate, F8RuntimeAvailability::Available, false,\n"
        "FailureReason::None, EdgeState::Pending, 0);\n";
    const std::string unavailableIdle =
        "PublishApResult(gate, F8RuntimeAvailability::Pending, false,\n"
        "FailureReason::BattleGate, EdgeState::Pending, 0);\n";
    Expect(ValidateIdleApGatePublication(validIdle),
           "AP idle validator must accept Available/no-failure publication");
    Expect(!ValidateIdleApGatePublication(unavailableIdle),
           "mutation proof: Pending/BattleGate ordinary idle publication must be rejected");

    const std::string validOwner =
        "StartUnXBoosterHook(g_base, LogLine);\n"
        "PublishResolvedF8Status(\"arena_plus.compose_f7\", ProducerUnavailable);\n"
        "AddVectoredExceptionHandler";
    const std::string blanketOwner =
        "StartUnXBoosterHook(g_base, LogLine);\n"
        "PublishResolvedF8Status(\"arena_plus.compose_f7\");\n"
        "PublishFutureUnXRowsUnavailable();\n"
        "AddVectoredExceptionHandler";
    Expect(ValidateAdapterOwnsPostStartStatus(validOwner, validOwner),
           "post-Start status validator must accept the compose-only fixture");
    Expect(!ValidateAdapterOwnsPostStartStatus(blanketOwner, blanketOwner),
           "mutation proof: blanket UnX status publication after Start must be rejected");

    const std::string guardedFallback =
        "bool attemptedPresentHook = false;\n"
        "if (running) { attemptedPresentHook = true; "
        "AuroraD3DHookPresentFromVtable(vtable, reason); }\n"
        "if (attemptedPresentHook) { TryPublishAuroraD3DPresentTerminal(); }\n";
    std::string unconditionalFallback = guardedFallback;
    Expect(ReplaceFirstSourceToken(
               unconditionalFallback, "if (attemptedPresentHook)", "if (true)"),
           "cancellation mutation setup must replace the attempt gate");
    Expect(ValidateFallbackAttemptGate(guardedFallback),
           "fallback validator must accept terminal publication gated by an actual attempt");
    Expect(!ValidateFallbackAttemptGate(unconditionalFallback),
           "mutation proof: cancellation before an attempt must not publish terminal");

    std::string swappedBindings = boosterSource;
    const bool swappedFirst = ReplaceFirstSourceToken(
        swappedBindings, "FFX_DEBUG_PERMANENT_SENSOR_OFFSET", "FFX_DEBUG_SWAP_SENTINEL");
    const bool swappedSecond = ReplaceFirstSourceToken(
        swappedBindings, "FFX_DEBUG_INVINCIBLE_PARTY_OFFSET",
        "FFX_DEBUG_PERMANENT_SENSOR_OFFSET");
    const bool swappedThird = ReplaceFirstSourceToken(
        swappedBindings, "FFX_DEBUG_SWAP_SENTINEL", "FFX_DEBUG_INVINCIBLE_PARTY_OFFSET");
    Expect(swappedFirst && swappedSecond && swappedThird,
           "binding-swap mutation setup must exchange two recovered offsets");
    Expect(!ValidateExactDebugBindingPairs(swappedBindings),
           "mutation proof: swapping two canonical Debug offsets must be rejected");

    const SourceBlock ap = SourceFunctionBody(boosterSource, "void ApplyEntirePartyAp()");
    Expect(ap.Valid() && ValidateExactApLoopStructure(ap.body),
           "AP loop validator must accept the exact production structure");
    std::string shortBound = ap.body;
    Expect(ReplaceFirstSourceToken(
               shortBound,
               "slot < FfxHooks::F8Runtime::kApSlotCount; ++slot",
               "slot < FfxHooks::F8Runtime::kApSlotCount - 1u; ++slot"),
           "AP bound mutation setup must shorten one loop");
    Expect(!ValidateExactApLoopStructure(shortBound),
           "mutation proof: a six-slot AP loop must be rejected");
    std::string wrongDestination = ap.body;
    Expect(ReplaceFirstSourceToken(
               wrongDestination,
               "g_adapter.participationBytes[slot], updates[slot].participation",
               "g_adapter.earnBytes[slot], updates[slot].participation"),
           "AP destination mutation setup must redirect participation writes");
    Expect(!ValidateExactApLoopStructure(wrongDestination),
           "mutation proof: a participation write redirected to earn bytes must be rejected");
    std::string wrongSnapshot = ap.body;
    Expect(ReplaceFirstSourceToken(
               wrongSnapshot,
               "g_adapter.inParty[slot], &inParty[slot]",
               "g_adapter.participationBytes[slot], &inParty[slot]"),
           "AP snapshot mutation setup must redirect the in_party read");
    Expect(!ValidateExactApLoopStructure(wrongSnapshot),
           "mutation proof: an in_party snapshot redirected to participation must be rejected");
    std::string wrongReadback = ap.body;
    Expect(ReplaceFirstSourceToken(
               wrongReadback,
               "g_adapter.earnBytes[slot], &earnReadback[slot]",
               "g_adapter.earnBytes[slot], &participationReadback[slot]"),
           "AP readback mutation setup must redirect the earn destination");
    Expect(!ValidateExactApLoopStructure(wrongReadback),
           "mutation proof: earn readback into the participation array must be rejected");
    std::string missingReadbackEquality = ap.body;
    Expect(ReplaceFirstSourceToken(
               missingReadbackEquality,
               "participationReadback[slot] == updates[slot].participation &&",
               "true &&"),
           "AP readback-equality removal mutation setup must remove one comparison");
    Expect(!ValidateExactApLoopStructure(missingReadbackEquality),
           "mutation proof: removing a participation readback equality must be rejected");
    std::string swappedReadbackEqualities = ap.body;
    const bool swappedParticipationReadback = ReplaceFirstSourceToken(
        swappedReadbackEqualities,
        "participationReadback[slot] == updates[slot].participation",
        "participationReadback[slot] == updates[slot].readbackSwapSentinel");
    const bool swappedEarnReadback = ReplaceFirstSourceToken(
        swappedReadbackEqualities, "earnReadback[slot] == updates[slot].earn",
        "earnReadback[slot] == updates[slot].participation");
    const bool restoredReadbackSentinel = ReplaceFirstSourceToken(
        swappedReadbackEqualities,
        "participationReadback[slot] == updates[slot].readbackSwapSentinel",
        "participationReadback[slot] == updates[slot].earn");
    Expect(swappedParticipationReadback && swappedEarnReadback && restoredReadbackSentinel,
           "AP readback-equality swap mutation setup must exchange both expected values");
    Expect(!ValidateExactApLoopStructure(swappedReadbackEqualities),
           "mutation proof: swapped participation/earn readback equalities must be rejected");
    std::string shortCircuitedReadback = ap.body;
    Expect(ReplaceFirstSourceToken(
               shortCircuitedReadback,
               "earnReadback[slot] == updates[slot].earn && readbackMatches;",
               "earnReadback[slot] == updates[slot].earn;"),
           "AP readback accumulation mutation setup must remove the cumulative match");
    Expect(!ValidateExactApLoopStructure(shortCircuitedReadback),
           "mutation proof: short-circuiting cumulative readbackMatches must be rejected");
    std::string extraLoop = ap.body;
    extraLoop +=
        "\nfor (size_t slot = 0; slot < FfxHooks::F8Runtime::kApSlotCount; ++slot) "
        "{ GuardedStoreByte(g_adapter.earnBytes[slot], updates[slot].earn); }\n";
    Expect(!ValidateExactApLoopStructure(extraLoop),
           "mutation proof: a fifth AP slot loop must be rejected");
}

bool SameNullable(const char* actual, const char* expected) {
    if (!actual || !expected) return actual == expected;
    return strcmp(actual, expected) == 0;
}

void ExpectCatalog(bool condition, size_t index, const char* field) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        fprintf(stderr, "FAIL: catalog row %zu has wrong %s\n", index, field);
    }
}

BoolGateSpec MigratedSpec() {
    return {
        "field_scout.master",
        "f8_authority.field_scout_master",
        "labs.field_scout",
        "FFXHOOKS_ENABLE_FIELD_SCOUT",
        "field_scout.flag",
        "FFXHOOKS_DISABLE_FIELD_SCOUT",
        "field_scout.flag.off",
        "field_scout_global.flag.off",
        false,
    };
}

void TestExactLookupHasNoAliasesOrSuffixes() {
    FakeState state;
    Configure(state,
              "[alpha]\n"
              "enabled = 1\n"
              "[beta]\n"
              "enabled = 0\n"
              "[field_scout]\n"
              "master = 1\n");

    bool value = false;
    Expect(TryGetBoolExact("alpha.enabled", &value) && value,
           "exact lookup must find the requested section/key");
    Expect(!TryGetBoolExact("enabled", &value),
           "exact lookup must not suffix-match another section");
    Expect(!GetBool("enabled", false),
           "generic GetBool must return its default instead of suffix-matching");
    Expect(!TryGetBoolExact("field_scout_master", &value),
           "exact lookup must not rewrite underscores into dots");
}

void TestResolutionPrecedence() {
    {
        FakeState state;
        Configure(state,
                  "[field_scout]\nmaster = 0\n"
                  "[f8_authority]\nfield_scout_master = 1\n"
                  "[labs]\nfield_scout = 0\n");
        state.environment["FFXHOOKS_ENABLE_FIELD_SCOUT"] = true;
        state.flags["field_scout.flag"] = BoolSource::LegacyFlagRoot;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(result.value && result.source == BoolSource::Environment,
               "explicit environment true must override all lower sources");
    }
    {
        FakeState state;
        Configure(state,
                  "[field_scout]\nmaster = 1\n"
                  "[f8_authority]\nfield_scout_master = 1\n"
                  "[labs]\nfield_scout = 1\n");
        state.environment["FFXHOOKS_ENABLE_FIELD_SCOUT"] = false;
        state.flags["field_scout.flag"] = BoolSource::LegacyFlagModules;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(!result.value && result.source == BoolSource::Environment,
               "explicit environment false must override all lower positive sources");
    }
    {
        FakeState state;
        Configure(state, "[labs]\nfield_scout = 1\n");
        state.environment["FFXHOOKS_DISABLE_FIELD_SCOUT"] = true;
        state.environment["FFXHOOKS_ENABLE_FIELD_SCOUT"] = true;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(!result.value && result.source == BoolSource::DisableEnvironment,
               "true disable environment must win before every positive source");
    }
    {
        FakeState state;
        Configure(state, "[labs]\nfield_scout = 1\n");
        state.environment["FFXHOOKS_ENABLE_FIELD_SCOUT"] = true;
        state.flags["field_scout.flag.off"] = BoolSource::LegacyFlagConfig;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(!result.value && result.source == BoolSource::LegacyOffFlag,
               "present feature off flag must win before every positive source");
    }
    {
        FakeState state;
        Configure(state, "[labs]\nfield_scout = 1\n");
        state.environment["FFXHOOKS_ENABLE_FIELD_SCOUT"] = true;
        state.flags["field_scout_global.flag.off"] = BoolSource::LegacyFlagRoot;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(!result.value && result.source == BoolSource::LegacyOffFlag,
               "present global off flag must win before every positive source");
    }
    {
        FakeState state;
        Configure(state,
                  "[field_scout]\nmaster = 0\n"
                  "[f8_authority]\nfield_scout_master = 1\n"
                  "[labs]\nfield_scout = 1\n");
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(!result.value && result.source == BoolSource::AuthoritativeCanonicalIni,
               "authority marker plus canonical false must beat legacy true");
    }
    {
        FakeState state;
        Configure(state,
                  "[field_scout]\nmaster = 0\n"
                  "[labs]\nfield_scout = 1\n");
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(result.value && result.source == BoolSource::LegacyIni,
               "unmarked pre-seeded canonical false must not beat legacy true");
    }
}

void TestArenaMusicMarkerIo() {
    char exe[MAX_PATH]={};
    Expect(GetModuleFileNameA(nullptr,exe,MAX_PATH)!=0,"music IO fixture locates its isolated runner");
    char* slash=std::strrchr(exe,'\\');if(!slash)return;*slash=0;
    const std::string root(exe);
    for(const auto* sub:{"modules","config","modules\\config"})CreateDirectoryA((root+"\\"+sub).c_str(),nullptr);
    const char* leaves[]={"modules\\arena_plus_music.flag.off","config\\arena_plus_music.flag.off",
        "modules\\config\\arena_plus_music.flag.off","arena_plus_music.flag.off"};
    const char contents[]="Jarvis-HOOK isolated F8 marker";
    std::vector<std::string> paths;
    for(const auto* leaf:leaves)paths.push_back(root+"\\"+leaf);
    const auto* flag=FfxHooks::FindF8Flag("arena_plus.music");
    for(bool persist:{false,true}) {
        bool created=true;
        for(const auto& path:paths) {
            HANDLE file=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE){created=false;break;}
            DWORD written=0;const bool ok=WriteFile(file,contents,sizeof(contents),&written,nullptr)&&written==sizeof(contents);
            CloseHandle(file);if(!ok){created=false;break;}
        }
        Expect(created,"marker IO fixtures never overwrite an existing flag");
        if(!created)return;
        FakeState state;Configure(state,"[arena_plus]\nmusic=0\n");state.persistSucceeds=persist;
        SetProvidersForTests({&state,&FakeTryEnvBool,nullptr,&FakePersistText,nullptr});
        const auto result=FfxHooks::SetF8FlagValue(*flag,true);
        Expect((result.code==FfxHooks::F8EditCode::Saved)==persist,"real marker archival follows INI persistence result");
        for(const auto& path:paths) {
            const bool exists=GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES;
            Expect(exists!=persist,"each of four exact-name OFF locations follows the transaction");
            std::string readPath=path;
            if(persist) {
                WIN32_FIND_DATAA data{};HANDLE find=FindFirstFileA((path+".f8-on-*.bak").c_str(),&data);
                Expect(find!=INVALID_HANDLE_VALUE,"successful ON retains the old marker as a backup");
                if(find==INVALID_HANDLE_VALUE)continue;
                FindClose(find);readPath=path.substr(0,path.find_last_of('\\')+1)+data.cFileName;
            }
            HANDLE file=CreateFileA(readPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            char bytes[sizeof(contents)]={};DWORD read=0;
            const bool ok=file!=INVALID_HANDLE_VALUE&&ReadFile(file,bytes,sizeof(bytes),&read,nullptr)&&read==sizeof(contents)&&std::memcmp(bytes,contents,sizeof(contents))==0;
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
            Expect(ok,"archive or restored marker retains its exact bytes");
            if(ok)DeleteFileA(readPath.c_str());
        }
    }
}

void TestArenaMusicExplicitOnRetiresOnlyOwnOff() {
    const auto* flag=FfxHooks::FindF8Flag("arena_plus.music");
    {FakeState state;Configure(state,"[arena_plus]\nmusic=0\n");
     state.flags["arena_plus_music.flag.off"]=BoolSource::LegacyFlagModules;
     const auto result=FfxHooks::SetF8FlagValue(*flag,true);
     Expect(result.code==FfxHooks::F8EditCode::Saved&&result.effective.value,"explicit Music ON clears the old feature-specific OFF override");
     Expect(state.archivedMusicFlags.count("arena_plus_music.flag.off")==1,"the old marker is retained for rollback");}
    {FakeState state;Configure(state,"[arena_plus]\nmusic=0\n");
     state.flags["arena_plus_music.flag.off"]=BoolSource::LegacyFlagModules;state.persistSucceeds=false;
     const auto result=FfxHooks::SetF8FlagValue(*flag,true);
     Expect(result.code==FfxHooks::F8EditCode::PersistFailed&&state.flags.count("arena_plus_music.flag.off")==1,
            "INI failure restores the marker and remains OFF");}
    {FakeState state;Configure(state,"[arena_plus]\nmusic=0\n");
     state.flags["arena_plus_music.flag.off"]=BoolSource::LegacyFlagModules;state.archiveSucceeds=false;
     const auto result=FfxHooks::SetF8FlagValue(*flag,true);
     Expect(result.code==FfxHooks::F8EditCode::PersistFailed&&state.persistCalls==0,"archive failure cannot claim an ON edit");}
    {FakeState state;Configure(state,"[arena_plus]\nmusic=0\n");
     state.flags["arena_plus_music.flag.off"]=BoolSource::LegacyFlagModules;state.flags["music.flag.off"]=BoolSource::LegacyFlagRoot;
     const auto result=FfxHooks::SetF8FlagValue(*flag,true);
     Expect(!result.effective.value&&state.archivedMusicFlags.empty()&&state.flags.size()==2,"global OFF remains authoritative without archiving other flags");}
}

void TestCompatibilityNegativeNames() {
    {
        FakeState state;
        Configure(state, "[labs]\narena_plus_compose_f7 = 1\n");
        BoolGateSpec spec = {
            "arena_plus.compose_f7", "f8_authority.arena_plus_compose_f7",
            "labs.arena_plus_compose_f7", "FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7",
            "arena_plus_compose_f7.flag", "FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7",
            nullptr, nullptr, false,
        };
        state.environment["FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7"] = true;
        const BoolGateResult result = ResolveBoolGate(spec);
        Expect(!result.value && result.source == BoolSource::DisableEnvironment,
               "Compose must preserve its established disable environment name");
    }
    {
        FakeState state;
        Configure(state, "[music]\narena_plus = 1\n");
        BoolGateSpec spec = {
            "arena_plus.music", "f8_authority.arena_plus_music", "music.arena_plus",
            "FFXHOOKS_ARENAPLUS_MUSIC", "arena_plus_music.flag",
            "FFXHOOKS_DISABLE_ARENA_PLUS_MUSIC", "arena_plus_music.flag.off",
            "music.flag.off", false,
        };
        state.flags["arena_plus_music.flag.off"] = BoolSource::LegacyFlagModulesConfig;
        const BoolGateResult localOff = ResolveBoolGate(spec);
        Expect(!localOff.value && localOff.source == BoolSource::LegacyOffFlag,
               "Arena+ Music must preserve arena_plus_music.flag.off");
        state.flags.clear();
        state.flags["music.flag.off"] = BoolSource::LegacyFlagRoot;
        const BoolGateResult globalOff = ResolveBoolGate(spec);
        Expect(!globalOff.value && globalOff.source == BoolSource::LegacyOffFlag,
               "Arena+ Music must preserve the global music.flag.off");
    }
}

void TestEveryLegacyFlagLocationIsObservable() {
    const BoolSource sources[] = {
        BoolSource::LegacyFlagModules,
        BoolSource::LegacyFlagConfig,
        BoolSource::LegacyFlagModulesConfig,
        BoolSource::LegacyFlagRoot,
    };
    for (const BoolSource source : sources) {
        FakeState state;
        Configure(state, "[core]\nlog_level = 1\n");
        state.flags["field_scout.flag"] = source;
        const BoolGateResult result = ResolveBoolGate(MigratedSpec());
        Expect(result.value && result.source == source,
               "each legacy flag location must retain its distinct BoolSource");
    }
}

void TestUnmarkedCanonicalAndInvalidInputs() {
    FakeState state;
    Configure(state, "[cheats]\ninvincible_party = 0\ninvalid = maybe\n");
    const BoolGateSpec canonicalOnly = {
        "cheats.invincible_party", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, true,
    };
    const BoolGateResult result = ResolveBoolGate(canonicalOnly);
    Expect(!result.value && result.source == BoolSource::UnmarkedCanonicalIni,
           "unmarked canonical must be used when the row has no legacy source");

    bool value = true;
    Expect(!TryGetBoolExact(nullptr, &value), "null exact key must be rejected");
    Expect(!TryGetBoolExact("cheats.invincible_party", nullptr),
           "null exact output must be rejected");
    Expect(!TryGetBoolExact("cheats.invalid", &value),
           "invalid exact boolean must be reported as unknown");
    Expect(GetBool(nullptr, true), "null boolean key must return its default");
    Expect(GetInt(nullptr, 73) == 73, "null integer key must return its default");
    Expect(GetFloat(nullptr, 2.5f) == 2.5f, "null float key must return its default");
    Expect(strcmp(GetString(nullptr, "fallback"), "fallback") == 0,
           "null string key must return a safe default copy");

    const BoolGateSpec invalid = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, true};
    const BoolGateResult invalidResult = ResolveBoolGate(invalid);
    Expect(invalidResult.value && invalidResult.source == BoolSource::DefaultValue,
           "invalid gate spec must return its default without dereferencing null");
}

void TestSourceNamesAreStable() {
    struct Case { BoolSource source; const char* expected; };
    const Case cases[] = {
        {BoolSource::DefaultValue, "DefaultValue"},
        {BoolSource::DisableEnvironment, "DisableEnvironment"},
        {BoolSource::LegacyOffFlag, "LegacyOffFlag"},
        {BoolSource::Environment, "Environment"},
        {BoolSource::AuthoritativeCanonicalIni, "AuthoritativeCanonicalIni"},
        {BoolSource::LegacyIni, "LegacyIni"},
        {BoolSource::LegacyFlagModules, "LegacyFlagModules"},
        {BoolSource::LegacyFlagConfig, "LegacyFlagConfig"},
        {BoolSource::LegacyFlagModulesConfig, "LegacyFlagModulesConfig"},
        {BoolSource::LegacyFlagRoot, "LegacyFlagRoot"},
        {BoolSource::UnmarkedCanonicalIni, "UnmarkedCanonicalIni"},
    };
    for (const Case& item : cases) {
        Expect(strcmp(FfxHooks::Config::BoolSourceName(item.source), item.expected) == 0,
               "BoolSourceName must expose a stable diagnostic label");
    }
}

struct ExpectedCatalogRow {
    const char* tab;
    const char* label;
    const char* canonicalKey;
    const char* authorityKey;
    const char* legacyKey;
    const char* envName;
    const char* flagName;
    const char* disableEnvName;
    const char* offFlagName;
    const char* globalOffFlagName;
    bool defaultValue;
    F8Activation activation;
    F8ApplyMode applyMode;
};

void TestSpeedHackControl() {
    struct Step {
        FfxHooks::SpeedHackInputSample sample;
        FfxHooks::SpeedHackTransition expectedTransition;
        uint8_t expectedFactor;
        const char* message;
    };
    struct Scenario {
        const Step* steps;
        size_t count;
    };

    static const Step gateOff[] = {
        {{false, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "Speed Hack gate OFF must ignore an otherwise valid chord"},
    };
    static const Step armedWithoutChord[] = {
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 1,
         "Speed Hack must stay at 1x while armed without K"},
    };
    static const Step cycle[] = {
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "first armed Ctrl+Shift+K edge must select 2x"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 2,
         "held K must not retrigger the speed cycle"},
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 2,
         "releasing K must retain 2x until the next edge"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 4,
         "second armed Ctrl+Shift+K edge must select 4x"},
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 4,
         "K release after 4x must re-arm the edge detector"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 8,
         "third armed Ctrl+Shift+K edge must select 8x"},
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 8,
         "K release after 8x must re-arm the edge detector"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::Restored1x, 1,
         "fourth armed Ctrl+Shift+K edge must wrap to 1x"},
    };
    static const Step keyBeforeModifiers[] = {
        {{true, true, false, false, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "K before modifiers must not activate Speed Hack"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "adding modifiers while K is held must not create an edge"},
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 1,
         "K release after an invalid order must only re-arm the edge detector"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "a fresh Ctrl+Shift+K edge after release must select 2x"},
    };
    static const Step missingCtrl[] = {
        {{true, true, false, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "Ctrl is required for Speed Hack activation"},
    };
    static const Step missingShift[] = {
        {{true, true, true, false, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "Shift is required for Speed Hack activation"},
    };
    static const Step altPresent[] = {
        {{true, true, true, true, true, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "Alt must suppress the Speed Hack chord"},
    };
    static const Step focusLoss[] = {
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "focus-loss fixture must accelerate before losing foreground"},
        {{true, false, true, true, false, true},
         FfxHooks::SpeedHackTransition::Restored1x, 1,
         "foreground loss must restore 1x exactly once"},
        {{true, false, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "continued foreground loss must not emit a second restore"},
    };
    static const Step gateLoss[] = {
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "gate-loss fixture must accelerate before gate removal"},
        {{false, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::Restored1x, 1,
         "gate loss must restore 1x exactly once"},
        {{false, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "continued gate loss must not emit a second restore"},
    };
    static const Step reenableWhileHeld[] = {
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "re-enable fixture must begin accelerated"},
        {{false, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::Restored1x, 1,
         "gate loss while K is held must restore 1x"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::None, 1,
         "re-enabling while K is held must require a release"},
        {{true, true, true, true, false, false},
         FfxHooks::SpeedHackTransition::None, 1,
         "release after re-enable must only arm the next edge"},
        {{true, true, true, true, false, true},
         FfxHooks::SpeedHackTransition::FactorChanged, 2,
         "new edge after re-enable must select 2x"},
    };
    static const Scenario scenarios[] = {
        {gateOff, sizeof(gateOff) / sizeof(gateOff[0])},
        {armedWithoutChord, sizeof(armedWithoutChord) / sizeof(armedWithoutChord[0])},
        {cycle, sizeof(cycle) / sizeof(cycle[0])},
        {keyBeforeModifiers, sizeof(keyBeforeModifiers) / sizeof(keyBeforeModifiers[0])},
        {missingCtrl, sizeof(missingCtrl) / sizeof(missingCtrl[0])},
        {missingShift, sizeof(missingShift) / sizeof(missingShift[0])},
        {altPresent, sizeof(altPresent) / sizeof(altPresent[0])},
        {focusLoss, sizeof(focusLoss) / sizeof(focusLoss[0])},
        {gateLoss, sizeof(gateLoss) / sizeof(gateLoss[0])},
        {reenableWhileHeld, sizeof(reenableWhileHeld) / sizeof(reenableWhileHeld[0])},
    };

    for (const Scenario& scenario : scenarios) {
        FfxHooks::SpeedHackControlState state{};
        for (size_t stepIndex = 0; stepIndex < scenario.count; ++stepIndex) {
            const Step& step = scenario.steps[stepIndex];
            const FfxHooks::SpeedHackTransition actual =
                FfxHooks::AdvanceSpeedHackControl(&state, step.sample);
            Expect(actual == step.expectedTransition && state.factor == step.expectedFactor,
                   step.message);
        }
    }
}

void TestSpeedHackStickyForegroundLoss() {
    FfxHooks::SpeedHackControlState state{};
    FfxHooks::SpeedHackInputSample heldChord = {
        true, true, true, true, false, true,
    };

    Expect(FfxHooks::AdvanceSpeedHackControl(&state, heldChord) ==
               FfxHooks::SpeedHackTransition::FactorChanged &&
               state.factor == 2,
           "the sticky focus-loss fixture must begin with an accelerated route");
    state.factor = 8;

    Expect(FfxHooks::AdvanceSpeedHackControlAfterForegroundEvent(
               &state, heldChord, true) ==
               FfxHooks::SpeedHackTransition::Restored1x &&
               state.factor == 1 && state.keyWasDown,
           "a queued focus-loss event must restore 1x even if the first returning Present sees FFX foreground");
    Expect(FfxHooks::AdvanceSpeedHackControlAfterForegroundEvent(
               &state, heldChord, false) ==
               FfxHooks::SpeedHackTransition::None && state.factor == 1,
           "a held chord after focus return must not silently re-arm Speed Hack");

    heldChord.keyDown = false;
    Expect(FfxHooks::AdvanceSpeedHackControlAfterForegroundEvent(
               &state, heldChord, false) ==
               FfxHooks::SpeedHackTransition::None,
           "releasing K after focus return must only re-arm the edge detector");
    heldChord.keyDown = true;
    Expect(FfxHooks::AdvanceSpeedHackControlAfterForegroundEvent(
               &state, heldChord, false) ==
               FfxHooks::SpeedHackTransition::FactorChanged &&
               state.factor == 2,
           "a fresh post-focus chord edge may select 2x again");
}

void TestSpeedHackNativeArbitrationAndTelemetry() {
    using FfxHooks::ResolveSpeedHackArbitration;
    using FfxHooks::SpeedHackArbitrationInput;
    using FfxHooks::SpeedHackBackend;
    using FfxHooks::SpeedHackConflictReason;
    using FfxHooks::SpeedHackNativeAction;
    using FfxHooks::SpeedHackRuntimePhase;

    Expect(RVA_FFX_NATIVE_SPEED_BOOSTER == 0x008E82A4u &&
               RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY == 0x008E82ACu,
           "native Speed arbitration must retain the exact DWORD and read-only availability RVAs");

    SpeedHackArbitrationInput input{};
    input.requestedFactor = 2;
    input.maxSpeed = 8.0f;
    input.nativeStateReady = true;
    input.nativeAvailabilityReady = true;
    input.globalTickHookReady = true;
    input.dialogBypassReady = true;
    input.globalTargetOwned = true;
    input.nativeAvailable = true;
    input.observedNativeState = 0;

    FfxHooks::SpeedHackArbitration decision = ResolveSpeedHackArbitration(input);
    Expect(decision.route.requestedFactor == 2 && decision.route.routedFactor == 2 &&
               decision.route.backend == SpeedHackBackend::NativeStandard &&
               decision.nativeAction == SpeedHackNativeAction::ClaimState1 &&
               decision.expectedNativeState == 0 && decision.desiredNativeState == 1 &&
               decision.phase == SpeedHackRuntimePhase::Arming,
           "2x must claim native SpeedBooster state 1 only from observed zero");

    input.nativeOwned = true;
    input.lastWrittenNativeState = 1;
    input.observedNativeState = 1;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::None &&
               decision.phase == SpeedHackRuntimePhase::Armed &&
               decision.route.backend == SpeedHackBackend::NativeStandard,
           "owned native state 1 must report 2x armed without claiming callback application");

    input.requestedFactor = 4;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::ChangeToState2 &&
               decision.expectedNativeState == 1 && decision.desiredNativeState == 2 &&
               decision.route.routedFactor == 4,
           "2x to 4x must compare-change the exact owned native state 1 to state 2");

    input.requestedFactor = 8;
    input.observedNativeState = 2;
    input.lastWrittenNativeState = 2;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::RestoreZero &&
               decision.expectedNativeState == 2 && decision.desiredNativeState == 0 &&
               decision.route.backend == SpeedHackBackend::FastFieldScenes,
           "4x to 8x must compare-restore native state before arming the global field-scene route");

    input.nativeOwned = false;
    input.lastWrittenNativeState = 0;
    input.observedNativeState = 0;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::None &&
               decision.route.routedFactor == 8 &&
               decision.route.backend == SpeedHackBackend::FastFieldScenes &&
               decision.phase == SpeedHackRuntimePhase::Armed,
           "8x must arm only the clean-room field-scene bridge while native state is zero");

    input.requestedFactor = 2;
    input.observedNativeState = 1;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::NativeBusy &&
               decision.phase == SpeedHackRuntimePhase::Conflict &&
               decision.route.routedFactor == 1 &&
               decision.route.backend == SpeedHackBackend::None,
           "an unowned nonzero native state must fail closed instead of double-multiplying");

    input.nativeOwned = true;
    input.lastWrittenNativeState = 2;
    input.observedNativeState = 1;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::NativeDrift &&
               decision.nativeAction == SpeedHackNativeAction::None,
           "native drift away from the last exact write must drop authority without overwriting it");

    input.nativeOwned = false;
    input.lastWrittenNativeState = 0;
    input.observedNativeState = 0;
    input.nativeDriftLatched = true;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::NativeDrift &&
               decision.nativeAction == SpeedHackNativeAction::None &&
               decision.route.routedFactor == 1,
           "native drift must stay fail-closed instead of re-adopting observed zero next frame");
    input.requestedFactor = 1;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.phase == SpeedHackRuntimePhase::Idle &&
               decision.conflict == SpeedHackConflictReason::None,
           "returning to 1x must clear the native-drift latch before a later deliberate cycle");
    input.nativeDriftLatched = false;
    input.requestedFactor = 2;

    Expect(FfxHooks::ResolveSpeedHackPublishEpochDisposition(true, true) ==
               FfxHooks::SpeedHackPublishEpochDisposition::Commit,
           "a publication with one unchanged producer epoch must commit its route and voice side effects");
    Expect(FfxHooks::ResolveSpeedHackPublishEpochDisposition(false, false) ==
               FfxHooks::SpeedHackPublishEpochDisposition::RejectBeforeSideEffects,
           "a stale producer discovered before publication must perform no route or voice side effect");
    Expect(FfxHooks::ResolveSpeedHackPublishEpochDisposition(true, false) ==
               FfxHooks::SpeedHackPublishEpochDisposition::CompensateAfterSideEffects,
           "focus loss after the final frame precheck but before publication must force compensating neutralization");

    SpeedHackArbitrationInput latchedInput{};
    latchedInput.requestedFactor = 2;
    latchedInput.maxSpeed = 8.0f;
    latchedInput.nativeStateReady = true;
    latchedInput.nativeAvailabilityReady = true;
    latchedInput.globalTickHookReady = true;
    latchedInput.dialogBypassReady = true;
    latchedInput.globalTargetOwned = true;
    latchedInput.nativeAvailable = true;
    latchedInput.observedNativeState = 0;
    latchedInput.nativeDriftLatched = true;
    latchedInput.unxModuleLoaded = true;
    bool runtimeDriftLatch = FfxHooks::NextSpeedHackNativeDriftLatch(false, true, false);
    runtimeDriftLatch = FfxHooks::NextSpeedHackNativeDriftLatch(
        runtimeDriftLatch, false, false);
    FfxHooks::SpeedHackArbitration latchedDecision = ResolveSpeedHackArbitration(latchedInput);
    Expect(runtimeDriftLatch &&
               latchedDecision.conflict == SpeedHackConflictReason::UnXModuleLoaded &&
               !FfxHooks::SpeedHackCanResetNativeDriftLatch(latchedInput, latchedDecision),
           "a transient UnX conflict may be reported but must not reset an earlier native-drift latch");
    latchedInput.unxModuleLoaded = false;
    runtimeDriftLatch = FfxHooks::NextSpeedHackNativeDriftLatch(
        runtimeDriftLatch, false, false);
    latchedInput.nativeDriftLatched = runtimeDriftLatch;
    latchedDecision = ResolveSpeedHackArbitration(latchedInput);
    Expect(latchedDecision.conflict == SpeedHackConflictReason::NativeDrift,
           "native drift must reappear after a transient UnX conflict clears instead of re-adopting 2x");

    latchedInput.globalTargetOwned = false;
    runtimeDriftLatch = FfxHooks::NextSpeedHackNativeDriftLatch(
        runtimeDriftLatch, false, false);
    latchedDecision = ResolveSpeedHackArbitration(latchedInput);
    Expect(latchedDecision.conflict == SpeedHackConflictReason::ExternalTargetDrift &&
               !FfxHooks::SpeedHackCanResetNativeDriftLatch(latchedInput, latchedDecision),
           "target drift must not overwrite or reset an earlier native-drift latch");
    latchedInput.globalTargetOwned = true;
    latchedDecision = ResolveSpeedHackArbitration(latchedInput);
    Expect(latchedDecision.conflict == SpeedHackConflictReason::NativeDrift,
           "native drift must remain latched after the field-service target returns to owned bytes");

    latchedInput.requestedFactor = 1;
    latchedDecision = ResolveSpeedHackArbitration(latchedInput);
    Expect(FfxHooks::SpeedHackCanResetNativeDriftLatch(latchedInput, latchedDecision),
           "an explicit safe 1x decision with zero native state is the only in-process drift reset policy");
    runtimeDriftLatch = FfxHooks::NextSpeedHackNativeDriftLatch(
        runtimeDriftLatch, false, true);
    Expect(!runtimeDriftLatch,
           "only the explicit safe 1x reset policy may clear the monotonic native-drift latch");
    latchedInput.requestedFactor = 2;
    Expect(!FfxHooks::SpeedHackCanResetNativeDriftLatch(latchedInput, latchedDecision),
           "a stale 1x decision must never authorize clearing the drift latch after the request returns to 2x");

    input.globalTargetOwned = true;
    input.unxModuleLoaded = true;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::UnXModuleLoaded &&
               decision.phase == SpeedHackRuntimePhase::Conflict,
           "a loaded UnX module must fail closed because no stable coexistence bridge is approved");

    input.globalTargetOwned = false;
    input.unxModuleLoaded = false;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::ExternalTargetDrift &&
               decision.phase == SpeedHackRuntimePhase::Conflict,
           "external global-target drift must neutralize Speed without chaining the detour");

    input.globalTargetOwned = true;
    input.requestedFactor = 1;
    input.nativeAvailable = false;
    input.nativeOwned = true;
    input.lastWrittenNativeState = 2;
    input.observedNativeState = 0;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::DropAfterNativeUnavailable &&
               decision.phase == SpeedHackRuntimePhase::Idle,
           "1x must accept an engine-reset zero while the native booster is unavailable without a redundant write");

    input.requestedFactor = 4;
    input.observedNativeState = 2;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.conflict == SpeedHackConflictReason::NativeDrift &&
               decision.nativeAction == SpeedHackNativeAction::None,
           "an unavailable native booster with a nonzero owned value must conflict instead of abandoning it");

    input.observedNativeState = 0;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::DropAfterNativeUnavailable &&
               decision.phase == SpeedHackRuntimePhase::Paused &&
               decision.route.requestedFactor == 4 && decision.route.routedFactor == 1,
           "native unavailability must accept the engine reset, write nothing, and avoid a scene claim");

    input.nativeAvailable = true;
    input.nativeOwned = false;
    input.lastWrittenNativeState = 0;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.nativeAction == SpeedHackNativeAction::ClaimState2 &&
               decision.expectedNativeState == 0 && decision.desiredNativeState == 2,
           "restored native availability must reapply 4x only by claiming observed zero");

    input.requestedFactor = 8;
    input.maxSpeed = 4.0f;
    decision = ResolveSpeedHackArbitration(input);
    Expect(decision.route.requestedFactor == 8 && decision.route.routedFactor == 4 &&
               decision.route.backend == SpeedHackBackend::NativeStandard &&
               decision.nativeAction == SpeedHackNativeAction::ClaimState2,
           "the maximum clamp must demote an 8x request to native Standard boost 4x");

    FfxHooks::SpeedHackRoute global8{};
    global8.requestedFactor = 8;
    global8.routedFactor = 8;
    global8.backend = SpeedHackBackend::FastFieldScenes;
    const uint32_t globalWord = FfxHooks::PackSpeedHackPublishedRoute(global8, 17, false);
    Expect(!FfxHooks::SpeedHackCallbackTelemetryMatches(globalWord, globalWord, 41, 41) &&
               FfxHooks::SpeedHackCallbackTelemetryMatches(globalWord, globalWord, 41, 42) &&
               !FfxHooks::SpeedHackCallbackTelemetryMatches(
                   globalWord, FfxHooks::PackSpeedHackPublishedRoute(global8, 16, false), 41, 42),
           "only a current-generation callback count advance may promote armed 8x to applied");
    Expect(FfxHooks::SpeedHackShouldLogAppliedGeneration(
               globalWord, 0, globalWord, 41, 42) &&
               !FfxHooks::SpeedHackShouldLogAppliedGeneration(
                   globalWord, globalWord, globalWord, 41, 43),
           "8x applied logging must emit once per route generation, not once per callback frame");

    FfxHooks::SpeedHackRoute native2{};
    native2.requestedFactor = 2;
    native2.routedFactor = 2;
    native2.backend = SpeedHackBackend::NativeStandard;
    const uint32_t native2Word = FfxHooks::PackSpeedHackPublishedRoute(native2, 18, false);
    FfxHooks::SpeedHackRoute native4 = native2;
    native4.requestedFactor = 4;
    native4.routedFactor = 4;
    const uint32_t native4Word = FfxHooks::PackSpeedHackPublishedRoute(native4, 19, false);
    Expect(FfxHooks::ResolveSpeedHackBridgeAction(native2Word, true, 1) ==
               FfxHooks::SpeedHackBridgeAction::PassThrough &&
               FfxHooks::ResolveSpeedHackBridgeAction(native4Word, true, 2) ==
               FfxHooks::SpeedHackBridgeAction::PassThrough,
           "native 2x/4x routes must preserve the original global delta without reporting conflict");
    Expect(FfxHooks::ResolveSpeedHackBridgeAction(globalWord, true, 0) ==
               FfxHooks::SpeedHackBridgeAction::Scale8 &&
               FfxHooks::ResolveSpeedHackBridgeAction(globalWord, true, 1) ==
               FfxHooks::SpeedHackBridgeAction::NativeConflict &&
               FfxHooks::ResolveSpeedHackBridgeAction(globalWord, false, 0) ==
               FfxHooks::SpeedHackBridgeAction::NativeConflict,
           "only an active custom 8x route may require readable native zero before scaling");
    Expect(!FfxHooks::SpeedHackShouldReportBridgeNativeConflict(native2Word, true) &&
               !FfxHooks::SpeedHackShouldReportBridgeNativeConflict(native4Word, true) &&
               FfxHooks::SpeedHackShouldReportBridgeNativeConflict(globalWord, true) &&
               !FfxHooks::SpeedHackShouldReportBridgeNativeConflict(globalWord, false),
           "a delayed old-8x callback conflict must not poison the currently published native route");

    char label[96] = {};
    Expect(FfxHooks::F8Ui::BuildSpeedIndicatorLabel(
               2, FfxHooks::F8Ui::SpeedIndicatorLabelMode::StandardBoost, label, sizeof(label)) &&
               std::string(label).find("Standard boost") != std::string::npos,
           "2x/4x labels must truthfully name the native Standard boost backend");
    Expect(FfxHooks::F8Ui::BuildSpeedIndicatorLabel(
               8, FfxHooks::F8Ui::SpeedIndicatorLabelMode::FastFieldScenes, label, sizeof(label)) &&
               std::string(label).find("Fast field scenes") != std::string::npos,
           "8x labels must name the reviewed field-scene scope");
    Expect(FfxHooks::F8Ui::BuildSpeedIndicatorLabel(
               4, FfxHooks::F8Ui::SpeedIndicatorLabelMode::Paused, label, sizeof(label)) &&
               strcmp(label, "Speed Hack 4x - Native booster unavailable") == 0 &&
               std::string(label).find("scene") == std::string::npos,
           "native availability loss must not be mislabeled as scene exclusion");

    const FfxHooks::F8FlagSpec* speedSpec = FfxHooks::FindF8Flag("boosters.speed_hack");
    Expect(speedSpec && speedSpec->help &&
               strcmp(speedSpec->help,
                       "LIVE - Native 2/4; field scenes 8; optional FMV acceleration.") == 0 &&
               std::string(speedSpec->help).find("optional FMV acceleration") !=
                   std::string::npos,
           "the Speed description distinguishes the separate opt-in FMV path");
}

void TestSpeedHackBackendRouting() {
    using FfxHooks::ResolveSpeedHackArbitration;
    using FfxHooks::SpeedHackBackend;
    using FfxHooks::SpeedHackRuntimePhase;

    FfxHooks::SpeedHackArbitrationInput ready{};
    ready.maxSpeed = 8.0f;
    ready.nativeStateReady = true;
    ready.nativeAvailabilityReady = true;
    ready.globalTickHookReady = true;
    ready.dialogBypassReady = true;
    ready.globalTargetOwned = true;
    ready.nativeAvailable = true;
    struct RouteCase {
        uint8_t requested;
        float maxSpeed;
        bool nativeOwned;
        uint32_t nativeState;
        uint8_t routed;
        SpeedHackBackend backend;
        SpeedHackRuntimePhase phase;
        const char* message;
    };
    static const RouteCase routes[] = {
        {1, 8.0f, false, 0, 1, SpeedHackBackend::None, SpeedHackRuntimePhase::Idle,
         "1x must leave both Speed Hack backends neutral"},
        {2, 8.0f, true, 1, 2, SpeedHackBackend::NativeStandard, SpeedHackRuntimePhase::Armed,
         "2x must route only through owned native Standard boost state 1"},
        {4, 8.0f, true, 2, 4, SpeedHackBackend::NativeStandard, SpeedHackRuntimePhase::Armed,
         "4x must route only through owned native Standard boost state 2"},
        {8, 8.0f, false, 0, 8, SpeedHackBackend::FastFieldScenes, SpeedHackRuntimePhase::Armed,
         "8x must arm only the clean-room fast-field-scenes backend"},
        {8, 4.0f, true, 2, 4, SpeedHackBackend::NativeStandard, SpeedHackRuntimePhase::Armed,
         "the safety cap must demote 8x to the native Standard boost 4x route"},
    };
    for (const RouteCase& test : routes) {
        FfxHooks::SpeedHackArbitrationInput input = ready;
        input.requestedFactor = test.requested;
        input.maxSpeed = test.maxSpeed;
        input.nativeOwned = test.nativeOwned;
        input.observedNativeState = test.nativeState;
        input.lastWrittenNativeState = test.nativeState;
        const FfxHooks::SpeedHackArbitration decision =
            ResolveSpeedHackArbitration(input);
        Expect(decision.route.requestedFactor == test.requested &&
                   decision.route.routedFactor == test.routed &&
                   decision.route.backend == test.backend &&
                   decision.phase == test.phase,
               test.message);
        Expect(!((decision.route.backend == SpeedHackBackend::NativeStandard &&
                  decision.route.routedFactor == 8) ||
                 (decision.route.backend == SpeedHackBackend::FastFieldScenes &&
                  (decision.route.routedFactor == 2 || decision.route.routedFactor == 4))),
               "a route must never arm native and global speed mechanisms together");
    }

    ready.requestedFactor = 4;
    ready.nativeStateReady = false;
    const FfxHooks::SpeedHackArbitration missingNative =
        ResolveSpeedHackArbitration(ready);
    Expect(missingNative.route.unavailable && missingNative.route.routedFactor == 1 &&
               missingNative.phase == SpeedHackRuntimePhase::Unavailable,
           "2x/4x must fail closed when the native state address is unavailable");

    ready.nativeStateReady = true;
    ready.requestedFactor = 8;
    ready.globalTickHookReady = false;
    const FfxHooks::SpeedHackArbitration missingGlobal =
        ResolveSpeedHackArbitration(ready);
    Expect(missingGlobal.route.unavailable && missingGlobal.route.routedFactor == 1,
           "8x must fail closed when the global tick hook is unavailable");

    ready.globalTickHookReady = true;
    ready.dialogBypassReady = false;
    const FfxHooks::SpeedHackArbitration missingDialog =
        ResolveSpeedHackArbitration(ready);
    Expect(missingDialog.route.unavailable && missingDialog.route.routedFactor == 1,
           "8x must fail closed when its voice bypass owner is unavailable");

    ready.requestedFactor = 2;
    const FfxHooks::SpeedHackArbitration nativeWithoutDialog2 =
        ResolveSpeedHackArbitration(ready);
    Expect(!nativeWithoutDialog2.route.unavailable &&
               nativeWithoutDialog2.route.routedFactor == 2 &&
               nativeWithoutDialog2.route.backend == SpeedHackBackend::NativeStandard,
           "2x must remain available when the 8x-only Dialog Skip dependency is unavailable");
    ready.requestedFactor = 4;
    const FfxHooks::SpeedHackArbitration nativeWithoutDialog4 =
        ResolveSpeedHackArbitration(ready);
    Expect(!nativeWithoutDialog4.route.unavailable &&
               nativeWithoutDialog4.route.routedFactor == 4 &&
               nativeWithoutDialog4.route.backend == SpeedHackBackend::NativeStandard,
           "4x must remain available when the 8x-only Dialog Skip dependency is unavailable");
}

void TestSpeedHackPackedRouteAndNestedTransitionSafety() {
    using FfxHooks::PackSpeedHackPublishedRoute;
    using FfxHooks::SpeedHackAcknowledgementMatches;
    using FfxHooks::SpeedHackBackend;
    using FfxHooks::SpeedHackGlobalFactorFromPublishedRoute;
    using FfxHooks::UnpackSpeedHackPublishedRoute;

    FfxHooks::SpeedHackRoute native4{};
    native4.requestedFactor = 4;
    native4.routedFactor = 4;
    native4.backend = SpeedHackBackend::NativeStandard;
    const uint32_t nativeWord = PackSpeedHackPublishedRoute(native4, 0x123456u, false);
    const FfxHooks::SpeedHackPublishedRoute decodedNative =
        UnpackSpeedHackPublishedRoute(nativeWord);
    Expect(decodedNative.requestedFactor == 4 && decodedNative.routedFactor == 4 &&
               decodedNative.backend == SpeedHackBackend::NativeStandard &&
               decodedNative.generation == 0x123456u && !decodedNative.paused,
           "the packed route must round-trip one coherent requested/routed/backend generation");
    Expect(SpeedHackGlobalFactorFromPublishedRoute(nativeWord) == 1,
           "a packed native 4x route must leave the global multiplier neutral");

    FfxHooks::SpeedHackRoute global8{};
    global8.requestedFactor = 8;
    global8.routedFactor = 8;
    global8.backend = SpeedHackBackend::FastFieldScenes;
    const uint32_t globalWord = PackSpeedHackPublishedRoute(global8, 0x123457u, false);
    Expect(SpeedHackGlobalFactorFromPublishedRoute(globalWord) == 8,
           "an 8x packed route must admit only the global field-scene multiplier");
    Expect(!SpeedHackAcknowledgementMatches(globalWord, nativeWord),
           "an acknowledgement from the previous native generation must never apply to 8x");
    Expect(SpeedHackAcknowledgementMatches(globalWord, globalWord),
           "an acknowledgement must apply only to the exact current packed generation");

    FfxHooks::SpeedHackRoute paused4{};
    paused4.requestedFactor = 4;
    const uint32_t pausedWord = PackSpeedHackPublishedRoute(paused4, 0x123458u, true);
    const FfxHooks::SpeedHackPublishedRoute decodedPaused =
        UnpackSpeedHackPublishedRoute(pausedWord);
    Expect(decodedPaused.paused && decodedPaused.requestedFactor == 4 &&
               decodedPaused.routedFactor == 1 && decodedPaused.backend == SpeedHackBackend::None,
           "a packed native-unavailable pause must retain the request while both backends stay neutral");

    const uint32_t wrappedGeneration =
        PackSpeedHackPublishedRoute(global8, 0x1ABCDEFu, false);
    Expect(UnpackSpeedHackPublishedRoute(wrappedGeneration).generation == 0xABCDEFu,
           "the published generation must have an explicit deterministic 24-bit wrap");
}

void TestSpeedHackIndicatorContract() {
    using FfxHooks::ResolveSpeedHackIndicator;
    using FfxHooks::SpeedHackBackend;
    using FfxHooks::SpeedHackIndicatorMode;
    using FfxHooks::SpeedHackRuntimePhase;

    FfxHooks::SpeedHackRuntimeSnapshot snapshot{};
    snapshot.nativeStateReady = true;
    snapshot.nativeAvailabilityReady = true;
    snapshot.globalTickHookReady = true;
    snapshot.dialogBypassReady = true;
    snapshot.requestedFactor = 1;
    snapshot.routedFactor = 1;
    snapshot.appliedFactor = 1;
    snapshot.backend = SpeedHackBackend::None;
    snapshot.phase = SpeedHackRuntimePhase::Idle;

    const FfxHooks::SpeedHackIndicatorState hidden =
        ResolveSpeedHackIndicator(false, snapshot);
    Expect(hidden.mode == SpeedHackIndicatorMode::Hidden && hidden.factor == 1,
           "Speed Hack indicator must be hidden while its F8 gate is OFF");

    snapshot.requestedFactor = 8;
    snapshot.phase = SpeedHackRuntimePhase::Unavailable;
    const FfxHooks::SpeedHackIndicatorState unavailable =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(unavailable.mode == SpeedHackIndicatorMode::Unavailable && unavailable.factor == 8,
           "Speed Hack indicator must name the unavailable requested factor without advertising it as applied");

    snapshot.backend = SpeedHackBackend::FastFieldScenes;
    snapshot.routedFactor = 8;
    snapshot.phase = SpeedHackRuntimePhase::Armed;
    const FfxHooks::SpeedHackIndicatorState armedGlobal =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(armedGlobal.mode == SpeedHackIndicatorMode::Armed && armedGlobal.factor == 8 &&
               armedGlobal.backend == SpeedHackBackend::FastFieldScenes,
           "Speed Hack indicator must distinguish armed 8x from callback-proven application");

    snapshot.requestedFactor = 8;
    snapshot.routedFactor = 4;
    snapshot.backend = SpeedHackBackend::NativeStandard;
    const FfxHooks::SpeedHackIndicatorState cappedArmed =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(cappedArmed.mode == SpeedHackIndicatorMode::Armed &&
               cappedArmed.factor == 4 &&
               cappedArmed.backend == SpeedHackBackend::NativeStandard,
           "an armed indicator must show the capped native factor, not the larger request");

    snapshot.requestedFactor = 8;
    snapshot.routedFactor = 8;
    snapshot.appliedFactor = 8;
    snapshot.backend = SpeedHackBackend::FastFieldScenes;
    snapshot.phase = SpeedHackRuntimePhase::Applied;
    const FfxHooks::SpeedHackIndicatorState applied =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(applied.mode == SpeedHackIndicatorMode::Applied && applied.factor == 8,
           "only callback-proven global 8x may be labeled applied");

    snapshot.requestedFactor = 4;
    snapshot.routedFactor = 1;
    snapshot.appliedFactor = 1;
    snapshot.backend = SpeedHackBackend::None;
    snapshot.phase = SpeedHackRuntimePhase::Paused;
    const FfxHooks::SpeedHackIndicatorState paused =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(paused.mode == SpeedHackIndicatorMode::Paused && paused.factor == 4,
           "native booster unavailability must stay visible without claiming scene-specific application");

    snapshot.phase = SpeedHackRuntimePhase::Conflict;
    const FfxHooks::SpeedHackIndicatorState conflict =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(conflict.mode == SpeedHackIndicatorMode::Conflict && conflict.factor == 4,
           "external ownership or drift must be visible as conflict, never applied");

    snapshot.requestedFactor = 3;
    snapshot.routedFactor = 3;
    snapshot.backend = SpeedHackBackend::NativeStandard;
    snapshot.phase = SpeedHackRuntimePhase::Applied;
    const FfxHooks::SpeedHackIndicatorState invalid =
        ResolveSpeedHackIndicator(true, snapshot);
    Expect(invalid.mode == SpeedHackIndicatorMode::Unavailable && invalid.factor == 3,
           "Speed Hack indicator must fail closed for a corrupt effective factor");

    std::string speedHackSource;
    std::string dllmainSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\SpeedHackHook.cpp"), speedHackSource) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmainSource),
           "Speed Hack runtime and overlay sources must be readable for indicator contracts");

    Expect(speedHackSource.find("GetSpeedHackRuntimeSnapshot() noexcept") != std::string::npos &&
               speedHackSource.find("g_publishedRouteWord") != std::string::npos &&
               speedHackSource.find("g_appliedRouteWord") != std::string::npos &&
               speedHackSource.find("g_callbackCount") != std::string::npos &&
               speedHackSource.find("g_routeCallbackBaseline") != std::string::npos &&
               speedHackSource.find("SpeedHackCallbackTelemetryMatches") != std::string::npos,
           "Speed Hack must combine an exact route generation with a bounded callback-count advance");
    Expect(speedHackSource.find("Config::GetFloat(\"speed_hack.factor\"") == std::string::npos &&
               speedHackSource.find("Config::GetFloat(\"speed_hack.max_speed\"") != std::string::npos,
           "Speed Hack cycling must ignore stale legacy factor values while retaining the safety cap");
    Expect(speedHackSource.find(
               "SpeedHack: Ctrl+Shift+K requested=%ux routed=%ux backend=%s") != std::string::npos &&
               speedHackSource.find(
               "SpeedHack: applied factor=%ux backend=%s callbacks=%u") != std::string::npos &&
               speedHackSource.find("SpeedHack: restored 1x reason=%s") != std::string::npos,
           "Speed Hack diagnostics must distinguish requested, effective, applied, and restore states");
    Expect(speedHackSource.find("SpeedHackShouldLogAppliedGeneration") != std::string::npos &&
               speedHackSource.find("g_lastAppliedLogCount") == std::string::npos,
           "8x applied diagnostics must be first-per-generation instead of callback-count driven");

    const SourceBlock indicatorBody = SourceFunctionBody(
        dllmainSource, "static bool InGameDrawSpeedHackIndicator(HDC hdc, const RECT& rc)");
    const std::string indicatorCode = SourceCodeOnly(indicatorBody.body);
    Expect(indicatorBody.Valid() && SourceTokensInOrder(indicatorCode, {
               "GetSpeedHackRuntimeSnapshot()",
               "ResolveSpeedHackIndicator(",
               "SpeedIndicatorLabelMode::Unavailable",
               "SpeedIndicatorLabelMode::Conflict",
               "SpeedIndicatorLabelMode::Paused",
               "SpeedIndicatorLabelMode::FastFieldScenes",
               "SpeedIndicatorLabelMode::FastFieldScenesArmed",
               "SpeedIndicatorLabelMode::StandardBoost",
               "SpeedIndicatorLabelMode::Armed",
               "BuildSpeedIndicatorLabel(",
               "const int centerX = (rc.left + rc.right) / 2",
               "InGameDrawRoundFillStroke(",
               "InGameDrawSoftText(",
           }) &&
               indicatorBody.body.find("\"SpeedHack") == std::string::npos,
           "top-center Speed Hack indicator must map runtime truth into the normalized portable label contract");
    Expect(indicatorCode.find(
               "const bool accelerated = indicator.mode == FfxHooks::SpeedHackIndicatorMode::Applied") !=
               std::string::npos &&
               indicatorCode.find("indicator.factor > 1") == std::string::npos,
           "indicator color/glow must reserve acceleration styling for callback-proven APPLIED");

    const SourceBlock textureBody = SourceFunctionBody(
        dllmainSource, "static bool AuroraD3DUpdateTexture()");
    const SourceBlock renderBody = SourceFunctionBody(
        dllmainSource, "static void AuroraD3DRender(IDXGISwapChain* swapChain)");
    Expect(textureBody.Valid() && textureBody.body.find(
               "InGameDrawSpeedHackIndicator(g_auroraD3DMemDc, rc);") != std::string::npos,
           "the shared overlay texture must draw the Speed Hack indicator");
    Expect(renderBody.Valid() && renderBody.body.find("speedIndicatorVisible") != std::string::npos &&
               renderBody.body.find(
                   "if (!actorVisible && !menuOpen && !speedIndicatorVisible) return;") !=
                   std::string::npos,
           "the Present renderer must stay admitted when only the Speed Hack indicator is visible");

    const SourceBlock focusNotify =
        SourceFunctionBody(speedHackSource, "void SpeedHackNotifyForegroundLost() noexcept");
    const SourceBlock speedFrame =
        SourceFunctionBody(speedHackSource, "void SpeedHackFrameTick()");
    const SourceBlock menuWndProc = SourceFunctionBody(
        dllmainSource,
        "static LRESULT CALLBACK InGameMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)");
    const SourceBlock installWndProc = SourceFunctionBody(
        dllmainSource, "static void InGameMenuInstallWndProc(HWND hwnd)");
    const std::string focusCode = SourceCodeOnly(focusNotify.body);
    const std::string speedFrameCode = SourceCodeOnly(speedFrame.body);
    const SourceBlock executeNative = SourceFunctionBody(
        speedHackSource,
        "static SpeedHackNativeExecution ExecuteNativeAction(const SpeedHackArbitration& decision, LONG producerEpoch)");
    const SourceBlock compareRestoreNative = SourceFunctionBody(
        speedHackSource, "static bool CompareRestoreNativeOwned() noexcept");
    const SourceBlock publishDecision = SourceBlockAfterToken(
        speedHackSource, "static bool PublishDecision(");
    const SourceBlock latchNativeDrift = SourceFunctionBody(
        speedHackSource, "static void LatchNativeDrift() noexcept");
    const SourceBlock clearNativeDrift = SourceBlockAfterToken(
        speedHackSource, "static bool TryClearNativeDriftLatchAtSafe1x(");
    const SourceBlock dropNativeMetadata = SourceFunctionBody(
        speedHackSource, "static void DropNativeOwnershipMetadata() noexcept");
    const SourceBlock removeSpeed = SourceFunctionBody(
        speedHackSource, "void RemoveSpeedHackHook()");
    Expect(focusNotify.Valid() && SourceTokensInOrder(focusCode, {
               "InterlockedExchange(&g_foregroundLost, 1)",
               "InterlockedIncrement(&g_neutralEpoch)",
               "PublishNeutralRouteAsync()",
           }) &&
               focusCode.find("Config::") == std::string::npos &&
               focusCode.find("HookLog(") == std::string::npos &&
               menuWndProc.Valid() &&
               menuWndProc.body.find("WM_ACTIVATEAPP") != std::string::npos &&
               menuWndProc.body.find("WM_KILLFOCUS") != std::string::npos &&
               menuWndProc.body.find("FfxHooks::SpeedHackNotifyForegroundLost();") !=
                   std::string::npos,
           "focus loss must neutralize Speed asynchronously even when Present is temporarily paused");
    Expect(installWndProc.Valid() &&
               installWndProc.body.find("if (!hwnd) return;") != std::string::npos &&
               installWndProc.body.find("g_ingameMenuEnabled") == std::string::npos,
           "Speed focus observation must install independently of the optional in-game menu");
    Expect(speedFrame.Valid() && SourceTokensInOrder(speedFrameCode, {
               "producerEpoch = AtomicLoadLong(&g_neutralEpoch)",
               "InterlockedExchange(&g_foregroundLost, 0)",
               "AdvanceSpeedHackControlAfterForegroundEvent",
               "ResolveSpeedHackArbitration",
               "ProducerEpochStillCurrent(producerEpoch)",
               "PublishDecision",
           }),
           "a Present frame must consume sticky focus loss and validate its neutralization epoch before publication");
    Expect(speedFrameCode.find(
               "if (!PublishDecision(neutral, producerEpoch, &currentWord))") !=
               std::string::npos,
           "a global-to-native transition must not write native state unless neutral publication succeeded");
    Expect(executeNative.Valid() && SourceTokensInOrder(SourceCodeOnly(executeNative.body), {
               "ProducerEpochStillCurrent(producerEpoch)",
               "InterlockedCompareExchange(",
               "g_nativeStateAddress",
               "InterlockedExchange(&g_nativeOwned, 1)",
               "ProducerEpochStillCurrent(producerEpoch)",
               "CompareRestoreNativeOwned()",
           }),
           "native CAS must validate the focus/stop epoch immediately before and after a write and compensate on drift");
    Expect(compareRestoreNative.Valid() &&
               SourceTokensInOrder(SourceCodeOnly(compareRestoreNative.body), {
                   "InterlockedCompareExchange(&g_nativeOwned, 0, 1)",
                   "InterlockedExchange(&g_nativeLastWrite, 0)",
                   "InterlockedCompareExchange(g_nativeStateAddress, 0, expected)",
               }),
           "concurrent focus/stop neutralizers must atomically claim one idempotent native restore");
    const size_t finalEpochCheck = speedFrameCode.rfind(
        "if (!ProducerEpochStillCurrent(producerEpoch))");
    const size_t finalEpochRestore = finalEpochCheck == std::string::npos
        ? std::string::npos
        : speedFrameCode.find("CompareRestoreNativeOwned()", finalEpochCheck);
    Expect(finalEpochCheck != std::string::npos && finalEpochRestore != std::string::npos &&
               finalEpochCheck < finalEpochRestore,
           "a focus/stop epoch change after native CAS must compare-restore before Present returns");
    const std::string publishCode = SourceCodeOnly(publishDecision.body);
    Expect(publishDecision.Valid() && SourceTokensInOrder(publishCode, {
               "ProducerEpochStillCurrent(producerEpoch)",
               "DialogSkipFrameTick(desiredGlobal)",
               "InterlockedExchange(&g_runtimePhase",
               "ResolveSpeedHackPublishEpochDisposition(",
               "NeutralizeAfterProducerEpochLoss()",
               "HookLog(",
           }),
           "every changed or unchanged route, including an unchanged 8x route, must trail voice and publication side effects with epoch reconciliation");
    Expect(speedFrameCode.find(
               "if (!PublishDecision(decision, producerEpoch, &currentWord))") !=
               std::string::npos,
           "Present must stop stale logging and telemetry work when publication compensates for focus loss");
    Expect(speedHackSource.find("g_nativeDriftLatched") != std::string::npos &&
               speedFrameCode.find(
                   "AtomicLoadLong(&g_nativeDriftLatched) != 0") != std::string::npos &&
               speedFrameCode.find(
                   "AtomicLoadLong(&g_conflictReason) ==") == std::string::npos &&
               latchNativeDrift.Valid() && clearNativeDrift.Valid() &&
               compareRestoreNative.body.find("LatchNativeDrift()") != std::string::npos,
           "NativeDrift must use a dedicated sticky latch that transient display conflicts cannot overwrite");
    Expect(dropNativeMetadata.Valid() &&
               dropNativeMetadata.body.find("g_nativeOwned") != std::string::npos &&
               dropNativeMetadata.body.find("g_nativeLastWrite") != std::string::npos &&
               dropNativeMetadata.body.find("g_nativeStateAddress") == std::string::npos &&
               dropNativeMetadata.body.find("InterlockedCompareExchange") == std::string::npos &&
               speedFrameCode.find("DropNativeOwnershipMetadata()") != std::string::npos,
           "NativeDrift must discard only ownership metadata without writing or re-adopting the external state");
    Expect(removeSpeed.Valid() &&
               removeSpeed.body.find("RequestSpeedHackStop();") != std::string::npos &&
               removeSpeed.body.find("unHook()") == std::string::npos &&
               removeSpeed.body.find("delete static_cast<PLH::x86Detour*>") == std::string::npos &&
               removeSpeed.body.find("g_globalTickTrampoline = 0") == std::string::npos &&
               removeSpeed.body.find("g_nativeStateAddress = nullptr") == std::string::npos,
           "Speed teardown must close admission while retaining detour/trampoline storage for process lifetime");
    Expect(speedHackSource.find("UpdateAndRender_SpeedHack") == std::string::npos &&
               speedHackSource.find("g_fieldDetour") == std::string::npos &&
               speedHackSource.find("GetAsyncKeyState(VK_F12)") == std::string::npos &&
               speedFrame.body.find("GetAsyncKeyState('K')") != std::string::npos &&
               speedHackSource.find("cmp dword ptr [g_stopRequested], 0") !=
                   std::string::npos &&
               speedHackSource.find("cmp dword ptr [edx], 0") != std::string::npos,
           "Speed must remove the field hook, retain zero F12 ownership, and make global 8x reject stop/native overlap");
}

void TestDialogSkipPackedPublication() {
    using FfxHooks::ClearDialogSkipPublishedSpeedRequest;
    using FfxHooks::DialogSkipPublishedStateEffective;
    using FfxHooks::PackDialogSkipPublishedState;
    using FfxHooks::ResolveDialogSkipPublishedRequests;
    using FfxHooks::StopDialogSkipPublishedState;
    using FfxHooks::UnpackDialogSkipPublishedState;

    const uint32_t ready = PackDialogSkipPublishedState(true, false, false, false);
    const uint32_t manual = ResolveDialogSkipPublishedRequests(ready, true, false);
    const uint32_t speed = ResolveDialogSkipPublishedRequests(ready, false, true);
    const uint32_t both = ResolveDialogSkipPublishedRequests(ready, true, true);
    Expect(DialogSkipPublishedStateEffective(manual) &&
               DialogSkipPublishedStateEffective(speed) &&
               DialogSkipPublishedStateEffective(both),
           "manual and Speed 8x must independently OR into one atomic dialog bypass state");

    const uint32_t manualAfterFocus = ClearDialogSkipPublishedSpeedRequest(both);
    const FfxHooks::DialogSkipPublishedState decodedManual =
        UnpackDialogSkipPublishedState(manualAfterFocus);
    Expect(decodedManual.hookReady && !decodedManual.stopped &&
               decodedManual.manualRequested && !decodedManual.speedHack8Requested &&
               DialogSkipPublishedStateEffective(manualAfterFocus),
           "focus loss must atomically remove only Speed ownership and preserve manual Dialog Skip");

    const uint32_t stopped = StopDialogSkipPublishedState(both);
    const FfxHooks::DialogSkipPublishedState decodedStopped =
        UnpackDialogSkipPublishedState(stopped);
    Expect(decodedStopped.hookReady && decodedStopped.stopped &&
               !decodedStopped.manualRequested && !decodedStopped.speedHack8Requested &&
               !DialogSkipPublishedStateEffective(stopped),
           "the stop transition must close admission and clear both dialog request sources atomically");

    const uint32_t attemptedRearm =
        ResolveDialogSkipPublishedRequests(stopped, true, true);
    const FfxHooks::DialogSkipPublishedState decodedRearm =
        UnpackDialogSkipPublishedState(attemptedRearm);
    Expect(decodedRearm.stopped && !decodedRearm.manualRequested &&
               !decodedRearm.speedHack8Requested &&
               !DialogSkipPublishedStateEffective(attemptedRearm),
           "a stopped dialog state must be absorbing against stale Present publications");

    const uint32_t unavailable = PackDialogSkipPublishedState(false, false, true, true);
    Expect(!DialogSkipPublishedStateEffective(unavailable),
           "request bits must not bypass voice before the physical hook is ready");
}

void TestSpeedHackRelocatedTargetValidation() {
    using FfxHooks::SpeedHackTargetStatus;
    using FfxHooks::SpeedHackTargetValidation;
    using FfxHooks::ValidateSpeedHackTargetSignature;

    constexpr uint8_t preferred[12] = {
        0x80, 0x3D, 0xF5, 0x9C, 0xCC, 0x00,
        0x00, 0x56, 0x8B, 0xF1, 0x74, 0x06,
    };
    constexpr uint8_t relocated2F[12] = {
        0x80, 0x3D, 0xF5, 0x9C, 0xBB, 0x00,
        0x00, 0x56, 0x8B, 0xF1, 0x74, 0x06,
    };

    const SpeedHackTargetValidation preferredResult =
        ValidateSpeedHackTargetSignature(preferred, 12, 0x00400000u);
    Expect(preferredResult.status == SpeedHackTargetStatus::Match,
           "preferred-base Speed bytes must match their exact relocation value");
    const SpeedHackTargetValidation relocatedResult =
        ValidateSpeedHackTargetSignature(relocated2F, 12, 0x002F0000u);
    Expect(relocatedResult.status == SpeedHackTargetStatus::Match,
           "ASLR-loaded Speed bytes must match the relocated absolute operand");
    Expect(relocatedResult.expectedOperand == 0x00BB9CF5u &&
               relocatedResult.actualOperand == 0x00BB9CF5u &&
               relocatedResult.mismatchOffset == 0xFFu,
           "ASLR-loaded validation must report literal expected/actual operand 0x00BB9CF5");

    const SpeedHackTargetValidation preferredAtRelocatedBase =
        ValidateSpeedHackTargetSignature(preferred, 12, 0x002F0000u);
    Expect(preferredAtRelocatedBase.status == SpeedHackTargetStatus::Mismatch &&
               preferredAtRelocatedBase.mismatchOffset == 4u &&
               preferredAtRelocatedBase.expectedOperand == 0x00BB9CF5u &&
               preferredAtRelocatedBase.actualOperand == 0x00CC9CF5u,
           "preferred bytes must fail closed against the relocated image base");
    const SpeedHackTargetValidation relocatedAtPreferredBase =
        ValidateSpeedHackTargetSignature(relocated2F, 12, 0x00400000u);
    Expect(relocatedAtPreferredBase.status == SpeedHackTargetStatus::Mismatch &&
               relocatedAtPreferredBase.mismatchOffset == 4u &&
               relocatedAtPreferredBase.expectedOperand == 0x00CC9CF5u &&
               relocatedAtPreferredBase.actualOperand == 0x00BB9CF5u,
           "relocated bytes must fail closed against the preferred image base");

    for (size_t byteIndex = 0; byteIndex < 12; ++byteIndex) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mutated[12] = {};
            memcpy(mutated, relocated2F, sizeof(mutated));
            mutated[byteIndex] ^= static_cast<uint8_t>(1u << bit);
            const SpeedHackTargetValidation result =
                ValidateSpeedHackTargetSignature(mutated, 12, 0x002F0000u);
            Expect(result.status == SpeedHackTargetStatus::Mismatch &&
                       result.mismatchOffset == byteIndex,
                   "every single-bit mutation must fail at its exact first byte");
        }
    }

    Expect(ValidateSpeedHackTargetSignature(nullptr, 12, 0x00400000u).status ==
               SpeedHackTargetStatus::NullInput,
           "null Speed target bytes must fail closed");
    const SpeedHackTargetValidation shortResult =
        ValidateSpeedHackTargetSignature(preferred, 11, 0x00400000u);
    Expect(shortResult.status == SpeedHackTargetStatus::WrongLength &&
               shortResult.actualOperand == 0u,
           "an 11-byte Speed target must fail without reading the operand");
    uint8_t longBytes[13] = {};
    memcpy(longBytes, preferred, sizeof(preferred));
    const SpeedHackTargetValidation longResult =
        ValidateSpeedHackTargetSignature(longBytes, 13, 0x00400000u);
    Expect(longResult.status == SpeedHackTargetStatus::WrongLength &&
               longResult.actualOperand == 0u,
           "a 13-byte Speed target must fail without reading the operand");
    const SpeedHackTargetValidation overflowResult =
        ValidateSpeedHackTargetSignature(preferred, 12, UINTPTR_MAX);
    Expect(overflowResult.status == SpeedHackTargetStatus::BaseOverflow &&
               overflowResult.actualOperand == 0u,
           "an overflowing loaded base must fail before reading the target operand");

    uint8_t e9Prefix[12] = {};
    memcpy(e9Prefix, relocated2F, sizeof(e9Prefix));
    e9Prefix[0] = 0xE9u;
    const SpeedHackTargetValidation e9Result =
        ValidateSpeedHackTargetSignature(e9Prefix, 12, 0x002F0000u);
    Expect(e9Result.status == SpeedHackTargetStatus::DetourLikePrefix &&
               e9Result.mismatchOffset == 0u,
           "an E9 Speed target prefix must be classified and rejected as detour-like");
    uint8_t ff25Prefix[12] = {};
    memcpy(ff25Prefix, relocated2F, sizeof(ff25Prefix));
    ff25Prefix[0] = 0xFFu;
    ff25Prefix[1] = 0x25u;
    const SpeedHackTargetValidation ff25Result =
        ValidateSpeedHackTargetSignature(ff25Prefix, 12, 0x002F0000u);
    Expect(ff25Result.status == SpeedHackTargetStatus::DetourLikePrefix &&
               ff25Result.mismatchOffset == 0u,
           "an FF 25 Speed target prefix must be classified and rejected as detour-like");

    std::string speedHackSource;
    std::string dllmainSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\SpeedHackHook.cpp"), speedHackSource) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmainSource),
           "Speed install sources must be readable for fail-closed order contracts");
    const SourceBlock installBody = SourceFunctionBody(
        speedHackSource,
        "bool InstallSpeedHackHook(uintptr_t moduleBase, bool dialogBypassReady, void* logFn, SpeedHackInstallStatus* statusOut)");
    const std::string installCode = SourceCodeOnly(installBody.body);
    Expect(installBody.Valid() && SourceTokensInOrder(installCode, {
               "F8Runtime::ParseExecutableIdentity(",
               "F8Runtime::ValidateImageRange(",
               "RVA_FFX_NATIVE_SPEED_BOOSTER",
               "RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY",
               "ValidateSpeedHackGlobalTargetSignature(",
               "new PLH::x86Detour",
           }),
           "Speed install must validate profile, native ranges, and relocated global bytes before detouring");
    Expect(installBody.Valid() &&
               installCode.find("SpeedHackTargetSignatureMatches") == std::string::npos,
           "Speed install must never restore the old literal disk-byte matcher");

    const SourceBlock installHooksBody =
        SourceFunctionBody(dllmainSource, "static void InstallHooks()");
    const SourceBlock speedStatusBranch = SourceBlockAfterToken(
        installHooksBody.body, "switch (speedHackStatus)");
    const std::string statusCode = SourceCodeOnly(speedStatusBranch.body);
    Expect(installHooksBody.Valid() && speedStatusBranch.Valid() &&
               installHooksBody.body.find(
                   "InstallSpeedHackHook(g_base, dialogSkipReady, LogLine, &speedHackStatus)") != std::string::npos &&
               SourceTokensInOrder(statusCode, {
                   "case FfxHooks::SpeedHackInstallStatus::UnsupportedProfile:",
                   "case FfxHooks::SpeedHackInstallStatus::NativeStateOutOfRange:",
                   "case FfxHooks::SpeedHackInstallStatus::NativeAvailabilityOutOfRange:",
                   "FfxHooks::F8RuntimeAvailability::UnsupportedBuild",
                   "case FfxHooks::SpeedHackInstallStatus::GlobalSignatureMismatch:",
                   "case FfxHooks::SpeedHackInstallStatus::GlobalDetourLikePrefix:",
                   "FfxHooks::F8RuntimeAvailability::SignatureMismatch",
                   "case FfxHooks::SpeedHackInstallStatus::UnXModuleConflict:",
                   "FfxHooks::F8RuntimeAvailability::Conflict",
                   "case FfxHooks::SpeedHackInstallStatus::GlobalDetourFailed:",
                   "case FfxHooks::SpeedHackInstallStatus::PolyHookUnavailable:",
                   "FfxHooks::F8RuntimeAvailability::ProducerUnavailable",
               }),
           "dllmain must map Speed range, signature, ownership conflict, and producer failures distinctly");
}

void TestSpeedHackGlobalTargetValidation() {
    using FfxHooks::SpeedHackTargetStatus;
    using FfxHooks::ValidateSpeedHackGlobalTargetSignature;

    constexpr uint8_t preferred[16] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA4, 0x00, 0x00,
        0x00, 0xA1, 0xD8, 0x13, 0xC6, 0x00, 0x33, 0xC5,
    };
    constexpr uint8_t relocated2F[16] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA4, 0x00, 0x00,
        0x00, 0xA1, 0xD8, 0x13, 0xB5, 0x00, 0x33, 0xC5,
    };

    const FfxHooks::SpeedHackTargetValidation preferredResult =
        ValidateSpeedHackGlobalTargetSignature(preferred, sizeof(preferred), 0x00400000u);
    Expect(preferredResult.status == SpeedHackTargetStatus::Match &&
               preferredResult.expectedOperand == 0x00C613D8u,
           "preferred field-service-tick bytes must match their exact relocated operand");
    const FfxHooks::SpeedHackTargetValidation relocatedResult =
        ValidateSpeedHackGlobalTargetSignature(relocated2F, sizeof(relocated2F), 0x002F0000u);
    Expect(relocatedResult.status == SpeedHackTargetStatus::Match &&
               relocatedResult.expectedOperand == 0x00B513D8u &&
               relocatedResult.actualOperand == 0x00B513D8u,
           "ASLR-loaded field-service-tick bytes must match the relocated security-cookie operand");

    for (size_t byteIndex = 0; byteIndex < sizeof(relocated2F); ++byteIndex) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mutated[sizeof(relocated2F)] = {};
            memcpy(mutated, relocated2F, sizeof(mutated));
            mutated[byteIndex] ^= static_cast<uint8_t>(1u << bit);
            const FfxHooks::SpeedHackTargetValidation result =
                ValidateSpeedHackGlobalTargetSignature(
                    mutated, sizeof(mutated), 0x002F0000u);
            Expect(result.status == SpeedHackTargetStatus::Mismatch &&
                       result.mismatchOffset == byteIndex,
                   "every field-service-tick signature bit must fail at its first changed byte");
        }
    }

    uint8_t detoured[sizeof(relocated2F)] = {};
    memcpy(detoured, relocated2F, sizeof(detoured));
    detoured[0] = 0xE9u;
    Expect(ValidateSpeedHackGlobalTargetSignature(
               detoured, sizeof(detoured), 0x002F0000u).status ==
               SpeedHackTargetStatus::DetourLikePrefix,
           "an externally detoured global tick must be rejected instead of chained implicitly");
    Expect(RVA_FFX_SCENE_FIELD_SERVICE_TICK == 0x00420C00u,
           "the composed 8x backend must retain the exact supported service-tick RVA");

    std::string speedHackSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\SpeedHackHook.cpp"), speedHackSource),
           "Speed Hack source must be readable for the x86 bridge contract");
    const SourceBlock bridge = SourceFunctionBody(
        speedHackSource, "__declspec(naked) void SpeedHackGlobalTickBridge()");
    const std::string bridgeCode = SourceCodeOnly(bridge.body);
    Expect(bridge.Valid() &&
               bridgeCode.find("pushfd") != std::string::npos &&
               bridgeCode.find("pushad") != std::string::npos &&
               bridgeCode.find("fld dword ptr [esp + 40]") != std::string::npos &&
               bridgeCode.find("fstp dword ptr [esp + 40]") != std::string::npos &&
               bridgeCode.find("popad") != std::string::npos &&
               bridgeCode.find("popfd") != std::string::npos &&
               bridgeCode.find("jmp dword ptr [g_globalTickTrampoline]") !=
                    std::string::npos,
           "the global tick bridge must preserve GPR live-ins, scale the displaced stack argument, and tail-jump");
    Expect(bridge.Valid() && SourceTokensInOrder(bridgeCode, {
               "mov eax, dword ptr [g_publishedRouteWord]",
               "cmp edx, 3",
               "mov edx, dword ptr [g_nativeStateAddress]",
               "cmp dword ptr [edx], 0",
               "fld dword ptr [esp + 40]",
           }),
           "the bridge must decode custom 8x before native-zero enforcement so native 2x/4x passes through");
}

void TestDialogSkipCorrectedTargetAndOwnership() {
    using FfxHooks::DialogSkipTargetStatus;
    using FfxHooks::ShouldBypassDialogVoice;
    using FfxHooks::ValidateDialogSkipTargetSignature;

    Expect(!ShouldBypassDialogVoice(false, false) &&
               ShouldBypassDialogVoice(true, false) &&
               ShouldBypassDialogVoice(false, true) &&
               ShouldBypassDialogVoice(true, true),
           "manual Dialog Skip and composed 8x must remain independent OR-owned bypass sources");

    constexpr uint8_t target[16] = {
        0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x0C, 0x00, 0x57,
        0x8B, 0xF9, 0x0F, 0x84, 0xBE, 0x02, 0x00, 0x00,
    };
    const FfxHooks::DialogSkipTargetValidation exact =
        ValidateDialogSkipTargetSignature(target, sizeof(target));
    Expect(exact.status == DialogSkipTargetStatus::Match && exact.mismatchOffset == 0xFFu,
           "the corrected voice-read entry must accept its exact supported 16-byte prefix");
    for (size_t byteIndex = 0; byteIndex < sizeof(target); ++byteIndex) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mutated[sizeof(target)] = {};
            memcpy(mutated, target, sizeof(mutated));
            mutated[byteIndex] ^= static_cast<uint8_t>(1u << bit);
            const FfxHooks::DialogSkipTargetValidation result =
                ValidateDialogSkipTargetSignature(mutated, sizeof(mutated));
            Expect(result.status == DialogSkipTargetStatus::Mismatch &&
                       result.mismatchOffset == byteIndex,
                   "every corrected voice-read signature bit must fail at its first changed byte");
        }
    }
    uint8_t detoured[sizeof(target)] = {};
    memcpy(detoured, target, sizeof(detoured));
    detoured[0] = 0xFFu;
    detoured[1] = 0x25u;
    Expect(ValidateDialogSkipTargetSignature(detoured, sizeof(detoured)).status ==
               DialogSkipTargetStatus::DetourLikePrefix,
           "an externally detoured voice-read entry must be rejected instead of double-owned");
    Expect(RVA_FFX_FMODVOICE_READ_EVENT_DATA == 0x0030AEC0u,
           "Dialog Skip must target the real UnX voice-read entry, not stale RVA 0x30B040");

    std::string dialogSource;
    std::string dllmainSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\DialogSkipHook.cpp"), dialogSource) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmainSource),
           "Dialog Skip and dllmain sources must be readable for ownership contracts");
    const SourceBlock callback = SourceFunctionBody(
        dialogSource,
        "static int __fastcall ReadEventData_DialogSkipHook(void* this_, void* edx, unsigned int evtId, uintptr_t context)");
    const SourceBlock frameTick =
        SourceFunctionBody(dialogSource, "void DialogSkipFrameTick(bool speedHack8Active)");
    const std::string callbackCode = SourceCodeOnly(callback.body);
    const std::string tickCode = SourceCodeOnly(frameTick.body);
    Expect(callback.Valid() && callbackCode.find("Config::") == std::string::npos &&
               callbackCode.find("HookLog(") == std::string::npos &&
               callbackCode.find("AtomicLoadDialogState()") != std::string::npos &&
               callbackCode.find("DialogSkipPublishedStateEffective") != std::string::npos &&
               callbackCode.find("return 0") != std::string::npos,
           "the voice callback must consume one packed atomic state and return zero only when it resolves effective");
    Expect(frameTick.Valid() &&
               frameTick.body.find("Config::GetBool(\"input.dialog_skip\", false)") !=
                   std::string::npos &&
               tickCode.find("speedHack8Active") != std::string::npos &&
               tickCode.find("ResolveDialogSkipPublishedRequests") != std::string::npos &&
               tickCode.find("InterlockedCompareExchange") != std::string::npos,
           "Present must merge manual and Speed 8x through one compare-exchanged publication");

    const SourceBlock focusClear = SourceFunctionBody(
        dialogSource, "void DialogSkipNotifySpeedHack8Inactive() noexcept");
    const SourceBlock stop = SourceFunctionBody(
        dialogSource, "void RequestDialogSkipStop() noexcept");
    const std::string focusClearCode = SourceCodeOnly(focusClear.body);
    const std::string stopCode = SourceCodeOnly(stop.body);
    Expect(focusClear.Valid() &&
               focusClearCode.find("InterlockedAnd") != std::string::npos &&
               focusClearCode.find("kDialogSkipSpeed8Mask") != std::string::npos &&
               focusClearCode.find("Config::") == std::string::npos &&
               focusClearCode.find("HookLog(") == std::string::npos,
           "focus loss must clear only the packed Speed 8x request without I/O");
    Expect(stop.Valid() && SourceTokensInOrder(stopCode, {
               "InterlockedOr",
               "kDialogSkipStoppedMask",
               "InterlockedAnd",
               "kDialogSkipManualMask | kDialogSkipSpeed8Mask",
           }),
           "Dialog stop must publish its dominating bit before clearing both request sources");

    const SourceBlock install = SourceFunctionBody(
        dialogSource,
        "bool InstallDialogSkipHook(uintptr_t moduleBase, void* logFn, DialogSkipInstallStatus* statusOut)");
    const std::string installCode = SourceCodeOnly(install.body);
    Expect(install.Valid() && SourceTokensInOrder(installCode, {
               "F8Runtime::ParseExecutableIdentity(",
               "F8Runtime::ValidateImageRange(",
               "ValidateDialogSkipTargetSignature(target, kDialogSkipTargetLength)",
               "new PLH::x86Detour",
           }),
           "Dialog Skip must validate profile, range, and exact corrected entry before detouring");
    const size_t dialogTry = installCode.find("try");
    const size_t dialogHook = installCode.find("->hook()");
    const size_t dialogCatch = installCode.find("catch (...)");
    Expect(dialogTry != std::string::npos && dialogHook != std::string::npos &&
               dialogCatch != std::string::npos && dialogTry < dialogHook &&
               dialogHook < dialogCatch && installCode.find("unHook()") == std::string::npos &&
               installCode.find("delete static_cast<PLH::x86Detour*>") == std::string::npos,
           "Dialog installation must guard hook() exceptions and retain ambiguous failure state");

    const SourceBlock removeHooks = SourceFunctionBody(dllmainSource, "static void RemoveHooks()");
    Expect(removeHooks.Valid() && SourceTokensInOrder(SourceCodeOnly(removeHooks.body), {
               "FfxHooks::RemoveSpeedHackHook()",
               "FfxHooks::RemoveDialogSkipHook()",
           }),
           "normal-context teardown must neutralize Speed before removing the shared dialog owner");
    Expect(dllmainSource.find("FfxHooks::RequestSpeedHackStop();") != std::string::npos &&
               dllmainSource.find("FfxHooks::RequestDialogSkipStop();") != std::string::npos,
           "DllMain detach must only publish lock-free Speed and Dialog stop requests");
}

void TestCatalogMetadataAndInvariants() {
    const ExpectedCatalogRow expected[] = {
        {"System", "Borderless window", "window.borderless", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Keep cursor in game", "window.clip_cursor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Hide idle cursor", "window.hide_cursor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Performance display", "diagnostics.performance", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Free battle camera", "camera.free_look", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Freeze field scene", "camera.freeze_scene", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"System", "Native Hooks", "plugins.dinput8", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, true, F8Activation::ReadOnly, F8ApplyMode::None},
        {"System", "External render module", "plugins.dxgi", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::ReadOnly, F8ApplyMode::None},
        {"System", "External UnX module", "plugins.unx", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::ReadOnly, F8ApplyMode::None},
        {"System", "Native diagnostics", "plugins.ffx_probe", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::ReadOnly, F8ApplyMode::None},
        {"Boosters", "Permanent Sensor", "boosters.permanent_sensor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Boosters", "Playable Seymour", "boosters.playable_seymour", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Boosters", "Speed Hack", "boosters.speed_hack", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::ConfigPolled},
        {"Boosters", "SpeedHack FMV acceleration", "boosters.speed_hack_fmv", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Boosters", "Entire Party Earns AP", "boosters.entire_party_earns_ap", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Invincible Party", "cheats.invincible_party", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Invincible Enemies", "cheats.invincible_enemies", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Always Overdrive", "cheats.always_overdrive", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Always Critical", "cheats.always_critical", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Damage 99999", "cheats.damage_value", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Always Rare Drop", "cheats.always_rare_drop", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "AP Multiplier", "cheats.ap_100x", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Cheats", "Gil Multiplier", "cheats.gil_100x", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Dev", "FieldScout Master", "field_scout.master", "f8_authority.field_scout_master", "labs.field_scout", "FFXHOOKS_ENABLE_FIELD_SCOUT", "field_scout.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Dev", "FieldScout Heavy", "field_scout.heavy", "f8_authority.field_scout_heavy", "labs.field_scout_heavy", "FFXHOOKS_FIELD_SCOUT_HEAVY", "field_scout_heavy.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Dev", "FieldScout Max", "field_scout.max", "f8_authority.field_scout_max", "labs.field_scout_max", "FFXHOOKS_FIELD_SCOUT_MAX", "field_scout_max.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Dev", "FieldScout Ultra", "field_scout.ultra", "f8_authority.field_scout_ultra", "labs.field_scout_ultra", "FFXHOOKS_FIELD_SCOUT_ULTRA", "field_scout_ultra.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Arena+", "Arena+ Master", "arena_plus.master", "f8_authority.arena_plus_master", nullptr, "FFXHOOKS_ENABLE_ARENA_PLUS", "arena_plus.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Arena+", "Arena+ Compose F7", "arena_plus.compose_f7", "f8_authority.arena_plus_compose_f7", "labs.arena_plus_compose_f7", "FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7", "arena_plus_compose_f7.flag", "FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7", nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::ConfigPolled},
        {"Arena+", "Bypass Progression", "arena_plus.unlock_all", "f8_authority.arena_plus_unlock_all", nullptr, "FFXHOOKS_ARENAPLUS_UNLOCK_ALL", "arena_plus_unlock_all.flag", nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::ConfigPolled},
        {"Arena+", "Arena+ Victory Hook", "arena_plus.victory_hook", "f8_authority.arena_plus_victory_hook", nullptr, "FFXHOOKS_ENABLE_ARENA_PLUS_VICTORY_HOOK", "arena_plus_victory_hook.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Arena+", "Arena+ Resolver Log", "arena_plus.resolver_log", "f8_authority.arena_plus_resolver_log", nullptr, "FFXHOOKS_ENABLE_ARENA_PLUS_RESOLVER_LOG", "arena_plus_resolver_log.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Arena+", "Arena+ Music", "arena_plus.music", "f8_authority.arena_plus_music", "music.arena_plus", "FFXHOOKS_ARENAPLUS_MUSIC", "arena_plus_music.flag", "FFXHOOKS_DISABLE_ARENA_PLUS_MUSIC", "arena_plus_music.flag.off", "music.flag.off", false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Input", "Block Windows Key", "input.block_windows_key", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Input", "Fix Background Input", "input.fix_background_input", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Input", "Filter IME", "input.filter_ime", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::RuntimeAcknowledged},
        {"Input", "Dialog Skip", "input.dialog_skip", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, F8Activation::Live, F8ApplyMode::ConfigPolled},
        {"Dev", "Fastload Autosave", "development.fastload_autosave", "f8_authority.fastload_autosave", nullptr, "FFXHOOKS_ENABLE_FASTLOAD_AUTOSAVE", "fastload_autosave.flag", "FFXHOOKS_DISABLE_FASTLOAD_AUTOSAVE", "fastload_autosave.flag.off", nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Nova Super Damage", "labs.nova_super_damage", "f8_authority.lab_nova_super_damage", nullptr, "FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE", "nova_super_damage.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Ronso Mana", "labs.kimahri_ronso_mana", "f8_authority.lab_kimahri_ronso_mana", nullptr, "FFXHOOKS_ENABLE_RONSO_MANA", "kimahri_ronso_mana.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Equipment Workshop", "labs.equipment_workshop", "f8_authority.equipment_workshop", nullptr, "FFXHOOKS_EQUIPMENT_WORKSHOP", "equipment_workshop.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Grid Teach", "labs.grid_teach", "f8_authority.lab_grid_teach", nullptr, "FFXHOOKS_GRID_TEACH", "grid_teach.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Lancet Dual Grant", "labs.kimahri_lancet_dual_grant", "f8_authority.lab_kimahri_lancet_dual_grant", nullptr, "FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT", "kimahri_lancet_dual_grant.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Item Stack Cap", "labs.item_stack_cap", "f8_authority.lab_item_stack_cap", nullptr, "FFXHOOKS_ENABLE_ITEM_STACK_CAP", "item_stack_cap_255.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
        {"Reforge", "Double/Triple Drop", "labs.double_triple_drop", "f8_authority.lab_double_triple_drop", nullptr, "FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP", "double_triple_drop.flag", nullptr, nullptr, nullptr, false, F8Activation::RestartRequired, F8ApplyMode::None},
    };
    const char* expectedTabs[] = {"System", "Boosters", "Cheats", "Arena+", "Input", "Dev", "Reforge"};
    const size_t expectedTabCounts[] = {10, 5, 8, 6, 4, 5, 7};

    std::string speedHackSource;
    std::string dllmainSource;
    std::string configSource;
    std::string trackedIniSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\SpeedHackHook.cpp"), speedHackSource) &&
               ReadWholeFile(RuntimeSourcePath("shared\\Config.cpp"), configSource) &&
               ReadWholeFile(TrackedIniPath(), trackedIniSource),
           "Speed Hack consumer source must be readable from the RT0 test source path");
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmainSource),
           "dllmain source must be readable for the Speed Hack include contract");

    const SourceBlock speedFrameBody =
        SourceFunctionBody(speedHackSource, "void SpeedHackFrameTick()");
    const std::string speedFrameCode = SourceCodeOnly(speedFrameBody.body);
    Expect(speedFrameBody.Valid() &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(VK_CONTROL)") == 1 &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(VK_SHIFT)") == 1 &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(VK_MENU)") == 1 &&
               CountSourceToken(speedFrameBody.body, "GetAsyncKeyState('K')") == 1 &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(") == 4 &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(VK_F12)") == 0 &&
               CountSourceToken(speedFrameCode, "GetAsyncKeyState(VK_F6)") == 0,
           "Present-owned Speed control must sample only Ctrl, Shift, Alt, and K once per frame");
    Expect(speedFrameBody.Valid() && SourceTokensInOrder(speedFrameCode, {
               "g_stopRequested",
               "DialogSkipFrameTick(false)",
               "InterlockedExchange(&g_presentProducerActive, 0)",
               "return;",
           }),
           "a stopped or terminally unavailable Speed bundle must keep servicing manual Dialog Skip");

    Expect(speedHackSource.find("UpdateAndRender_SpeedHack") == std::string::npos &&
               speedHackSource.find("g_fieldDetour") == std::string::npos &&
               speedHackSource.find("RVA_FFX_FIELD_UPDATE_AND_RENDER") == std::string::npos,
           "2x/4x must remove the old field detour and use only the native Standard boost DWORD");

    const SourceBlock presentBody = SourceFunctionBody(
        dllmainSource,
        "static HRESULT STDMETHODCALLTYPE AuroraD3DPresentShim(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)");
    const std::string presentCode = SourceCodeOnly(presentBody.body);
    Expect(presentBody.Valid() && SourceTokensInOrder(presentCode, {
               "FpsScoutOnPresent(syncInterval, flags)",
               "FfxHooks::SpeedHackFrameTick()",
               "g_auroraD3DRenderEnabled",
           }),
           "Speed control must run from Present before optional visual-render admission");
    Expect(presentCode.find("const bool speedBaseReady =") != std::string::npos &&
               presentCode.find("speedRuntime.phase == FfxHooks::SpeedHackRuntimePhase::Unavailable") !=
                   std::string::npos &&
               presentCode.find(
                   "speedRuntime.globalTickHookReady && speedRuntime.dialogBypassReady") ==
                   std::string::npos,
           "Present status publication must keep native 2x/4x available when only the 8x voice dependency is absent");
    const SourceBlock publishPresent = SourceBlockAfterToken(
        dllmainSource,
        "static void PublishAuroraD3DPresentResult(");
    const std::string publishPresentCode = SourceCodeOnly(publishPresent.body);
    Expect(publishPresent.Valid() && SourceTokensInOrder(publishPresentCode, {
               "PresentHookResult::PublishTerminal",
               "InterlockedExchange(&g_auroraD3DPresentTerminal, 1)",
               "RequestSpeedHackStop()",
               "RequestDialogSkipStop()",
               "PublishResolvedF8Status",
               "F8RuntimeAvailability::ProducerUnavailable",
               "PublishResolvedF8Status",
           }) &&
               publishPresent.body.find("\"input.dialog_skip\"") !=
                   std::string::npos &&
               publishPresent.body.find("\"boosters.speed_hack\"") !=
                   std::string::npos,
           "a late terminal Present failure must close and depublish both Present-owned consumers");

    const FfxHooks::F8FlagSpec* speedHack =
        FfxHooks::FindF8Flag("boosters.speed_hack");
    Expect(speedHack && strcmp(speedHack->label, "Speed Hack") == 0 &&
               strcmp(speedHack->help,
                        "LIVE - Native 2/4; field scenes 8; optional FMV acceleration.") == 0,
           "Speed Hack catalog row must name native/field-scene scope and the RT2-pending FMV boundary");

    const char* speedHackIncludeToken = "#include \"hooks/SpeedHackHook.h\"";
    const size_t speedHackInclude = dllmainSource.find(speedHackIncludeToken);
    const size_t speedHackIncludeEnd =
        speedHackInclude == std::string::npos
            ? std::string::npos
            : dllmainSource.find('\n', speedHackInclude);
    const std::string speedHackIncludeLine =
        speedHackInclude == std::string::npos
            ? std::string{}
            : dllmainSource.substr(
                  speedHackInclude,
                  speedHackIncludeEnd == std::string::npos
                      ? std::string::npos
                      : speedHackIncludeEnd - speedHackInclude);
    Expect(speedHackInclude != std::string::npos &&
               speedHackIncludeLine.find("Ctrl+Shift+K") != std::string::npos &&
               speedHackIncludeLine.find("F12-hold") == std::string::npos &&
               speedHackIncludeLine.find("F6-hold") == std::string::npos,
           "Speed Hack dllmain include comment must name Ctrl+Shift+K, not F12/F6");

    const SourceBlock installHooksBody =
        SourceFunctionBody(dllmainSource, "static void InstallHooks()");
    const size_t dialogSkipReady = installHooksBody.body.find("const bool dialogSkipReady =");
    const SourceBlock dashboardReadyInstall =
        SourceBlockAfterToken(installHooksBody.body, "if (dashboardReady)");
    const size_t speedInstall =
        installHooksBody.body.find(
            "InstallSpeedHackHook(g_base, dialogSkipReady, LogLine, &speedHackStatus)");
    const size_t dialogInstall =
        installHooksBody.body.find("InstallDialogSkipHook(g_base, LogLine, &dialogSkipStatus)");
    const size_t speedPublication = installHooksBody.body.find("\"boosters.speed_hack\"");
    Expect(installHooksBody.Valid() && dialogSkipReady != std::string::npos &&
               dashboardReadyInstall.Valid() &&
               dialogInstall != std::string::npos && speedInstall != std::string::npos &&
               speedPublication != std::string::npos && dialogInstall < speedInstall &&
               (dialogInstall < dashboardReadyInstall.open ||
                dialogInstall > dashboardReadyInstall.close) &&
               (speedInstall < dashboardReadyInstall.open ||
                speedInstall > dashboardReadyInstall.close) &&
               speedInstall < speedPublication,
           "the corrected dialog owner and Speed bundle must install outside dashboard readiness in dependency order");

    const size_t profileValidation = speedHackSource.find("F8Runtime::ParseExecutableIdentity");
    const size_t supportedProfile = speedHackSource.find("F8Runtime::IsSupportedExecutable");
    const size_t rangeValidation = speedHackSource.find("F8Runtime::ValidateImageRange");
    const size_t nativeStateRange = speedHackSource.find("RVA_FFX_NATIVE_SPEED_BOOSTER");
    const size_t nativeAvailabilityRange = speedHackSource.find(
        "RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY");
    const size_t globalSignatureValidation = speedHackSource.find(
        "ValidateSpeedHackGlobalTargetSignature(");
    const size_t detourConstruction = speedHackSource.find("new PLH::x86Detour");
    Expect(profileValidation != std::string::npos && supportedProfile != std::string::npos &&
               rangeValidation != std::string::npos && nativeStateRange != std::string::npos &&
               nativeAvailabilityRange != std::string::npos &&
               globalSignatureValidation != std::string::npos &&
               detourConstruction != std::string::npos &&
               profileValidation < supportedProfile && supportedProfile < rangeValidation &&
               rangeValidation <= nativeStateRange &&
               nativeStateRange < nativeAvailabilityRange &&
               nativeAvailabilityRange < globalSignatureValidation &&
               globalSignatureValidation < detourConstruction,
           "Speed Hack must validate profile, both native RVAs, and the relocated field-service target before detouring");
    const SourceBlock speedInstallBody = SourceFunctionBody(
        speedHackSource,
        "bool InstallSpeedHackHook(uintptr_t moduleBase, bool dialogBypassReady, void* logFn, SpeedHackInstallStatus* statusOut)");
    const std::string speedInstallCode = SourceCodeOnly(speedInstallBody.body);
    Expect(speedInstallBody.Valid() &&
               CountSourceToken(speedInstallCode, "->hook()") == 1 &&
               CountSourceToken(speedInstallCode, "try") == 1 &&
               CountSourceToken(speedInstallCode, "catch (...)") == 1 &&
               speedInstallCode.find("unHook()") == std::string::npos &&
               speedInstallCode.find("delete static_cast<PLH::x86Detour*>") ==
                   std::string::npos &&
               speedInstallCode.find("g_terminalInstallStatus") != std::string::npos,
           "the sole global Speed detour must guard hook() and retain an ambiguous failure gateway");

    Expect(installHooksBody.body.find(
               "const bool dialogSkipOperational = dialogSkipReady && f8PresentProducerOperational") !=
               std::string::npos &&
               installHooksBody.body.find(
               "const bool speedHackOperational = speedHackReady && f8PresentProducerOperational") !=
               std::string::npos &&
               installHooksBody.body.find(
               "dialogSkipReady && !f8PresentProducerOperational") != std::string::npos &&
               installHooksBody.body.find(
               "speedHackReady && !f8PresentProducerOperational") != std::string::npos,
           "Speed and Dialog catalog availability must require their shared Present producer");
    Expect(SourceTokensInOrder(SourceCodeOnly(installHooksBody.body), {
               "PublishResolvedF8Status",
               "speedHackAvailability",
               "speedHackOperational",
               "InterlockedCompareExchange(&g_auroraD3DPresentTerminal, 0, 0)",
               "RequestSpeedHackStop()",
               "RequestDialogSkipStop()",
               "F8RuntimeAvailability::ProducerUnavailable",
           }),
           "post-install reconciliation must make a raced terminal Present publication absorbing");
    Expect(configSource.find(
               "# Ctrl+Shift+K cycles 1x/2x/4x/8x while armed") != std::string::npos &&
               configSource.find(
                   "# Native 2x/4x is armed; this hook does not infer per-scene application") !=
                   std::string::npos &&
               configSource.find(
                    "# Custom 8x targets the reviewed field-scene service tick; broader coverage needs RT2") !=
                   std::string::npos &&
               configSource.find("# FMV acceleration is separately opt-in and requires restart") !=
                   std::string::npos &&
               configSource.find("# factor and speed_step are retained as ignored legacy keys") !=
                   std::string::npos &&
               trackedIniSource.find(
                   "# Native 2x/4x is armed; this hook does not infer per-scene application") !=
                   std::string::npos &&
               trackedIniSource.find(
                    "# Custom 8x targets the reviewed field-scene service tick; broader coverage needs RT2") !=
                   std::string::npos &&
               trackedIniSource.find("# FMV acceleration is separately opt-in and requires restart") !=
                   std::string::npos &&
               configSource.find("factor = 8.0") != std::string::npos &&
               trackedIniSource.find("factor = 8.0") != std::string::npos &&
               trackedIniSource.find("max_speed = 8.0") != std::string::npos,
           "Speed Hack defaults must document the fixed cycle, legacy keys, and 8x safety cap");

    Expect(FfxHooks::F8FlagCount() == 45, "catalog must expose exactly 45 rows");
    Expect(FfxHooks::F8FlagCount() == sizeof(expected) / sizeof(expected[0]),
           "catalog row count must match the hand-derived fixture");
    Expect(FfxHooks::F8TabCount() == 7, "catalog must expose exactly seven tabs");

    size_t tabCounts[7] = {};
    size_t activationCounts[4] = {};
    size_t applyCounts[3] = {};
    std::unordered_map<std::string, int> keyOccurrences;
    for (size_t i = 0; i < FfxHooks::F8FlagCount() && i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const FfxHooks::F8FlagSpec& actual = FfxHooks::F8FlagAt(i);
        const ExpectedCatalogRow& want = expected[i];
        ExpectCatalog(SameNullable(actual.tab, want.tab), i, "tab");
        ExpectCatalog(SameNullable(actual.label, want.label), i, "label");
        ExpectCatalog(SameNullable(actual.gate.canonicalKey, want.canonicalKey), i, "canonical key");
        ExpectCatalog(SameNullable(actual.gate.authorityKey, want.authorityKey), i, "authority key");
        ExpectCatalog(SameNullable(actual.gate.legacyKey, want.legacyKey), i, "legacy INI key");
        ExpectCatalog(SameNullable(actual.gate.envName, want.envName), i, "environment name");
        ExpectCatalog(SameNullable(actual.gate.flagName, want.flagName), i, "legacy flag name");
        ExpectCatalog(SameNullable(actual.gate.disableEnvName, want.disableEnvName), i, "disable environment name");
        ExpectCatalog(SameNullable(actual.gate.offFlagName, want.offFlagName), i, "off flag name");
        ExpectCatalog(SameNullable(actual.gate.globalOffFlagName, want.globalOffFlagName), i, "global off flag name");
        ExpectCatalog(actual.gate.defaultValue == want.defaultValue, i, "default value");
        ExpectCatalog(actual.activation == want.activation, i, "activation");
        ExpectCatalog(actual.applyMode == want.applyMode, i, "apply mode");
        ExpectCatalog(FfxHooks::FindF8Flag(want.canonicalKey) == &actual, i, "canonical lookup identity");

        ++keyOccurrences[actual.gate.canonicalKey ? actual.gate.canonicalKey : ""];
        ++activationCounts[static_cast<size_t>(actual.activation)];
        ++applyCounts[static_cast<size_t>(actual.applyMode)];
        for (size_t tab = 0; tab < 7; ++tab) {
            if (strcmp(actual.tab, expectedTabs[tab]) == 0) ++tabCounts[tab];
        }

        const char* prefix = FfxHooks::F8ActivationName(actual.activation);
        const size_t prefixLength = strlen(prefix);
        ExpectCatalog(actual.help && strncmp(actual.help, prefix, prefixLength) == 0 &&
                          strncmp(actual.help + prefixLength, " -", 2) == 0,
                      i, "activation-prefixed help");
    }

    for (const auto& occurrence : keyOccurrences) {
        Expect(occurrence.first.size() > 0 && occurrence.second == 1,
               "every canonical key must be non-empty and unique");
    }
    for (size_t tab = 0; tab < 7; ++tab) {
        Expect(strcmp(FfxHooks::F8TabName(tab), expectedTabs[tab]) == 0,
               "tab names must use the hand-derived stable order");
        Expect(tabCounts[tab] == expectedTabCounts[tab],
               "tab row counts must be 10/5/8/6/4/5/7");
    }
    Expect(activationCounts[static_cast<size_t>(F8Activation::Live)] == 24,
           "catalog must contain 24 LIVE rows");
    Expect(activationCounts[static_cast<size_t>(F8Activation::RestartRequired)] == 17,
           "catalog must contain 17 RESTART REQUIRED rows");
    Expect(activationCounts[static_cast<size_t>(F8Activation::NotWired)] == 0,
           "catalog must contain no unresolved NOT WIRED rows");
    Expect(activationCounts[static_cast<size_t>(F8Activation::ReadOnly)] == 4,
           "external module compatibility remains read-only information");
    Expect(applyCounts[static_cast<size_t>(F8ApplyMode::ConfigPolled)] == 4,
           "Speed, Compose, progression bypass, and Dialog are ConfigPolled");
    Expect(applyCounts[static_cast<size_t>(F8ApplyMode::RuntimeAcknowledged)] == 20,
           "existing gameplay and new native ports must be RuntimeAcknowledged");
    Expect(FfxHooks::FindF8Flag(nullptr) == nullptr &&
               FfxHooks::FindF8Flag("unknown.flag") == nullptr,
           "catalog lookup must reject null and unknown keys");

    const FfxHooks::F8FlagSpec* seymour = FfxHooks::FindF8Flag("boosters.playable_seymour");
    const FfxHooks::F8FlagSpec* compose = FfxHooks::FindF8Flag("arena_plus.compose_f7");
    const FfxHooks::F8FlagSpec* victory = FfxHooks::FindF8Flag("arena_plus.victory_hook");
    const FfxHooks::F8FlagSpec* fieldMax = FfxHooks::FindF8Flag("field_scout.max");
    const FfxHooks::F8FlagSpec* fieldUltra = FfxHooks::FindF8Flag("field_scout.ultra");
    Expect(seymour && seymour->activation == F8Activation::Live &&
               seymour->applyMode == F8ApplyMode::RuntimeAcknowledged &&
               strstr(seymour->help, "battle roster") != nullptr &&
               strstr(seymour->help, "Sphere Grid") != nullptr,
           "Seymour help must preserve the battle-only and no-Sphere-Grid boundary");
    Expect(compose && compose->activation == F8Activation::Live &&
               strstr(compose->help, "F7") != nullptr &&
               strstr(compose->help, "Arena+") != nullptr,
           "Compose help must name its player-visible F7 and Arena+ action");
    Expect(victory && victory->activation == F8Activation::RestartRequired &&
               strstr(victory->help, "rewards") != nullptr &&
               strstr(victory->help, "unchanged") != nullptr,
           "Victory Hook help must state scaffold/log-only truth");
    Expect(fieldUltra && strstr(fieldUltra->help, "Heavy") != nullptr,
           "FieldScout Ultra help must name its Heavy prerequisite");
    Expect(fieldMax && strstr(fieldMax->help, "Heavy") != nullptr &&
               strstr(fieldMax->help, "Ultra") != nullptr,
           "FieldScout Max help must name Heavy and Ultra prerequisites");

    for(const char* key : {"plugins.dinput8", "plugins.dxgi", "plugins.unx", "plugins.ffx_probe"}) {
        const auto* flag=FfxHooks::FindF8Flag(key);
        Expect(flag && flag->activation==F8Activation::ReadOnly && strncmp(flag->help,"READ ONLY - ",12)==0,
               "external modules must be information rows, never fake loader switches");
    }
    for(const char* key : {"input.block_windows_key", "input.fix_background_input", "input.filter_ime"}) {
        const auto* flag=FfxHooks::FindF8Flag(key);
        Expect(flag && flag->activation==F8Activation::Live && flag->applyMode==F8ApplyMode::RuntimeAcknowledged,
               "new input controls require real consumer acknowledgment");
    }
}

void TestEditableCatalogKeysExistInBothDefaults() {
    FakeState builtInState;
    Configure(builtInState, nullptr);
    for (size_t i = 0; i < FfxHooks::F8FlagCount(); ++i) {
        const FfxHooks::F8FlagSpec& flag = FfxHooks::F8FlagAt(i);
        if (flag.activation == F8Activation::NotWired) continue;
        bool value = false;
        Expect(TryGetBoolExact(flag.gate.canonicalKey, &value),
               "every editable row must be present in the built-in default template");
    }

    std::string trackedText;
    const std::string trackedPath = TrackedIniPath();
    Expect(!trackedPath.empty() && ReadWholeFile(trackedPath, trackedText),
           "tracked ffx-hooks.ini must be readable from the RT0 source tree");
    FakeState trackedState;
    Configure(trackedState, trackedText.c_str());
    for (size_t i = 0; i < FfxHooks::F8FlagCount(); ++i) {
        const FfxHooks::F8FlagSpec& flag = FfxHooks::F8FlagAt(i);
        if (flag.activation == F8Activation::NotWired) continue;
        bool value = false;
        Expect(TryGetBoolExact(flag.gate.canonicalKey, &value),
               "every editable row must be present in tracked ffx-hooks.ini");
    }
}

void TestCatalogEditGuardsAndReadback() {
    {
        FakeState state;
        Configure(state, "[boosters]\nplayable_seymour = 0\n");
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("boosters.playable_seymour");
        Expect(FfxHooks::PublishF8RuntimeStatus(
                   flag->gate.canonicalKey, F8RuntimeAvailability::Available, true, false),
               "Seymour test producer must publish available OFF readback before an edit");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
        Expect(result.code == F8EditCode::Saved && result.requestedValue &&
                   result.effective.value && state.persistCalls == 1,
               "available Seymour LIVE edits must persist and await runtime acknowledgement");
    }
    {
        FakeState state;
        Configure(state, "[boosters]\npermanent_sensor = 0\n");
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("boosters.permanent_sensor");
        Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
                   F8RuntimeAvailability::UnsupportedBuild, false, false),
               "test must make the LIVE row explicitly unavailable");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
        Expect(result.code == F8EditCode::RejectedUnavailable &&
                   result.runtime.availability == F8RuntimeAvailability::UnsupportedBuild &&
                   state.persistCalls == 0,
               "unavailable LIVE edits must reject without calling persistence");
    }
    {
        FakeState state;
        Configure(state,
                  "[arena_plus]\ncompose_f7 = 0\n"
                  "[labs]\narena_plus_compose_f7 = 0\n");
        state.environment["FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7"] = true;
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("arena_plus.compose_f7");
        Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
                   F8RuntimeAvailability::Available, true, false),
               "test must make Compose available before a live edit");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, false);
        Expect(result.code == F8EditCode::Saved && !result.requestedValue &&
                   result.effective.value && result.effective.source == BoolSource::Environment,
               "saved edits must re-resolve requested false against environment true");
        Expect(result.runtime.availability == F8RuntimeAvailability::Available &&
                   result.runtime.hasAppliedValue && result.runtime.appliedValue,
               "ConfigPolled edits must report the re-resolved effective value as applied");
        Expect(state.persistCalls == 1,
               "successful ConfigPolled edit must perform exactly one persistence transaction");
    }
    {
        FakeState state;
        Configure(state, "[boosters]\npermanent_sensor = 0\n");
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("boosters.permanent_sensor");
        Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
                   F8RuntimeAvailability::Available, true, false),
               "test must make RuntimeAcknowledged row available before an edit");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
        Expect(result.code == F8EditCode::Saved && result.effective.value,
               "available RuntimeAcknowledged edit must persist successfully");
        Expect(result.runtime.availability == F8RuntimeAvailability::Pending &&
                   !result.runtime.hasAppliedValue,
               "RuntimeAcknowledged edit must wait for consumer readback in Pending");
    }
    {
        FakeState state;
        Configure(state, "[field_scout]\nmaster = 0\n[labs]\nfield_scout = 0\n");
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("field_scout.master");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
        Expect(result.code == F8EditCode::Saved && result.effective.value &&
                   result.runtime.availability == F8RuntimeAvailability::NotApplicable,
               "RESTART REQUIRED edits must save without claiming runtime application");
    }
    {
        FakeState state;
        Configure(state, "[input]\ndialog_skip = 0\n");
        state.persistSucceeds = false;
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("input.dialog_skip");
        Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
                   F8RuntimeAvailability::Available, true, false),
               "test must make Dialog Skip available before persistence failure");
        const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
        Expect(result.code == F8EditCode::PersistFailed && !result.effective.value &&
                   result.runtime.availability == F8RuntimeAvailability::Available &&
                   result.runtime.hasAppliedValue && !result.runtime.appliedValue,
               "failed persistence must preserve effective and runtime state");
    }
}

void TestF8BulkTabTransactions() {
    const char* const scoutOff =
        "[field_scout]\nmaster = 0\nheavy = 0\nmax = 0\nultra = 0\n"
        "[f8_authority]\nfield_scout_master = 1\nfield_scout_heavy = 1\n"
        "field_scout_max = 1\nfield_scout_ultra = 1\n";
    {
        FakeState state;
        Configure(state, scoutOff);
        FfxHooks::F8BulkEditResult result = FfxHooks::SetF8TabValues("Dev", true);
        Expect(result.requestedValue && result.eligible == 5 && result.changed == 5 &&
                   result.already == 0 && result.unavailable == 0 &&
                   result.externalOverride == 0 && result.invalidParameter == 0 &&
                   result.effectiveMismatch == 0 && !result.persistFailed &&
                   state.persistCalls == 1 && result.rowCount == 5,
               "Enable Tab must persist all five editable Dev rows in one transaction");
        for (const char* key : {"field_scout.master", "field_scout.heavy",
                                "field_scout.max", "field_scout.ultra", "development.fastload_autosave"}) {
            const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag(key);
            Expect(flag && FfxHooks::ResolveF8Flag(*flag).value,
                   "the successful Scout bulk-enable must publish every accepted row");
        }
        result = FfxHooks::SetF8TabValues("Dev", true);
        Expect(result.eligible == 5 && result.changed == 0 && result.already == 5 &&
                   result.unavailable == 0 && result.externalOverride == 0 &&
                   result.invalidParameter == 0 && result.effectiveMismatch == 0 &&
                   state.persistCalls == 1,
               "repeating Enable Tab must detect already-effective rows without another write");
        result = FfxHooks::SetF8TabValues("Dev", false);
        Expect(!result.requestedValue && result.changed == 5 && result.already == 0 &&
                   result.unavailable == 0 && result.externalOverride == 0 &&
                   result.invalidParameter == 0 && result.effectiveMismatch == 0 &&
                   !result.persistFailed && state.persistCalls == 2,
               "Disable Tab must turn all Scout rows off in one additional transaction");
    }
    {
        FakeState state;
        Configure(state, scoutOff);
        state.persistSucceeds = false;
        const FfxHooks::F8BulkEditResult result =
            FfxHooks::SetF8TabValues("Dev", true);
        Expect(result.eligible == 5 && result.changed == 0 && result.persistFailed &&
                   state.persistCalls == 1 && result.rowCount == 5,
               "failed bulk persistence must report every pending row as persist-failed and publish none");
        bool everyQueuedRowFailed = true;
        for (size_t row = 0; row < result.rowCount; ++row) {
            everyQueuedRowFailed = everyQueuedRowFailed &&
                result.rows[row].code == FfxHooks::F8BulkRowCode::PersistFailed;
        }
        Expect(everyQueuedRowFailed,
               "every queued row must carry PersistFailed when the atomic write fails");
        for (const char* key : {"field_scout.master", "field_scout.heavy",
                                "field_scout.max", "field_scout.ultra", "development.fastload_autosave"}) {
            const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag(key);
            Expect(flag && !FfxHooks::ResolveF8Flag(*flag).value,
                   "failed bulk persistence must preserve the complete previous tab snapshot");
        }
    }
    {
        FakeState state;
        Configure(state,
            "[arena_plus]\nmaster = 0\ncompose_f7 = 0\nvictory_hook = 0\n"
            "resolver_log = 0\nmusic = 0\n"
            "[f8_authority]\narena_plus_master = 1\narena_plus_compose_f7 = 1\n"
            "arena_plus_victory_hook = 1\narena_plus_resolver_log = 1\n"
            "arena_plus_music = 1\n");
        const FfxHooks::F8FlagSpec* compose =
            FfxHooks::FindF8Flag("arena_plus.compose_f7");
        Expect(compose && FfxHooks::PublishF8RuntimeStatus(
                   compose->gate.canonicalKey,
                   F8RuntimeAvailability::ProducerUnavailable, false, false),
               "mixed bulk setup must publish the quarantined Compose row unavailable");
        const FfxHooks::F8BulkEditResult result =
            FfxHooks::SetF8TabValues("Arena+", true);
        Expect(result.eligible == 6 && result.changed == 5 && result.already == 0 &&
                   result.unavailable == 1 && result.externalOverride == 0 &&
                   result.invalidParameter == 0 && result.effectiveMismatch == 0 &&
                   !result.persistFailed && state.persistCalls == 1,
               "Enable Tab must atomically save accepted Arena+ rows while reporting Compose unavailable");
        const FfxHooks::F8BulkRowResult* composeRow = nullptr;
        for (size_t row = 0; row < result.rowCount; ++row) {
            if (result.rows[row].flag == compose) composeRow = &result.rows[row];
        }
        Expect(composeRow && composeRow->code == FfxHooks::F8BulkRowCode::Unavailable,
               "the quarantined Compose row must be classified Unavailable, not blocked");
        Expect(compose && !FfxHooks::ResolveF8Flag(*compose).value,
               "an unavailable bulk row must remain disabled while accepted siblings publish");
    }
    {
        FakeState state;
        Configure(state, "[plugins]\ndinput8 = 0\ndxgi = 0\nunx = 0\nffx_probe = 0\n");
        const FfxHooks::F8BulkEditResult result =
            FfxHooks::SetF8TabValues("System", true);
        Expect(result.eligible == 6 && result.changed == 0 && result.rowCount == 6 && result.unavailable == 6 &&
                   state.persistCalls == 0,
               "bulk actions exclude read-only module rows and do not persist unavailable native controls");
    }
}

void TestF8BulkExternalOverrideTruth() {
    // Exact Arena+ reproduction from the RT2 screenshot: master/victory/resolver already
    // ON, Compose quarantined, Music held OFF by arena_plus_music.flag.off — the summary
    // must read 1 changed, 3 already, 1 unavailable, 1 external OFF without an error tone.
    {
        FakeState state;
        Configure(state,
            "[arena_plus]\nmaster = 1\ncompose_f7 = 0\nvictory_hook = 1\n"
            "resolver_log = 1\nmusic = 0\n"
            "[f8_authority]\narena_plus_master = 1\narena_plus_compose_f7 = 1\n"
            "arena_plus_victory_hook = 1\narena_plus_resolver_log = 1\n"
            "arena_plus_music = 1\n");
        state.flags["arena_plus_music.flag.off"] = BoolSource::LegacyFlagModulesConfig;
        const FfxHooks::F8FlagSpec* compose =
            FfxHooks::FindF8Flag("arena_plus.compose_f7");
        const FfxHooks::F8FlagSpec* music =
            FfxHooks::FindF8Flag("arena_plus.music");
        Expect(compose && music && FfxHooks::PublishF8RuntimeStatus(
                   compose->gate.canonicalKey,
                   F8RuntimeAvailability::ProducerUnavailable, false, false),
               "Arena+ override fixture must publish Compose unavailable and find Music");

        const FfxHooks::F8BulkEditResult result =
            FfxHooks::SetF8TabValues("Arena+", true);
        Expect(result.eligible == 6 && result.changed == 1 && result.already == 3 &&
                   result.unavailable == 1 && result.externalOverride == 1 &&
                   result.invalidParameter == 0 && result.effectiveMismatch == 0 &&
                   !result.persistFailed && state.persistCalls == 1,
               "with the bypass row, the outcome must classify as 1 changed, 3 already, "
               "1 unavailable, 1 external OFF — not a generic block or save failure");
        const FfxHooks::F8BulkRowResult* composeRow = nullptr;
        const FfxHooks::F8BulkRowResult* musicRow = nullptr;
        for (size_t row = 0; row < result.rowCount; ++row) {
            if (result.rows[row].flag == compose) composeRow = &result.rows[row];
            if (result.rows[row].flag == music) musicRow = &result.rows[row];
        }
        Expect(composeRow && composeRow->code == FfxHooks::F8BulkRowCode::Unavailable &&
               musicRow && musicRow->code == FfxHooks::F8BulkRowCode::ExternalOverride &&
               musicRow->source == BoolSource::LegacyOffFlag,
               "Compose must be Unavailable while Music carries the off-flag source");
        Expect(state.persistedText.find("music = 1") != std::string::npos,
               "the canonical Music intent must persist even though the marker dominates");
        Expect(!FfxHooks::ResolveF8Flag(*music).value &&
               FfxHooks::ResolveF8Flag(*music).source == BoolSource::LegacyOffFlag,
               "the .off marker must keep dominating effective state; bulk may never "
               "delete or rename it");
        Expect(FfxHooks::F8GateSourceDetail(*music, BoolSource::LegacyOffFlag) &&
               strstr(FfxHooks::F8GateSourceDetail(*music, BoolSource::LegacyOffFlag),
                      "arena_plus_music.flag.off") != nullptr,
               "the override detail must name arena_plus_music.flag.off for the player");
    }
    // An effective mismatch after a successful write is an error, distinct from a known
    // external override: environment forcing ON while we persist OFF.
    {
        FakeState state;
        Configure(state,
            "[arena_plus]\nmaster = 0\ncompose_f7 = 0\nvictory_hook = 0\n"
            "resolver_log = 0\nmusic = 0\n"
            "[f8_authority]\narena_plus_master = 1\narena_plus_compose_f7 = 1\n"
            "arena_plus_victory_hook = 1\narena_plus_resolver_log = 1\n"
            "arena_plus_music = 1\n");
        state.environment["FFXHOOKS_ENABLE_ARENA_PLUS_VICTORY_HOOK"] = true;
        const FfxHooks::F8FlagSpec* victory =
            FfxHooks::FindF8Flag("arena_plus.victory_hook");
        Expect(victory != nullptr, "victory_hook catalog row must exist");
        const FfxHooks::F8BulkEditResult result =
            FfxHooks::SetF8TabValues("Arena+", false);
        const FfxHooks::F8BulkRowResult* victoryRow = nullptr;
        for (size_t row = 0; row < result.rowCount; ++row) {
            if (result.rows[row].flag == victory) victoryRow = &result.rows[row];
        }
        Expect(victoryRow && victoryRow->code == FfxHooks::F8BulkRowCode::ExternalOverride &&
               victoryRow->source == BoolSource::Environment &&
               !result.persistFailed && result.effectiveMismatch == 0,
               "a request defeated by environment must classify ExternalOverride by its "
               "real source, never as a persistence error or silent block");
    }
}

void TestUnavailableLiveRowsAllowFailSafeDisarm() {
    struct Case {
        const char* canonicalKey;
        const char* iniText;
        F8RuntimeAvailability availability;
        bool hasAppliedValue;
        bool appliedValue;
        F8ApplyMode applyMode;
        bool environmentOverride;
        bool expectedEffective;
        BoolSource expectedSource;
    };
    const Case cases[] = {
        {
            "arena_plus.compose_f7",
            "[arena_plus]\ncompose_f7 = 1\n"
            "[f8_authority]\narena_plus_compose_f7 = 1\n"
            "[labs]\narena_plus_compose_f7 = 0\n",
            F8RuntimeAvailability::Conflict,
            true,
            true,
            F8ApplyMode::ConfigPolled,
            true,
            true,
            BoolSource::Environment,
        },
        {
            "boosters.permanent_sensor",
            "[boosters]\npermanent_sensor = 1\n",
            F8RuntimeAvailability::Conflict,
            true,
            true,
            F8ApplyMode::RuntimeAcknowledged,
            false,
            false,
            BoolSource::UnmarkedCanonicalIni,
        },
        {
            "input.dialog_skip",
            "[input]\ndialog_skip = 1\n",
            F8RuntimeAvailability::RestorePending,
            true,
            false,
            F8ApplyMode::ConfigPolled,
            false,
            false,
            BoolSource::UnmarkedCanonicalIni,
        },
        {
            "boosters.entire_party_earns_ap",
            "[boosters]\nentire_party_earns_ap = 1\n",
            F8RuntimeAvailability::RestorePending,
            true,
            false,
            F8ApplyMode::RuntimeAcknowledged,
            false,
            false,
            BoolSource::UnmarkedCanonicalIni,
        },
        {
            "boosters.speed_hack",
            "[boosters]\nspeed_hack = 1\n",
            F8RuntimeAvailability::ProducerUnavailable,
            false,
            true,
            F8ApplyMode::ConfigPolled,
            false,
            false,
            BoolSource::UnmarkedCanonicalIni,
        },
        {
            "cheats.invincible_party",
            "[cheats]\ninvincible_party = 1\n",
            F8RuntimeAvailability::ProducerUnavailable,
            false,
            true,
            F8ApplyMode::RuntimeAcknowledged,
            false,
            false,
            BoolSource::UnmarkedCanonicalIni,
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const Case& item = cases[i];
        FakeState state;
        Configure(state, item.iniText);
        if (item.environmentOverride) {
            state.environment["FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7"] = true;
        }
        const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag(item.canonicalKey);
        ExpectCatalog(flag && flag->activation == F8Activation::Live &&
                          flag->applyMode == item.applyMode,
                      i, "unavailable disarm fixture metadata");
        ExpectCatalog(FfxHooks::PublishF8RuntimeStatus(
                          item.canonicalKey, item.availability,
                          item.hasAppliedValue, item.appliedValue),
                      i, "unavailable disarm setup publication");

        const FfxHooks::F8EditResult enable = FfxHooks::SetF8FlagValue(*flag, true);
        ExpectCatalog(enable.code == F8EditCode::RejectedUnavailable &&
                          enable.requestedValue && state.persistCalls == 0,
                      i, "unavailable enable rejection without persistence");
        ExpectCatalog(enable.runtime.availability == item.availability &&
                          enable.runtime.hasAppliedValue == item.hasAppliedValue &&
                          enable.runtime.appliedValue == item.appliedValue,
                      i, "unavailable enable exact runtime preservation");

        const FfxHooks::F8EditResult disable = FfxHooks::SetF8FlagValue(*flag, false);
        ExpectCatalog(disable.code == F8EditCode::Saved && !disable.requestedValue &&
                          state.persistCalls == 1,
                      i, "unavailable fail-safe disable persistence");
        ExpectCatalog(disable.effective.value == item.expectedEffective &&
                          disable.effective.source == item.expectedSource,
                      i, "unavailable disable re-resolved effective truth");
        ExpectCatalog(disable.runtime.availability == item.availability &&
                          disable.runtime.hasAppliedValue == item.hasAppliedValue &&
                          disable.runtime.appliedValue == item.appliedValue,
                      i, "unavailable disable exact runtime preservation");

        bool canonicalValue = true;
        ExpectCatalog(TryGetBoolExact(item.canonicalKey, &canonicalValue) && !canonicalValue,
                      i, "unavailable disable authoritative canonical false");
        if (flag->gate.authorityKey) {
            bool authorityMarker = false;
            ExpectCatalog(TryGetBoolExact(flag->gate.authorityKey, &authorityMarker) &&
                              authorityMarker,
                          i, "unavailable disable authority marker");
        }

        const FfxHooks::F8RuntimeStatus beforeRecovery =
            FfxHooks::GetF8RuntimeStatus(*flag);
        ExpectCatalog(beforeRecovery.availability == item.availability &&
                          beforeRecovery.hasAppliedValue == item.hasAppliedValue &&
                          beforeRecovery.appliedValue == item.appliedValue,
                      i, "unavailable status retained until producer recovery");
        ExpectCatalog(FfxHooks::PublishF8RuntimeStatus(
                          item.canonicalKey, F8RuntimeAvailability::Available, true, false),
                      i, "producer recovery publication");
        const FfxHooks::F8RuntimeStatus afterRecovery =
            FfxHooks::GetF8RuntimeStatus(*flag);
        ExpectCatalog(afterRecovery.availability == F8RuntimeAvailability::Available &&
                          afterRecovery.hasAppliedValue && !afterRecovery.appliedValue,
                      i, "producer recovery becomes final runtime truth");
    }
}

void TestSameRowFailurePublicationLinearizesAfterConfigPolledEdit() {
    FakeState state;
    Configure(state,
              "[arena_plus]\ncompose_f7 = 0\n"
              "[labs]\narena_plus_compose_f7 = 0\n");
    state.blockFirstPersist = true;
    const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("arena_plus.compose_f7");
    Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
               F8RuntimeAvailability::Available, true, false),
           "Compose must be available before the interleaving edit");

    FfxHooks::F8EditResult editResult = {
        F8EditCode::PersistFailed,
        false,
        {false, BoolSource::DefaultValue},
        {F8RuntimeAvailability::NotApplicable, false, false},
    };
    std::thread editor([&]() { editResult = FfxHooks::SetF8FlagValue(*flag, true); });
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        Expect(WaitForCondition(state.condition, lock, [&]() { return state.firstPersistEntered; }),
               "Compose edit must be held inside the persistence provider");
    }

    PublicationProgress sameRow;
    PublicationProgress otherRow;
    std::thread failurePublisher([&]() {
        PublishAfterBarrier(sameRow, flag->gate.canonicalKey,
                            F8RuntimeAvailability::UnsupportedBuild, false, false);
    });
    std::thread otherPublisher([&]() {
        PublishAfterBarrier(otherRow, "input.dialog_skip",
                            F8RuntimeAvailability::Available, true, false);
    });
    ArmPublication(sameRow);
    ArmPublication(otherRow);

    {
        std::unique_lock<std::mutex> lock(otherRow.mutex);
        Expect(WaitForConditionMillis(otherRow.condition, lock, 1000,
                                     [&]() { return otherRow.returned; }),
               "an unrelated row must remain publishable while Compose persistence is held");
    }
    {
        std::unique_lock<std::mutex> lock(sameRow.mutex);
        const bool completedInsideTransaction = WaitForConditionMillis(
            sameRow.condition, lock, 1000, [&]() { return sameRow.returned; });
        Expect(!completedInsideTransaction,
               "same-row failure publication must wait until the Compose edit releases its row");
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.releaseFirstPersist = true;
        state.condition.notify_all();
    }
    editor.join();
    failurePublisher.join();
    otherPublisher.join();

    const FfxHooks::F8RuntimeStatus finalStatus = FfxHooks::GetF8RuntimeStatus(*flag);
    Expect(editResult.code == F8EditCode::Saved && state.persistCalls == 1,
           "Compose edit must complete one persistence transaction after release");
    Expect(sameRow.result && otherRow.result,
           "both same-row and unrelated runtime publications must succeed");
    Expect(finalStatus.availability == F8RuntimeAvailability::UnsupportedBuild &&
               !finalStatus.hasAppliedValue,
           "failure publication that began during the edit must linearize last and remain final");
}

void TestSameRowReadbackLinearizesAfterRuntimeAcknowledgedEdit() {
    FakeState state;
    Configure(state, "[boosters]\npermanent_sensor = 0\n");
    state.blockFirstPersist = true;
    const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("boosters.permanent_sensor");
    Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
               F8RuntimeAvailability::Available, true, false),
           "Permanent Sensor must be available before the interleaving edit");

    FfxHooks::F8EditResult editResult = {
        F8EditCode::PersistFailed,
        false,
        {false, BoolSource::DefaultValue},
        {F8RuntimeAvailability::NotApplicable, false, false},
    };
    std::thread editor([&]() { editResult = FfxHooks::SetF8FlagValue(*flag, true); });
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        Expect(WaitForCondition(state.condition, lock, [&]() { return state.firstPersistEntered; }),
               "RuntimeAcknowledged edit must be held inside the persistence provider");
    }

    PublicationProgress sameRow;
    PublicationProgress otherRow;
    std::thread readbackPublisher([&]() {
        PublishAfterBarrier(sameRow, flag->gate.canonicalKey,
                            F8RuntimeAvailability::Available, true, true);
    });
    std::thread otherPublisher([&]() {
        PublishAfterBarrier(otherRow, "input.dialog_skip",
                            F8RuntimeAvailability::Available, true, true);
    });
    ArmPublication(sameRow);
    ArmPublication(otherRow);

    {
        std::unique_lock<std::mutex> lock(otherRow.mutex);
        Expect(WaitForConditionMillis(otherRow.condition, lock, 1000,
                                     [&]() { return otherRow.returned; }),
               "an unrelated row must remain publishable while runtime persistence is held");
    }
    {
        std::unique_lock<std::mutex> lock(sameRow.mutex);
        const bool completedInsideTransaction = WaitForConditionMillis(
            sameRow.condition, lock, 1000, [&]() { return sameRow.returned; });
        Expect(!completedInsideTransaction,
               "same-row readback must wait until the RuntimeAcknowledged edit releases its row");
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.releaseFirstPersist = true;
        state.condition.notify_all();
    }
    editor.join();
    readbackPublisher.join();
    otherPublisher.join();

    const FfxHooks::F8RuntimeStatus finalStatus = FfxHooks::GetF8RuntimeStatus(*flag);
    Expect(editResult.code == F8EditCode::Saved &&
               editResult.runtime.availability == F8RuntimeAvailability::Pending &&
               state.persistCalls == 1,
           "RuntimeAcknowledged edit must produce Pending before consumer readback");
    Expect(sameRow.result && otherRow.result,
           "both readback and unrelated runtime publications must succeed");
    Expect(finalStatus.availability == F8RuntimeAvailability::Available &&
               finalStatus.hasAppliedValue && finalStatus.appliedValue,
           "fresh readback that began during the edit must linearize last and remain final");
}

void TestUnavailablePublicationBeforeEditPreventsPersistence() {
    FakeState state;
    Configure(state, "[boosters]\nspeed_hack = 0\n");
    const FfxHooks::F8FlagSpec* flag = FfxHooks::FindF8Flag("boosters.speed_hack");
    Expect(FfxHooks::PublishF8RuntimeStatus(flag->gate.canonicalKey,
               F8RuntimeAvailability::ProducerUnavailable, false, false),
           "ProducerUnavailable publication must own the row before the edit starts");

    const FfxHooks::F8EditResult result = FfxHooks::SetF8FlagValue(*flag, true);
    Expect(result.code == F8EditCode::RejectedUnavailable &&
               result.runtime.availability == F8RuntimeAvailability::ProducerUnavailable &&
               state.persistCalls == 0,
           "an unavailable status that linearizes first must prevent persistence");
}

void TestRuntimeStatusIsRowSpecificAndMetadataIsImmutable() {
    const FfxHooks::F8FlagSpec* target = FfxHooks::FindF8Flag("arena_plus.music");
    const FfxHooks::F8FlagSpec* neighbor = FfxHooks::FindF8Flag("arena_plus.master");
    const FfxHooks::F8RuntimeStatus neighborBefore = FfxHooks::GetF8RuntimeStatus(*neighbor);
    const char* tabBefore = target->tab;
    const char* labelBefore = target->label;
    const char* helpBefore = target->help;
    const char* canonicalBefore = target->gate.canonicalKey;
    const F8Activation activationBefore = target->activation;
    const F8ApplyMode applyModeBefore = target->applyMode;

    Expect(FfxHooks::PublishF8RuntimeStatus(target->gate.canonicalKey,
               F8RuntimeAvailability::Conflict, true, true),
           "known canonical key must accept a row-specific runtime publication");
    const FfxHooks::F8RuntimeStatus targetAfter = FfxHooks::GetF8RuntimeStatus(*target);
    const FfxHooks::F8RuntimeStatus neighborAfter = FfxHooks::GetF8RuntimeStatus(*neighbor);
    Expect(targetAfter.availability == F8RuntimeAvailability::Conflict &&
               targetAfter.hasAppliedValue && targetAfter.appliedValue,
           "published runtime status must be observable on its exact row");
    Expect(neighborAfter.availability == neighborBefore.availability &&
               neighborAfter.hasAppliedValue == neighborBefore.hasAppliedValue &&
               neighborAfter.appliedValue == neighborBefore.appliedValue,
           "runtime publication must not mutate a neighboring row");
    Expect(target->tab == tabBefore && target->label == labelBefore && target->help == helpBefore &&
               target->gate.canonicalKey == canonicalBefore && target->activation == activationBefore &&
               target->applyMode == applyModeBefore,
           "runtime publication must not mutate immutable catalog metadata");
    Expect(!FfxHooks::PublishF8RuntimeStatus("unknown.flag",
                F8RuntimeAvailability::Available, false, false),
           "unknown canonical key must reject runtime publication");
}

void TestCatalogNamesAreStable() {
    struct ActivationCase { F8Activation value; const char* expected; };
    const ActivationCase activations[] = {
        {F8Activation::Live, "LIVE"},
        {F8Activation::RestartRequired, "RESTART REQUIRED"},
        {F8Activation::NotWired, "NOT WIRED"},
    };
    for (const ActivationCase& item : activations) {
        Expect(strcmp(FfxHooks::F8ActivationName(item.value), item.expected) == 0,
               "F8ActivationName must expose a stable user-visible label");
    }

    struct AvailabilityCase { F8RuntimeAvailability value; const char* expected; };
    const AvailabilityCase availability[] = {
        {F8RuntimeAvailability::NotApplicable, "NOT APPLICABLE"},
        {F8RuntimeAvailability::Pending, "PENDING"},
        {F8RuntimeAvailability::Available, "AVAILABLE"},
        {F8RuntimeAvailability::UnsupportedBuild, "UNSUPPORTED BUILD"},
        {F8RuntimeAvailability::ProducerUnavailable, "PRODUCER UNAVAILABLE"},
        {F8RuntimeAvailability::SignatureMismatch, "SIGNATURE MISMATCH"},
        {F8RuntimeAvailability::RestorePending, "RESTORE PENDING"},
        {F8RuntimeAvailability::Conflict, "CONFLICT"},
    };
    for (const AvailabilityCase& item : availability) {
        Expect(strcmp(FfxHooks::F8AvailabilityName(item.value), item.expected) == 0,
               "F8AvailabilityName must expose a stable user-visible label");
    }
}

void TestRewardProductionAdapterSourceContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), source),
           "UnX booster source must be readable for reward adapter contracts");
    const std::string rewardTable = BoundedSourceSection(
        source,
        "std::array<RewardBinding, kRewardBindingCount> g_rewardBindings = {{",
        "}};");
    Expect(!rewardTable.empty() && CountSourceToken(rewardTable, "{\"") == 2 &&
               rewardTable.find("{\"cheats.ap_100x\", RewardKind::Ap") != std::string::npos &&
               rewardTable.find("{\"cheats.gil_100x\", RewardKind::Gil") != std::string::npos,
           "AP and Gil must leave the generic loop as two independent compound bindings");

    const SourceBlock start = SourceFunctionBody(
        source, "bool FfxHooks::StartUnXBoosterHook(uintptr_t moduleBase, BoosterLogFn log)");
    const SourceBlock validate = SourceFunctionBody(
        source, "bool ValidateAdapter(uintptr_t moduleBase, ProfileResult* failureOut)");
    Expect(start.Valid() && validate.Valid() &&
               start.body.find("ValidateAdapter") != std::string::npos &&
               validate.body.find("PrepareRewardHookState") != std::string::npos &&
               start.body.find("InstallRewardHook") == std::string::npos &&
               start.body.find("VirtualAlloc") == std::string::npos &&
               start.body.find("VirtualProtect") == std::string::npos,
           "Start may prepare reward state but must perform no allocation or code write");

    const SourceBlock apply = SourceFunctionBody(source, "void ApplyRewardMultipliers()");
    Expect(apply.Valid() && SourceTokensInOrder(apply.body, {
               "g_adapter.battleActive", "binding.gateAddress", "gateBefore != 0",
               "if (battleActive == 0)", "InstallRewardHook", "ResolveF8Scalar",
               "UpdateRewardBinding", "PublishRewardEdge"}),
           "admitted Present work must install only out of battle, then publish scalar before gate status");
    const std::string publish = BoundedSourceSection(
        source, "void PublishRewardEdge(RewardBinding& binding,", "void ApplyRewardMultipliers()");
    Expect(!publish.empty() && publish.find("PublishF8RuntimeScalarStatus") != std::string::npos,
           "reward publication must carry exact scalar and boolean readback together");
    Expect(apply.Valid() && apply.body.find("for (RewardBinding& binding : g_rewardBindings)") !=
               std::string::npos,
           "AP and Gil install/application failures must remain isolated per compound binding");
    Expect(apply.Valid() && SourceTokensInOrder(apply.body, {
               "AcquireSRWLockExclusive(&g_rewardPatchLock)",
               "CanRunRewardHookTransaction", "RemoveRewardHook",
               "PrepareRewardHookState", "InstallRewardHook",
               "ReleaseSRWLockExclusive(&g_rewardPatchLock)"}),
           "one dedicated lock must serialize full recovery/install transactions across the shared page");

    const SourceBlock confirmBattle = SourceFunctionBody(
        source, "bool RewardConfirmBattleInactive(void*)");
    Expect(confirmBattle.Valid() && SourceTokensInOrder(confirmBattle.body, {
               "GuardedReadByte", "g_adapter.battleActive", "battleActive == 0"}),
           "the Windows adapter must re-sample battle state immediately for each site write");
    const SourceBlock beginCodeWrite = SourceFunctionBody(
        source,
        "bool RewardBeginCodeWrite(\n    void*, uintptr_t address, size_t length,\n"
        "    FfxHooks::F8Runtime::PatchProtectionToken* token)");
    Expect(beginCodeWrite.Valid() &&
               beginCodeWrite.body.find("IsExecutableProtection(prior)") != std::string::npos &&
               beginCodeWrite.body.find("IsWritableProtection(prior)") != std::string::npos &&
               CountSourceToken(beginCodeWrite.body, "VirtualProtect") >= 2,
           "site admission must reject writable/non-executable prior protection and restore it");
    const SourceBlock conditionalWrite = SourceFunctionBody(
        source,
        "FfxHooks::F8Runtime::MutationReport RewardWriteIfEqual(\n"
        "    void*, uintptr_t address, const uint8_t* expected,\n"
        "    const uint8_t* desired, size_t length)");
    Expect(conditionalWrite.Valid() &&
               conditionalWrite.body.find("AcquireSRWLockExclusive") == std::string::npos,
           "conditional site compare/write must rely on the outer full-transaction lock");
    Expect(publish.find("case RewardBindingResult::HookDeferred:") != std::string::npos,
           "normal hook deferral must publish Pending without hook-install-failed");

    const SourceBlock tick = SourceFunctionBody(
        source, "void FfxHooks::UnXBoosterFrameTick(uint32_t nowMs)");
    Expect(tick.Valid() && SourceTokensInOrder(tick.body, {
               "ApplyDebugBindings()", "ApplyRewardMultipliers()", "ApplyEntirePartyAp()"}),
           "Present tick must keep generic flags, reward bindings, and party AP as separate owners");

    const char* const adapterTokens[] = {
        "VirtualAlloc(nullptr, kRewardStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)",
        "PAGE_EXECUTE_READ", "FlushInstructionCache", "InterlockedExchange",
        "InterlockedCompareExchange", "RewardHookIo", "RewardScalarIo",
    };
    for (const char* token : adapterTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "Windows reward adapter must contain %s", token);
        ExpectSourceIncludes(source, token, message);
    }
    const SourceBlock stop = SourceFunctionBody(
        source, "void FfxHooks::RequestUnXBoosterStop()");
    Expect(stop.Valid() && stop.body.find("RemoveRewardHook") == std::string::npos &&
               stop.body.find("VirtualFree") == std::string::npos,
           "detach stop must not pretend to own synchronous reward restoration");
}

void TestMultiplierEditorAndUiContracts() {
    FfxHooks::F8Ui::ScalarEditor editor;
    Expect(!editor.Active(), "a fresh scalar editor must be inactive");
    Expect(editor.Begin(7, 25, 1, 100) && editor.Active() && editor.Row() == 7 &&
               editor.Draft() == 25,
           "scalar editor must capture its physical row and initial draft");
    Expect(editor.Adjust(-1) && editor.Draft() == 24,
           "scalar editor must apply a fine decrement");
    Expect(editor.Adjust(1) && editor.Draft() == 25,
           "scalar editor must apply a fine increment");
    Expect(editor.Adjust(-10) && editor.Draft() == 15,
           "scalar editor must apply a coarse decrement");
    Expect(editor.Adjust(10) && editor.Draft() == 25,
           "scalar editor must apply a coarse increment");
    Expect(editor.Adjust(-1000) && editor.Draft() == 1,
           "scalar editor must clamp at 1 without wrapping");
    Expect(editor.Adjust(1000) && editor.Draft() == 100,
           "scalar editor must clamp at 100 without wrapping");
    int saved = 0;
    Expect(editor.Confirm(&saved) && saved == 100 && !editor.Active(),
           "Confirm must return the draft and end the edit transaction");
    Expect(!editor.Confirm(&saved), "Confirm outside edit mode must be inert");
    Expect(editor.Begin(3, 40, 1, 100), "a second scalar edit must begin after Confirm");
    editor.Adjust(10);
    editor.Cancel();
    Expect(!editor.Active(), "Cancel must discard an active scalar draft");
    Expect(!editor.Begin(-1, 25, 1, 100) && !editor.Begin(1, 25, 100, 1),
           "invalid row or range must fail closed without entering edit mode");

    size_t cheatsFlags = 0;
    size_t cheatsScalars = 0;
    for (size_t i = 0; i < FfxHooks::F8FlagCount(); ++i) {
        const FfxHooks::F8FlagSpec& flag = FfxHooks::F8FlagAt(i);
        if (strcmp(flag.tab, "Cheats") != 0) continue;
        ++cheatsFlags;
        if (flag.scalar) ++cheatsScalars;
    }
    Expect(cheatsFlags == 8 && cheatsScalars == 2 &&
               cheatsFlags + cheatsScalars + 1 == 11,
           "Cheats must expose ten functional physical rows plus Back");
    Expect(FfxHooks::F8Ui::Layout::VisibleRows == 9,
           "the eleven-row Cheats surface must retain the nine-row viewport");

    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for scalar UI contracts");
    const SourceBlock build = SourceFunctionBody(source, "static void F7_BuildRows(int kind)");
    const SourceBlock input = SourceFunctionBody(source, "static int __cdecl F7Sub_InputCb(int obj)");
    const SourceBlock draw = SourceFunctionBody(source, "static int __cdecl F7Sub_DrawCb(int obj)");
    const SourceBlock close = SourceFunctionBody(source, "static void F8ReturnFlagsToGame()");
    Expect(build.Valid() && input.Valid() && draw.Valid() && close.Valid(),
           "scalar UI integration bodies must remain structurally readable");
    const SourceBlock flagsBuild = SourceBlockAfterToken(
        build.body, "else if (kind == F7_MENU_FLAGS)");
    Expect(flagsBuild.Valid() && SourceTokensInOrder(flagsBuild.body, {
               "F7RT_TOGGLE", "if (flag->scalar)", "F7RT_SCALAR", "Back"}),
           "each Rate row must be inserted immediately after its matching boolean row");

    const size_t editBranch = input.body.find("if (g_f8ScalarEditor.Active())");
    const size_t previousTab = input.body.find("g_f7Tab = (g_f7Tab - 1 + tabCount)");
    Expect(editBranch != std::string::npos && previousTab != std::string::npos &&
               editBranch < previousTab,
           "scalar edit input must intercept arrows before tab switching");
    Expect(input.body.find("if (!g_f8ScalarEditor.Active()) F8MouseTabHitTest(obj)") !=
               std::string::npos,
           "mouse tab hit testing must be inert during scalar editing");
    Expect(input.body.find("g_f8ScalarEditor.Confirm(&savedValue)") != std::string::npos &&
               CountSourceToken(input.body, "SetF8ScalarValue(") == 1,
           "Confirm must be the scalar editor's only persistence edge");
    Expect(input.body.find("g_f8ScalarEditor.Cancel()") != std::string::npos,
           "Back must cancel an active draft without closing FLAGS");
    Expect(close.body.find("g_f8ScalarEditor.Cancel()") != std::string::npos,
           "closing FLAGS must discard an active draft");

    const SourceBlock rows = SourceBlockAfterToken(draw.body, "for (int r = 0; r < page");
    Expect(rows.Valid() && CountSourceToken(rows.body, "DrawString(_lbl") == 1 &&
               rows.body.find("F8RefreshScalarLabel(row)") != std::string::npos,
           "dynamic Rate labels must reuse the row's single existing text draw");
    Expect(draw.body.find("F8Ui::FooterMode::ScalarEdit") != std::string::npos &&
               draw.body.find("F8Ui::FooterMode::Configure") != std::string::npos &&
               draw.body.find("F8Ui::FooterMode::Toggle") != std::string::npos &&
               draw.body.find("F8Ui::FooterText(") != std::string::npos,
           "FLAGS footer adapter must distinguish toggle, configure, and edit controls through the portable contract");
}

void TestF7DifficultyTruthfulUiContracts() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for F7 Difficulty UI contracts");
    const SourceBlock build = SourceFunctionBody(source, "static void F7_BuildRows(int kind)");
    const SourceBlock preset = SourceFunctionBody(source, "static void F7_DiffPresetFill(int preset)");
    const SourceBlock toggle = SourceFunctionBody(source, "static void F7DiffToggleBit(int valIdx, int bit)");
    const SourceBlock input = SourceFunctionBody(source, "static int __cdecl F7Sub_InputCb(int obj)");
    const SourceBlock draw = SourceFunctionBody(source, "static void F7Diff_Draw(int F)");
    const SourceBlock commit = SourceFunctionBody(source, "static void F7_CommitValsToConfig()");
    Expect(build.Valid() && preset.Valid() && toggle.Valid() && input.Valid() &&
               draw.Valid() && commit.Valid(),
           "F7 Difficulty UI functions must remain structurally readable");
    Expect(source.find("static bool g_f7DifficultyEnabled") != std::string::npos &&
               source.find("static int g_f7DiffPresetIdx") != std::string::npos,
           "Difficulty enabled state and preset identity must be independent variables");
    Expect(build.body.find("g_f7DifficultyEnabled = p.enabled") != std::string::npos &&
               build.body.find("g_f7Vals[0] = 0") == std::string::npos,
           "opening a custom preset must preserve configured enabled state");
    Expect(commit.body.find("p.enabled = g_f7DifficultyEnabled") != std::string::npos &&
               commit.body.find("p.enabled = g_f7Vals[0]") == std::string::npos,
           "saving a custom preset must not derive enabled state from its preset ID");
    Expect(preset.body.find("g_f7DifficultyEnabled = preset != 0") != std::string::npos &&
               toggle.body.find("g_f7DifficultyEnabled") == std::string::npos &&
               input.body.find("g_f7Vals[0] = 0") == std::string::npos,
           "preset selection may change enabled state, while manual edits must preserve it");
    Expect(draw.body.find("F7_DifficultyStatus()") != std::string::npos &&
               draw.body.find("F7_DifficultyResultName") != std::string::npos &&
               draw.body.find("CFG %s | INFRA %s | ADMISSION %s | LAST %s") !=
                   std::string::npos,
           "Difficulty must render config, infrastructure, admission, and last outcome "
           "as independent truths — the gate enum is not an installed-state word");
    Expect(draw.body.find("Apply Unavailable") != std::string::npos &&
               draw.body.find("Restore Stats") != std::string::npos &&
               draw.body.find("No Restore Needed") != std::string::npos,
           "unavailable apply stays explicit and the Off preset exposes its restore path");
    Expect(draw.body.find("\"STATS\"") != std::string::npos &&
               draw.body.find("CONFIG ONLY") == std::string::npos &&
               source.find("status/elements config-only") == std::string::npos,
           "wired Difficulty controls keep compact headers without the obsolete configuration-only caveat");
    Expect(draw.body.find("DrawInputHint(") != std::string::npos,
           "Difficulty footer must render native input glyphs, not text-only hints");
    Expect(input.body.find("status.installed") == std::string::npos &&
               input.body.find("!status.infrastructureInstalled") != std::string::npos &&
               input.body.find("!status.callbackAdmissionOpen") != std::string::npos &&
               input.body.find("F7_DifficultyGateName(status.infrastructureGate)") !=
                   std::string::npos &&
               input.body.find("F7_DifficultyResultName(status.last.code)") != std::string::npos &&
               input.body.find("Armed for next battle") != std::string::npos &&
               input.body.find("status.last.fieldsWritten") != std::string::npos &&
               input.body.find("status.last.fieldsRestored") != std::string::npos &&
               input.body.find("status.pointersRejected") != std::string::npos,
           "Apply Now must gate on infrastructure+admission, report the full structured "
           "result, and say Armed for next battle instead of Unavailable outside battle; "
           "it may never print the gate enum as an unavailable reason again");
}

void TestInputHintGlyphContracts() {
    std::string shell, source, maechen;
    Expect(ReadWholeFile(RuntimeSourcePath("..\\NativeMenuShell\\NativeMenuShell.h"), shell),
           "NativeMenuShell header must be readable for input-hint contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for input-hint contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), maechen),
           "MaechenHook source must be readable for input-hint contracts");

    Expect(shell.find("DrawPromptGlyph") != std::string::npos &&
               shell.find("DrawInputHint") != std::string::npos &&
               shell.find("PadInputActive") != std::string::npos,
           "prompt glyph emit, per-hint helper, and device switch must exist in the shell");
    Expect(shell.find("pad_icon.dds.phyre") != std::string::npos &&
               shell.find("keyboard_icon.dds.phyre") != std::string::npos &&
               shell.find("0xCCB218") != std::string::npos,
           "glyph routing must use the registered VFS texture paths (bare names fail "
           "ResolveTextureNameForUI) and the AsyncQ active flag");
    const SourceBlock glyph = SourceBlockAfterToken(
        shell, "static inline void DrawPromptGlyph(const char* sheet, int cell,");
    Expect(glyph.Valid() && glyph.body.find("int q[38]") != std::string::npos &&
               glyph.body.find("*(float*)&q[0] = x0") != std::string::npos &&
               glyph.body.find("*(float*)&q[8] = x1") != std::string::npos &&
               glyph.body.find("for (int i = 0; i < 4; ++i)") == std::string::npos,
           "Menu_RenderEnqueue takes TL/BR corners and expands four captured vertices; "
           "pre-expanded TL/BL makes every prompt quad zero-width");
    Expect(shell.find("0x63F090") != std::string::npos &&
               shell.find("0x8E5A20") != std::string::npos,
           "glyph quads must go through Menu_RenderEnqueue after scissor clipping");

    const SourceBlock sub = SourceFunctionBody(source, "static int __cdecl F7Sub_DrawCb(int obj)");
    const SourceBlock sin = SourceFunctionBody(source, "static int __cdecl SinCurse_DrawCb(int obj)");
    const SourceBlock arena = SourceFunctionBody(source, "static int __cdecl ArenaPlus_Draw(int obj)");
    const SourceBlock diff = SourceFunctionBody(source, "static void F7Diff_Draw(int F)");
    Expect(sub.Valid() && sin.Valid() && arena.Valid() && diff.Valid(),
           "F7 submenus, S.I.N., Arena+, and Difficulty draws must remain structurally readable");
    Expect(sub.body.find("DrawInputHint(") != std::string::npos &&
               sin.body.find("DrawInputHint(") != std::string::npos &&
               arena.body.find("DrawInputHint(") != std::string::npos &&
               diff.body.find("DrawInputHint(") != std::string::npos,
           "every footer must render native input glyphs instead of text-only control hints");
    Expect(sub.body.find("PC_PAD_FACE_D") != std::string::npos &&
               sub.body.find("PC_PAD_FACE_R") != std::string::npos,
           "confirm/cancel hints must map to the face-down and face-right cells");
    Expect(sin.body.find("S.I.N. - Curses of Sin") != std::string::npos,
           "S.I.N. title identifies the player-facing curse mode");
    Expect(maechen.find("DrawInputHint") != std::string::npos &&
               maechen.find("PC_KB_ENTER") != std::string::npos &&
               maechen.find("PC_KB_F9") != std::string::npos,
           "Maechen footer must render keyboard glyphs for ask, page, and close");
}

void TestBackspacePromptSemantics() {
    std::string shell, source, maechen;
    Expect(ReadWholeFile(RuntimeSourcePath("..\\NativeMenuShell\\NativeMenuShell.h"), shell),
           "NativeMenuShell header must be readable for Backspace contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), source),
           "dllmain source must be readable for Backspace contracts");
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), maechen),
           "MaechenHook source must be readable for Backspace contracts");

    // The game merges keyboard input into the pad stream; the F-key menus'
    // Back/Cancel action is the pad cancel edge, which on keyboard is the
    // Backspace key (user RT2, 2026-09-16). Row 4 cell 2 of the keyboard_icon
    // sheet is the Backspace glyph.
    Expect(shell.find("PC_KB_BACKSPACE = 0x42") != std::string::npos,
           "PromptCell must define the Backspace cell on row 4 of the keyboard sheet");
    Expect(CountSourceToken(source, "PC_KB_ESC, PC_SKIP, \"Back\"") == 0 &&
               CountSourceToken(shell, "PC_KB_ESC, PC_SKIP, \"Back\"") == 0,
           "no semantic Back hint may keep the Esc cell");
    Expect(CountSourceToken(source, "PC_KB_BACKSPACE, PC_SKIP, \"Back\"") == 7 &&
               CountSourceToken(shell, "PC_KB_BACKSPACE, PC_SKIP, \"Back\"") == 1,
           "every verified Back hint must render the Backspace cell "
           "(F7 generic/Force/AI/Difficulty, F8 flags, S.I.N., Arena+, shell main menu)");
    Expect(CountSourceToken(source, "PC_KB_ESC, PC_SKIP, \"Cancel\"") == 0 &&
               CountSourceToken(source, "PC_KB_BACKSPACE, PC_SKIP, \"Cancel\"") == 1,
           "the F8 scalar editor Cancel rides the same pad-cancel edge and must "
           "show Backspace, not Esc");
    Expect(CountSourceToken(maechen, "PC_KB_ESC") >= 1 &&
               maechen.find("\"Close\"") != std::string::npos,
           "F9/Maechen keeps its explicit Esc close hint unchanged");
    Expect(CountSourceToken(source, "PC_PAD_FACE_R, PC_SKIP, PC_KB_BACKSPACE") >= 8,
           "pad Back must remain PC_PAD_FACE_R so UnX controller skins keep working");
    Expect(source.find("VK_ESCAPE") != std::string::npos,
           "the emergency Escape close on the dormant held-lane stays untouched");
}

void TestStrictIntegerConfiguration() {
    FakeState state;
    Configure(state,
              "[cheats]\n"
              "one = 1\n"
              "hundred = 100\n"
              "zero = 0\n"
              "above = 101\n"
              "negative = -1\n"
              "trailing = 25x\n"
              "empty =\n"
              "overflow = 999999999999999999999999999999\n"
              "ap_multiplier = 25\n");

    const auto missing = ReadIntExact("cheats.missing", 1, 100);
    Expect(missing.state == IntReadState::Missing,
           "strict integer lookup must distinguish a missing key");
    Expect(ReadIntExact("cheats.one", 1, 100).state == IntReadState::Valid &&
               ReadIntExact("cheats.one", 1, 100).value == 1,
           "strict integer lookup must accept the lower boundary exactly");
    Expect(ReadIntExact("cheats.hundred", 1, 100).state == IntReadState::Valid &&
               ReadIntExact("cheats.hundred", 1, 100).value == 100,
           "strict integer lookup must accept the upper boundary exactly");

    const char* invalidKeys[] = {
        "cheats.zero", "cheats.above", "cheats.negative", "cheats.trailing",
        "cheats.empty", "cheats.overflow",
    };
    for (const char* key : invalidKeys) {
        Expect(ReadIntExact(key, 1, 100).state == IntReadState::Invalid,
               "strict integer lookup must reject malformed or out-of-range text");
    }

    state.persistSucceeds = false;
    Expect(!SetInt("cheats.ap_multiplier", 40),
           "SetInt must report an atomic persistence failure");
    const auto preserved = ReadIntExact("cheats.ap_multiplier", 1, 100);
    Expect(preserved.state == IntReadState::Valid && preserved.value == 25,
           "failed SetInt must preserve the previous published snapshot");

    state.persistSucceeds = true;
    Expect(SetInt("cheats.ap_multiplier", 40), "SetInt must persist a valid integer");
    const auto saved = ReadIntExact("cheats.ap_multiplier", 1, 100);
    Expect(saved.state == IntReadState::Valid && saved.value == 40,
           "successful SetInt must publish only the persisted generation");
}

void TestFastloadDevelopmentGate() {
    const auto* flag=FfxHooks::FindF8Flag("development.fastload_autosave");
    Expect(flag!=nullptr,"Fastload development row exists");
    if(!flag)return;
    FakeState state;Configure(state,"");
    auto resolved=FfxHooks::ResolveF8Flag(*flag);
    Expect(!resolved.value&&resolved.source==BoolSource::DefaultValue,"Fastload missing config defaults OFF");
    state.environment["FFXHOOKS_ENABLE_FAST_BOOT_SKIP"]=true;
    state.environment["FFXHOOKS_FAST_BOOT_SKIP_APPLY"]=true;
    state.flags["fast_boot_skip.flag"]=BoolSource::LegacyFlagRoot;
    Expect(!FfxHooks::ResolveF8Flag(*flag).value,"old BootSkip artifacts cannot arm Fastload");
    const auto edited=FfxHooks::SetF8FlagValue(*flag,true);
    Expect(edited.code==F8EditCode::Saved&&flag->activation==F8Activation::RestartRequired&&FfxHooks::ResolveF8Flag(*flag).value,
           "F8 edit persists for restart without live apply");
    Expect(state.persistedText.find("fastload_autosave = 1")!=std::string::npos,
           "Fastload edit persists canonical choice atomically");
    state.environment["FFXHOOKS_DISABLE_FASTLOAD_AUTOSAVE"]=true;
    resolved=FfxHooks::ResolveF8Flag(*flag);
    Expect(!resolved.value&&resolved.source==BoolSource::DisableEnvironment,"disable environment overrides persisted ON");
    state.environment.erase("FFXHOOKS_DISABLE_FASTLOAD_AUTOSAVE");
    state.flags["fastload_autosave.flag.off"]=BoolSource::LegacyFlagRoot;
    resolved=FfxHooks::ResolveF8Flag(*flag);
    Expect(!resolved.value&&resolved.source==BoolSource::LegacyOffFlag,"off marker overrides persisted ON");
    std::string source,config,ini;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"),source)&&ReadWholeFile(RuntimeSourcePath("shared/Config.cpp"),config)&&ReadWholeFile(TrackedIniPath(),ini),"Fastload UI and default sources readable");
    const auto worker=SourceFunctionBody(source,"static DWORD WINAPI HooksWorkerThread(LPVOID)");
    const auto install=SourceFunctionBody(source,"static void InstallHooks()");
    Expect(worker.Valid()&&SourceTokensInOrder(worker.body,{"Config::Load()","CaptureF8StartupGates()","Sleep("})&&install.body.find("CaptureF8StartupGates()")==std::string::npos,
           "immutable startup capture follows config and precedes delayed hooks exactly once");
    const auto capture=SourceFunctionBody(source,"static void CaptureF8StartupGates()");
    Expect(capture.body.find("InitOnceExecuteOnce")!=std::string::npos,"startup capture has an actual once-only publication");
    Expect(config.find("fastload_autosave = 0")!=std::string::npos&&ini.find("[development]\nfastload_autosave = 0")!=std::string::npos,"built-in and tracked Fastload defaults OFF");
    const auto rows=SourceFunctionBody(source,"static void F7_BuildRows(int kind)");
    size_t devRows=0;
    for(size_t i=0;i<FfxHooks::F8FlagCount();++i)if(std::strcmp(FfxHooks::F8FlagAt(i).tab,"Dev")==0)++devRows;
    Expect(devRows==5&&rows.body.find("if (editableCount > 1)")!=std::string::npos,"Dev combines Scout and Fastload with atomic bulk actions");
    const auto status=SourceFunctionBody(source,"static void F8BuildSelectedStatus(int sel, char* out, size_t outSize)");
    Expect(status.body.find("Fastload::GetRuntimeSnapshot()")!=std::string::npos&&status.body.find("Fastload::RuntimeDetail")!=std::string::npos,
           "Fastload row reports bounded actual runtime status independently of pending configuration");
}

void TestLabCatalogAndRestartControls() {
    struct LabRow {const char* key;const char* authority;const char* env;const char* flag;const char* reader;};
    const LabRow rows[]={
        {"labs.nova_super_damage","f8_authority.lab_nova_super_damage","FFXHOOKS_ENABLE_NOVA_SUPER_DAMAGE","nova_super_damage.flag","NovaSuperDamageFlagEnabled"},
        {"labs.kimahri_ronso_mana","f8_authority.lab_kimahri_ronso_mana","FFXHOOKS_ENABLE_RONSO_MANA","kimahri_ronso_mana.flag","RonsoManaFlagEnabled"},
        {"labs.grid_teach","f8_authority.lab_grid_teach","FFXHOOKS_GRID_TEACH","grid_teach.flag","GridTeachEnabled"},
        {"labs.kimahri_lancet_dual_grant","f8_authority.lab_kimahri_lancet_dual_grant","FFXHOOKS_KIMAHRI_LANCET_DUAL_GRANT","kimahri_lancet_dual_grant.flag","KimahriLancetDualGrantEnabled"},
        {"labs.item_stack_cap","f8_authority.lab_item_stack_cap","FFXHOOKS_ENABLE_ITEM_STACK_CAP","item_stack_cap_255.flag","ItemStackCapFlagEnabled"},
        {"labs.double_triple_drop","f8_authority.lab_double_triple_drop","FFXHOOKS_ENABLE_DOUBLE_TRIPLE_DROP","double_triple_drop.flag","DoubleTripleDropEnabled"},
    };
    Expect(FfxHooks::F8FlagCount()==45&&FfxHooks::F8TabCount()==7&&
               strcmp(FfxHooks::F8TabName(6),"Reforge")==0,"Lab has six rows after Dev without replacing Fastload");
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"),source),"Lab install source readable");
    const auto capture=SourceBlockAfterToken(source,"static F8StartupGateSnapshot g_f8StartupGates[]").body;
    for(const auto& row:rows) {
        const auto* flag=FfxHooks::FindF8Flag(row.key);
        Expect(flag!=nullptr,"each selected wired Lab feature has a catalog row");
        const std::string readerName=std::string("static bool ")+row.reader+"()";
        const auto reader=SourceFunctionBody(source,readerName.c_str());
        Expect(reader.Valid()&&reader.body.find("F8CatalogGateEnabled")!=std::string::npos&&
                   reader.body.find(row.key)!=std::string::npos&&reader.body.find("CheckEnabled")==std::string::npos,
               "Lab install readers consume the same authoritative startup resolver as F8");
        Expect(capture.find(row.key)!=std::string::npos,"Lab gates are captured once with other restart-required gates");
        if(!flag)continue;
        Expect(strcmp(flag->tab,"Reforge")==0&&flag->activation==F8Activation::RestartRequired&&
                   flag->applyMode==F8ApplyMode::None&&!flag->gate.defaultValue&&
                   SameNullable(flag->gate.authorityKey,row.authority)&&SameNullable(flag->gate.envName,row.env)&&
                   SameNullable(flag->gate.flagName,row.flag)&&!flag->gate.disableEnvName&&!flag->gate.offFlagName,
               "Lab metadata is default OFF, restart-required and retains exact legacy names");
        FakeState state;Configure(state,"[labs]\n");
        Expect(!FfxHooks::ResolveF8Flag(*flag).value,"untouched Lab row defaults OFF");
        state.flags[row.flag]=BoolSource::LegacyFlagModules;
        Expect(FfxHooks::ResolveF8Flag(*flag).value,"untouched legacy flag still enables its Lab hook");
        const auto off=FfxHooks::SetF8FlagValue(*flag,false);
        Expect(off.code==F8EditCode::Saved&&!off.effective.value&&
                   off.effective.source==BoolSource::AuthoritativeCanonicalIni&&state.flags.count(row.flag)==1,
               "F8 OFF beats a stale flag without deleting that user's flag file");
        const std::string persisted=state.persistedText;Configure(state,persisted.c_str());
        Expect(!FfxHooks::ResolveF8Flag(*flag).value,"authoritative OFF survives config reload with the stale flag present");
        state.environment[row.env]=true;
        Expect(FfxHooks::ResolveF8Flag(*flag).value&&FfxHooks::ResolveF8Flag(*flag).source==BoolSource::Environment,
               "an explicit environment override remains visible and authoritative");
        state.environment.clear();state.flags.clear();
        const std::string legacy=std::string("[labs]\n")+(row.key+5)+"=1\n";
        Configure(state,legacy.c_str());
        Expect(FfxHooks::ResolveF8Flag(*flag).value,"existing unmarked INI true remains an opt-in after migration");
        const std::string neutral=std::string("[labs]\n")+(row.key+5)+"=0\n";
        Configure(state,neutral.c_str());state.flags[row.flag]=BoolSource::LegacyFlagModules;
        Expect(FfxHooks::ResolveF8Flag(*flag).value,"unmarked INI false preserves the old additive flag opt-in");
    }
    const auto* cap=FfxHooks::FindF8Flag("labs.item_stack_cap");
    if(cap) {
        Expect(cap->scalar&&strcmp(cap->scalar->canonicalKey,"labs.item_stack_cap_value")==0&&
                   cap->scalar->defaultValue==255&&cap->scalar->minimum==1&&cap->scalar->maximum==255,
               "item cap scalar has count range 1..255 and default 255");
        FakeState state;Configure(state,"[labs]\nitem_stack_cap_value=255\n");
        Expect(FfxHooks::SetF8FlagValue(*cap,true).code==F8EditCode::Saved,"item-cap gate may arm for next boot");
        for(int value:{1,99,127,200,255}) {
            const auto edit=FfxHooks::SetF8ScalarValue(*cap,value);
            Expect(edit.code==F8ScalarEditCode::Saved&&edit.configured.value==value&&
                       edit.runtime.availability==F8RuntimeAvailability::NotApplicable,
                   "an enabled restart-required scalar remains editable without a live writer or false Pending status");
        }
        for(int value:{0,256})Expect(FfxHooks::SetF8ScalarValue(*cap,value).code==F8ScalarEditCode::RejectedInvalid,
            "item-cap editor rejects values outside its byte ceiling");
        Expect(FfxHooks::PublishF8RuntimeScalarStatus(cap->gate.canonicalKey,
                   F8RuntimeAvailability::Available,true,true,true,200),"item cap can publish its actual startup ceiling");
        const auto changed=FfxHooks::SetF8ScalarValue(*cap,255);
        Expect(changed.code==F8ScalarEditCode::Saved&&changed.configured.value==255&&
                   changed.runtime.hasAppliedScalar&&changed.runtime.appliedScalar==200,
               "next-boot cap edits preserve the distinct current-startup readback");
        FakeState bulk;Configure(bulk,"[labs]\n");
        for(const auto& row:rows)bulk.flags[row.flag]=BoolSource::LegacyFlagRoot;
        bulk.flags["equipment_workshop.flag"]=BoolSource::LegacyFlagRoot;
        const auto disabled=FfxHooks::SetF8TabValues("Reforge",false);
        Expect(disabled.eligible==7&&disabled.changed==7&&bulk.persistCalls==1,"Lab bulk OFF resolves seven stale flags in one atomic write");
        const auto enabled=FfxHooks::SetF8TabValues("Reforge",true);
        Expect(enabled.eligible==7&&enabled.changed==7&&bulk.persistCalls==2,"Lab bulk ON persists all seven independent rows once");
        for(const auto& row:rows)Expect(FfxHooks::ResolveF8Flag(*FfxHooks::FindF8Flag(row.key)).value,"every bulk-enabled Lab gate resolves ON");
    }
    Expect(!FfxHooks::FindF8Flag("labs.kimahri_ronso_mana_apply")&&
               source.find("RonsoManaApplyEnabled")==std::string::npos&&
               source.find("InstallRonsoManaHook")==std::string::npos,
           "obsolete Apply has no menu, startup consumer or retired hook installation");
    const auto early=SourceFunctionBody(source,"static void StartNovaPoolEarlyIfRequested()");
    Expect(early.Valid()&&early.body.find("enableRonsoMana = RonsoManaFlagEnabled()")!=std::string::npos&&
               early.body.find("!enableRonsoMana && !compatibility")!=std::string::npos&&
               early.body.find("base, enableNovaBypass, enableNovaLog, enableRonsoMana, LogLine")!=std::string::npos,
           "early startup admits Ronso independently from Nova before any Fastload read");
    const auto worker=SourceFunctionBody(source,"static DWORD WINAPI HooksWorkerThread(LPVOID)");
    Expect(worker.Valid()&&SourceTokensInOrder(worker.body,{
               "CaptureF8StartupGates()","StartNovaPoolEarlyIfRequested()","StartFastloadEarlyIfRequested()"}),
           "Ronso save interception remains before Fastload and delayed installers");
    const auto* ronso=FfxHooks::FindF8Flag("labs.kimahri_ronso_mana");
    const auto* nova=FfxHooks::FindF8Flag("labs.nova_super_damage");
    if(ronso&&nova) {
        FakeState legacy;Configure(legacy,"[labs]\nkimahri_ronso_mana_apply=1\nnova_super_damage=1\n");
        legacy.flags["kimahri_ronso_mana_apply.flag"]=BoolSource::LegacyFlagModules;
        legacy.environment["FFXHOOKS_RONSO_MANA_APPLY"]=true;
        Expect(!FfxHooks::ResolveF8Flag(*ronso).value&&FfxHooks::ResolveF8Flag(*nova).value,
               "legacy Apply and Nova do not opt in to Ronso Mana");
        Expect(FfxHooks::SetF8FlagValue(*ronso,true).code==F8EditCode::Saved&&FfxHooks::ResolveF8Flag(*ronso).value,
               "one Ronso Mana switch enables the complete new feature");
        Expect(FfxHooks::SetF8FlagValue(*ronso,false).code==F8EditCode::Saved&&!FfxHooks::ResolveF8Flag(*ronso).value&&
                   FfxHooks::ResolveF8Flag(*nova).value,
               "Ronso OFF cannot be re-enabled by old Apply settings or Nova ON");
    }
    Expect(source.find("else if (!GridTeachEnabled())")!=std::string::npos&&
               source.find("grid_teach.flag off")!=std::string::npos,"Lancet still refuses an absent GridTeach dependency");
    Expect(source.find("GetInt(\"labs.item_stack_cap_value\", 255)")!=std::string::npos&&
               source.find("EnvInt(\"FFXHOOKS_ITEM_STACK_CAP\"")!=std::string::npos,
           "native item-cap installation consumes the saved scalar with its existing environment override");
    const auto label=SourceFunctionBody(source,"static void F8RefreshScalarLabel(int row)");
    Expect(label.Valid()&&label.body.find("labs.item_stack_cap")!=std::string::npos&&label.body.find("Item Cap")!=std::string::npos,
           "item-cap scalar is rendered as a count, never as the Gil multiplier");
    const auto scalarStatus=SourceFunctionBody(source,"static void F8BuildSelectedStatus(int sel, char* out, size_t outSize)");
    Expect(scalarStatus.Valid()&&scalarStatus.body.find("const bool nextBoot")!=std::string::npos&&
               scalarStatus.body.find("STARTUP %d")!=std::string::npos,
           "restart scalar status distinguishes configured count from startup evidence instead of claiming LIVE");
}

void TestArenaMixProgressionFlag() {
    FakeState state;
    Configure(state, "[arena_plus]\nunlock_all=0\n");
    const auto* flag = FfxHooks::FindF8Flag("arena_plus.unlock_all");
    Expect(flag && !FfxHooks::ResolveF8Flag(*flag).value,
           "progression bypass must default OFF");
    if (!flag) return;
    auto edit = FfxHooks::SetF8FlagValue(*flag, true);
    Expect(edit.code == FfxHooks::F8EditCode::Saved && edit.effective.value &&
               edit.runtime.appliedValue,
           "F8 must persist and immediately apply explicit progression bypass");
    state.flags["arena_plus_unlock_all.flag"] = BoolSource::LegacyFlagModulesConfig;
    edit = FfxHooks::SetF8FlagValue(*flag, false);
    Expect(edit.code == FfxHooks::F8EditCode::Saved && !edit.effective.value,
           "F8 OFF must restore boss requirements despite a stale legacy opt-in");
    Expect(state.persistedText.find("unlock_all = 0") != std::string::npos,
           "the OFF progression choice survives restart");
}

void TestMultiplierCatalogAndTransactions() {
    const FfxHooks::F8FlagSpec* ap = FfxHooks::FindF8Flag("cheats.ap_100x");
    const FfxHooks::F8FlagSpec* gil = FfxHooks::FindF8Flag("cheats.gil_100x");
    Expect(ap && gil, "AP and Gil legacy boolean rows must remain in the catalog");
    Expect(FfxHooks::F8FlagCount() == 45,
           "scalar metadata must not add boolean catalog rows");
    Expect(ap && ap->scalar && strcmp(ap->scalar->canonicalKey, "cheats.ap_multiplier") == 0 &&
               ap->scalar->defaultValue == 100 && ap->scalar->minimum == 1 &&
               ap->scalar->maximum == 100,
           "AP must expose the exact independent scalar contract");
    Expect(gil && gil->scalar && strcmp(gil->scalar->canonicalKey, "cheats.gil_multiplier") == 0 &&
               gil->scalar->defaultValue == 100 && gil->scalar->minimum == 1 &&
               gil->scalar->maximum == 100,
           "Gil must expose the exact independent scalar contract");
    size_t scalarRows = 0;
    for (size_t i = 0; i < FfxHooks::F8FlagCount(); ++i) {
        if (FfxHooks::F8FlagAt(i).scalar) ++scalarRows;
    }
    Expect(scalarRows == 3, "only AP, Gil and Item Stack Cap expose scalar metadata");

    FakeState state;
    Configure(state,
              "[cheats]\n"
              "ap_100x = 0\n"
              "gil_100x = 0\n");
    auto apScalar = FfxHooks::ResolveF8Scalar(*ap);
    Expect(apScalar.state == F8ScalarState::Defaulted && apScalar.value == 100,
           "a missing AP scalar must resolve to the backward-compatible 100x default");

    Configure(state,
              "[cheats]\n"
              "ap_100x = 0\n"
              "ap_multiplier = junk\n"
              "gil_100x = 0\n"
              "gil_multiplier = 7\n");
    apScalar = FfxHooks::ResolveF8Scalar(*ap);
    const auto gilScalar = FfxHooks::ResolveF8Scalar(*gil);
    Expect(apScalar.state == F8ScalarState::Invalid,
           "present invalid AP scalar text must remain invalid rather than defaulting");
    Expect(gilScalar.state == F8ScalarState::Valid && gilScalar.value == 7,
           "valid Gil scalar configuration must resolve independently");

    FfxHooks::PublishF8RuntimeScalarStatus(
        "cheats.ap_100x", F8RuntimeAvailability::SignatureMismatch,
        false, false, false, 0);
    const auto rejectedEnable = FfxHooks::SetF8FlagValue(*ap, true);
    Expect(rejectedEnable.code == F8EditCode::RejectedInvalidParameter,
           "an invalid scalar must prevent its legacy boolean gate from arming");
    Expect(!FfxHooks::ResolveF8Flag(*ap).value,
           "rejected invalid scalar enable must preserve the boolean gate");

    const auto repaired = FfxHooks::SetF8ScalarValue(*ap, 25);
    Expect(repaired.code == F8ScalarEditCode::Saved &&
               repaired.configured.state == F8ScalarState::Valid &&
               repaired.configured.value == 25 &&
               repaired.runtime.availability == F8RuntimeAvailability::SignatureMismatch,
           "invalid scalar configuration must remain repairable through the catalog transaction");
    Expect(!FfxHooks::ResolveF8Flag(*ap).value,
           "saving a scalar must preserve its independent boolean gate");

    state.persistSucceeds = false;
    const auto failed = FfxHooks::SetF8ScalarValue(*gil, 9);
    Expect(failed.code == F8ScalarEditCode::PersistFailed,
           "scalar persistence failure must be reported exactly");
    Expect(FfxHooks::ResolveF8Scalar(*gil).value == 7,
           "failed scalar persistence must publish no speculative value");
    state.persistSucceeds = true;

    FfxHooks::PublishF8RuntimeScalarStatus(
        "cheats.gil_100x", F8RuntimeAvailability::Available, true, false, true, 7);
    Expect(FfxHooks::SetF8FlagValue(*gil, true).code == F8EditCode::Saved,
           "valid Gil scalar must allow its legacy boolean gate to arm");
    FfxHooks::PublishF8RuntimeScalarStatus(
        "cheats.gil_100x", F8RuntimeAvailability::SignatureMismatch,
        false, false, false, 0);
    const auto unavailableEdit = FfxHooks::SetF8ScalarValue(*gil, 8);
    Expect(unavailableEdit.code == F8ScalarEditCode::RejectedUnavailable,
           "an enabled unavailable row must reject changes to an already valid scalar");
    Expect(FfxHooks::ResolveF8Scalar(*gil).value == 7,
           "unavailable scalar rejection must preserve the previous value");

    FfxHooks::PublishF8RuntimeScalarStatus(
        "cheats.ap_100x", F8RuntimeAvailability::Available, true, true, true, 25);
    const auto apRuntime = FfxHooks::GetF8RuntimeStatus(*ap);
    Expect(apRuntime.hasAppliedValue && apRuntime.appliedValue &&
               apRuntime.hasAppliedScalar && apRuntime.appliedScalar == 25,
           "runtime status must distinguish exact scalar readback from boolean readback");
}

void TestFailedPersistenceNeverPublishes() {
    FakeState state;
    Configure(state,
              "[txn]\n"
              "bool_value = 0\n"
              "text_value = old\n"
              "auth_value = 0\n");
    state.persistSucceeds = false;

    Expect(!SetBool("txn.bool_value", true), "SetBool must report persistence failure");
    Expect(!GetBool("txn.bool_value", true), "failed SetBool must preserve the old in-memory value");
    Expect(!SetString("txn.text_value", "new"), "SetString must report persistence failure");
    Expect(strcmp(GetString("txn.text_value", "missing"), "old") == 0,
           "failed SetString must preserve the old in-memory value");

    const BoolGateSpec spec = {
        "txn.auth_value", "f8_authority.txn_auth_value", "legacy.auth_value",
        nullptr, nullptr, nullptr, nullptr, nullptr, false,
    };
    Expect(!SetAuthoritativeBool(spec, true),
           "SetAuthoritativeBool must report persistence failure");
    Expect(!GetBool("txn.auth_value", true),
           "failed authoritative write must preserve the old canonical value");
    bool marker = false;
    Expect(!TryGetBoolExact("f8_authority.txn_auth_value", &marker),
           "failed authoritative write must not publish its marker");
}

void TestPublicationOccursAfterPersistence() {
    FakeState state;
    Configure(state, "[txn]\nbool_value = 0\n");
    state.inspectKey = "txn.bool_value";

    Expect(SetBool("txn.bool_value", true), "successful SetBool must return true");
    Expect(!state.observedDuringPersist,
           "persistence callback must observe the old value before publication");
    Expect(GetBool("txn.bool_value", false),
           "new value must publish after persistence succeeds");
}

void TestAuthoritativeWriteIsOneCompleteGeneration() {
    FakeState state;
    Configure(state,
              "[field_scout]\nmaster = 0\n"
              "[labs]\nfield_scout = 1\n");

    Expect(SetAuthoritativeBool(MigratedSpec(), false),
           "authoritative write must persist successfully");
    Expect(state.persistCalls == 1,
           "canonical value and authority marker must use one persistence call");
    Expect(state.persistedText.find("master = 0") != std::string::npos,
           "authoritative generation must contain the canonical value");
    Expect(state.persistedText.find("[f8_authority]") != std::string::npos &&
               state.persistedText.find("field_scout_master = 1") != std::string::npos,
           "authoritative generation must contain the authority marker");
    const BoolGateResult result = ResolveBoolGate(MigratedSpec());
    Expect(!result.value && result.source == BoolSource::AuthoritativeCanonicalIni,
           "published authoritative generation must resolve from canonical INI");

    FakeState canonicalOnlyState;
    Configure(canonicalOnlyState, "[cheats]\ninvincible_party = 0\n");
    const BoolGateSpec canonicalOnly = {
        "cheats.invincible_party", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
    };
    Expect(SetAuthoritativeBool(canonicalOnly, true),
           "canonical-only authoritative write must persist successfully");
    Expect(canonicalOnlyState.persistedText.find("invincible_party = 1") != std::string::npos,
           "canonical-only write must contain the requested canonical value");
    Expect(canonicalOnlyState.persistedText.find("[f8_authority]") == std::string::npos,
           "canonical-only write must not invent an authority marker");
}

void TestMissingIniStartsFromSafeDefaults() {
    FakeState state;
    Configure(state, nullptr);

    Expect(SetString("maechen.extracted_path", "C:\\mods\\extracted"),
           "missing INI transaction must create target content");
    Expect(state.persistedPath == "C:\\rt0\\_isolated\\ffx-hooks.ini",
           "missing INI transaction must target the supplied canonical path");
    Expect(state.persistedText.find("[core]") != std::string::npos &&
               state.persistedText.find("log_level = 1") != std::string::npos,
           "missing INI transaction must begin with built-in defaults");
    Expect(state.persistedText.find("playable_seymour = 0") != std::string::npos &&
               state.persistedText.find("speed_hack = 0") != std::string::npos,
           "missing INI transaction must retain safe booster defaults");
    Expect(state.persistedText.find("[maechen]") != std::string::npos &&
               state.persistedText.find("extracted_path = C:\\mods\\extracted") != std::string::npos,
           "missing INI transaction must include the requested content");
}

void TestRealFileDefaultsOnlyForConfirmedAbsence() {
    const std::string path = RealRt0IniPath();
    Expect(!path.empty(), "real RT0 INI path must resolve inside obj/rt0");
    if (path.empty()) return;

    FakeState state;
    Expect(DeleteOwnRt0Ini(path), "real RT0 INI cleanup must remove only its owned file");
    ResetForTests();
    SetTestProviders(state, false);
    Expect(Load(), "confirmed missing INI must load built-in defaults");
    Expect(SetBool("rt0_disk.missing", true),
           "confirmed missing INI must persist defaults plus the requested update");
    std::string diskText;
    Expect(ReadWholeFile(path, diskText), "confirmed missing INI replacement must be readable");
    Expect(diskText.find("[core]\nlog_level = 1") != std::string::npos &&
               diskText.find("[rt0_disk]\nmissing = 1") != std::string::npos,
           "confirmed missing INI content must contain literal defaults and target update");

    static const char kExistingIni[] = "[existing]\nkeep = 9\n";
    Expect(WriteWholeFile(path, kExistingIni, static_cast<DWORD>(sizeof(kExistingIni) - 1)),
           "valid existing unloaded INI fixture must be created");
    ResetForTests();
    SetTestProviders(state, false);
    Expect(SetBool("rt0_disk.existing", true),
           "unloaded writer must snapshot a valid existing INI instead of defaults");
    diskText.clear();
    Expect(ReadWholeFile(path, diskText) &&
               diskText.find("[existing]\nkeep = 9") != std::string::npos &&
               diskText.find("[rt0_disk]\nexisting = 1") != std::string::npos &&
               diskText.find("[core]") == std::string::npos,
           "valid existing unloaded INI must be preserved without injected defaults");

    Expect(WriteWholeFile(path, nullptr, 0), "empty existing INI fixture must be created");
    ResetForTests();
    SetTestProviders(state, false);
    Expect(!Load(), "empty existing INI must be a read error, not absence");
    Expect(!SetBool("rt0_disk.empty", true),
           "writer must refuse to replace an empty existing INI after failed load");
    diskText = "not-empty";
    Expect(ReadWholeFile(path, diskText) && diskText.empty(),
           "empty existing INI must remain byte-for-byte empty");

    const std::string oversized(65536, 'X');
    Expect(WriteWholeFile(path, oversized.data(), static_cast<DWORD>(oversized.size())),
           "oversized existing INI fixture must be created");
    ResetForTests();
    SetTestProviders(state, false);
    Expect(!Load(), "oversized existing INI must be a read error, not absence");
    Expect(!SetBool("rt0_disk.oversized", true),
           "writer must refuse to replace an oversized existing INI after failed load");
    diskText.clear();
    Expect(ReadWholeFile(path, diskText) && diskText == oversized,
           "oversized existing INI must remain byte-for-byte unchanged");

    static const char kLockedIni[] = "[locked]\nvalue = 7\n";
    Expect(WriteWholeFile(path, kLockedIni, static_cast<DWORD>(sizeof(kLockedIni) - 1)),
           "locked existing INI fixture must be created");
    HANDLE locked = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(locked != INVALID_HANDLE_VALUE, "existing INI must be exclusively lockable for RT0");
    if (locked != INVALID_HANDLE_VALUE) {
        ResetForTests();
        SetTestProviders(state, false);
        Expect(!Load(), "inaccessible existing INI must be a read error, not absence");
        Expect(!SetBool("rt0_disk.locked", true),
               "writer must refuse to replace an inaccessible existing INI after failed load");
        Expect(CloseHandle(locked) != FALSE, "exclusive RT0 INI handle must close");
        diskText.clear();
        Expect(ReadWholeFile(path, diskText) && diskText == kLockedIni,
               "inaccessible existing INI must remain byte-for-byte unchanged");
    }

    ResetForTests();
    Expect(DeleteOwnRt0Ini(path), "real RT0 INI cleanup must remove its owned file after testing");
}

void TestConcurrentReadersSeeWholeValues() {
    static const char kValueA[] =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    static const char kValueB[] =
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

    FakeState state;
    Configure(state,
              "[race]\n"
              "value = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
              "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
    state.blockFirstPersist = true;

    struct ReaderProgress {
        std::mutex mutex;
        std::condition_variable condition;
        int ready = 0;
        int readersWithObservation = 0;
        int observations = 0;
        int readersThatSawNew = 0;
        bool start = false;
        bool stop = false;
        bool invalid = false;
    } progress;

    std::atomic<bool> writerOk{false};
    std::thread writer([&]() { writerOk.store(SetString("race.value", kValueB)); });
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        Expect(WaitForCondition(state.condition, lock, [&]() { return state.firstPersistEntered; }),
               "reader test writer must reach the blocked persistence boundary");
    }

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            {
                std::unique_lock<std::mutex> lock(progress.mutex);
                ++progress.ready;
                progress.condition.notify_all();
                progress.condition.wait(lock, [&]() { return progress.start; });
            }
            bool contributedObservation = false;
            bool sawNew = false;
            for (;;) {
                const char* value = GetString("race.value", "missing");
                const bool isOld = strcmp(value, kValueA) == 0;
                const bool isNew = strcmp(value, kValueB) == 0;
                std::unique_lock<std::mutex> lock(progress.mutex);
                ++progress.observations;
                if (!contributedObservation) {
                    contributedObservation = true;
                    ++progress.readersWithObservation;
                }
                if (!isOld && !isNew) progress.invalid = true;
                if (isNew && !sawNew) {
                    sawNew = true;
                    ++progress.readersThatSawNew;
                }
                progress.condition.notify_all();
                if (progress.stop) break;
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(progress.mutex);
        Expect(WaitForCondition(progress.condition, lock, [&]() { return progress.ready == 4; }),
               "all reader threads must reach the explicit start barrier");
        progress.start = true;
        progress.condition.notify_all();
        Expect(WaitForCondition(progress.condition, lock, [&]() {
                   return progress.readersWithObservation == 4 || progress.invalid;
               }),
               "every reader must make an observation while persistence is blocked");
        Expect(progress.observations >= 4 && progress.readersWithObservation == 4,
               "reader test must record a nonzero observation from every reader");
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.releaseFirstPersist = true;
        state.condition.notify_all();
    }
    writer.join();

    {
        std::unique_lock<std::mutex> lock(progress.mutex);
        Expect(WaitForCondition(progress.condition, lock, [&]() {
                   return progress.readersThatSawNew == 4 || progress.invalid;
               }),
               "every reader must observe the published generation after persistence releases");
        progress.stop = true;
        progress.condition.notify_all();
    }
    for (std::thread& reader : readers) reader.join();
    Expect(writerOk.load(), "coordinated reader-test writer must succeed");
    Expect(!progress.invalid,
           "concurrent readers must observe only complete old or new string values");
}

void TestConcurrentWritersCannotLoseUpdates() {
    FakeState state;
    Configure(state,
              "[generation]\n"
              "alpha = 0\n"
              "beta = 0\n");
    state.blockFirstPersist = true;

    std::atomic<bool> alphaOk{false};
    std::atomic<bool> betaOk{false};
    std::thread alpha([&]() { alphaOk.store(SetBool("generation.alpha", true)); });
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        Expect(WaitForCondition(state.condition, lock, [&]() { return state.firstPersistEntered; }),
               "writer A must be held at the first persistence boundary");
    }

    struct WriterProgress {
        std::mutex mutex;
        std::condition_variable condition;
        bool ready = false;
        bool start = false;
        bool aboutToCall = false;
        bool returned = false;
    } progress;

    std::thread beta([&]() {
        {
            std::unique_lock<std::mutex> lock(progress.mutex);
            progress.ready = true;
            progress.condition.notify_all();
            progress.condition.wait(lock, [&]() { return progress.start; });
            progress.aboutToCall = true;
            progress.condition.notify_all();
        }
        betaOk.store(SetBool("generation.beta", true));
        {
            std::lock_guard<std::mutex> lock(progress.mutex);
            progress.returned = true;
            progress.condition.notify_all();
        }
    });

    {
        std::unique_lock<std::mutex> lock(progress.mutex);
        Expect(WaitForCondition(progress.condition, lock, [&]() { return progress.ready; }),
               "writer B must reach the explicit start barrier");
        progress.start = true;
        progress.condition.notify_all();
        Expect(WaitForCondition(progress.condition, lock, [&]() { return progress.aboutToCall; }),
               "writer B must attempt its transaction while writer A is held at persistence");
        Expect(!progress.returned, "writer B must remain in flight while writer A owns the transaction");
    }
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        const bool staleSnapshotReachedPersistence = WaitForCondition(
            state.condition, lock, [&]() { return state.secondPersistEntered; });
        Expect(!staleSnapshotReachedPersistence && state.persistCalls == 1,
               "writer B must not persist a stale snapshot while writer A is blocked");
        state.releaseFirstPersist = true;
        state.condition.notify_all();
    }
    alpha.join();
    beta.join();

    Expect(alphaOk.load() && betaOk.load(), "both concurrent writers must succeed");
    Expect(state.persistCalls == 2, "two serialized writers must publish two generations");
    Expect(state.persistedGenerations.size() == 2 &&
               state.persistedGenerations[0].find("alpha = 1") != std::string::npos &&
               state.persistedGenerations[0].find("beta = 0") != std::string::npos,
           "writer A persistence generation must contain only the hand-derived first update");
    Expect(state.persistedGenerations.size() == 2 &&
               state.persistedGenerations[1].find("alpha = 1") != std::string::npos &&
               state.persistedGenerations[1].find("beta = 1") != std::string::npos,
           "writer B snapshot after release must include writer A before adding beta");
    Expect(GetBool("generation.alpha", false) && GetBool("generation.beta", false),
           "final memory generation must contain both writer updates");
    Expect(state.persistedText.find("alpha = 1") != std::string::npos &&
               state.persistedText.find("beta = 1") != std::string::npos,
           "final persisted generation must contain both writer updates");
}

void PutU16(std::array<uint8_t, 512>& image, size_t offset, uint16_t value) {
    image[offset] = static_cast<uint8_t>(value & 0xFFu);
    image[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void PutU32(std::array<uint8_t, 512>& image, size_t offset, uint32_t value) {
    image[offset] = static_cast<uint8_t>(value & 0xFFu);
    image[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    image[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    image[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

std::array<uint8_t, 512> MakeSupportedPeHeader() {
    std::array<uint8_t, 512> image{};
    image[0] = 'M';
    image[1] = 'Z';
    constexpr size_t peOffset = 0x80;
    PutU32(image, 0x3C, static_cast<uint32_t>(peOffset));
    image[peOffset] = 'P';
    image[peOffset + 1] = 'E';
    PutU16(image, peOffset + 4, 0x014C);
    PutU32(image, peOffset + 8, 0x55D2F3CC);
    PutU16(image, peOffset + 20, 0x00E0);
    PutU16(image, peOffset + 24, 0x010B);
    PutU32(image, peOffset + 24 + 56, 0x0237D000);
    return image;
}

void TestRuntimeCorePeProfileAndRanges() {
    using namespace FfxHooks::F8Runtime;
    auto image = MakeSupportedPeHeader();
    ExecutableIdentity identity{};
    Expect(ParseExecutableIdentity(image.data(), image.size(), &identity) ==
               ProfileResult::Supported,
           "supported PE32/I386 profile must parse");
    Expect(identity.machine == 0x014C && identity.optionalMagic == 0x010B &&
               identity.timestamp == 0x55D2F3CC && identity.sizeOfImage == 0x0237D000,
           "PE parser must expose the exact supported executable identity");
    Expect(IsSupportedExecutable(identity), "exact executable identity must be supported");

    auto bad = image;
    bad[0] = 'N';
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) == ProfileResult::BadDos,
           "bad DOS signature must fail closed");
    Expect(ParseExecutableIdentity(image.data(), 0x3F, &identity) == ProfileResult::BadDos,
           "truncated DOS header must fail closed");

    bad = image;
    bad[0x80] = 'X';
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) == ProfileResult::BadPe,
           "bad PE signature must fail closed");
    bad = image;
    memmove(bad.data() + 0x10, image.data() + 0x80, 24 + 0xE0);
    PutU32(bad, 0x3C, 0x10);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) == ProfileResult::BadPe,
           "PE header overlapping the DOS header must fail closed");
    bad = image;
    PutU32(bad, 0x3C, 0x1F8);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) == ProfileResult::BadPe,
           "truncated PE header must fail closed");

    bad = image;
    PutU16(bad, 0x80 + 20, 0x0030);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) ==
               ProfileResult::BadOptionalHeader,
           "undersized optional header must fail closed");
    Expect(ParseExecutableIdentity(image.data(), 0x80 + 24 + 0xDF, &identity) ==
               ProfileResult::BadOptionalHeader,
           "truncated declared optional header must fail closed");
    bad = image;
    PutU16(bad, 0x80 + 24, 0x020B);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) ==
               ProfileResult::BadOptionalHeader,
           "non-PE32 optional magic must fail closed");

    bad = image;
    PutU16(bad, 0x80 + 4, 0x8664);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) ==
               ProfileResult::WrongMachine,
           "non-I386 machine must be rejected");
    bad = image;
    PutU32(bad, 0x80 + 8, 0x55D2F3CD);
    identity = {1, 2, 3, 4};
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) ==
               ProfileResult::WrongTimestamp,
           "wrong executable timestamp must be rejected");
    Expect(identity.machine == 0x014C && identity.optionalMagic == 0x010B &&
               identity.timestamp == 0x55D2F3CD && identity.sizeOfImage == 0x0237D000,
           "well-formed unsupported PE must still report its parsed identity");
    bad = image;
    PutU32(bad, 0x80 + 24 + 56, 0x0237CFFF);
    Expect(ParseExecutableIdentity(bad.data(), bad.size(), &identity) ==
               ProfileResult::WrongImageSize,
           "wrong SizeOfImage must be rejected");

    Expect(ValidateImageRange(0, 1, 0x0237D000) == ProfileResult::Supported,
           "first image byte must be a valid range");
    Expect(ValidateImageRange(0x0237CFFF, 1, 0x0237D000) == ProfileResult::Supported,
           "last image byte must be a valid range");
    Expect(ValidateImageRange(0x0237D000, 0, 0x0237D000) == ProfileResult::Supported,
           "empty range at image end must be valid");
    Expect(ValidateImageRange(0x0237D000, 1, 0x0237D000) == ProfileResult::OutOfImage,
           "range beyond SizeOfImage must fail closed");
    Expect(ValidateImageRange(0xFFFFFFF0u, 0x20u, 0xFFFFFFFFu) ==
               ProfileResult::RangeOverflow,
           "RVA plus length overflow must be distinguished");
}

void TestRewardMultiplierEvidenceAndEncoding() {
    using namespace FfxHooks::F8Runtime;
    const RewardHookSpec& ap = RewardHookSpecFor(RewardKind::Ap);
    const RewardHookSpec& gil = RewardHookSpecFor(RewardKind::Gil);
    const std::array<uint8_t, 8> apSignature = {
        0x74u, 0x06u, 0x6Bu, 0xC0u, 0x64u, 0x89u, 0x45u, 0xFCu};
    const std::array<uint8_t, 8> gilSignature = {
        0x74u, 0x06u, 0x6Bu, 0xC0u, 0x64u, 0x89u, 0x45u, 0xF8u};

    Expect(RVA_FFX_AP_MULTIPLIER_SIGNATURE == 0x00399121u &&
               RVA_FFX_AP_MULTIPLIER_SITE == 0x00399123u &&
               RVA_FFX_AP_MULTIPLIER_IMMEDIATE == 0x00399125u &&
               RVA_FFX_AP_MULTIPLIER_RESUME == 0x00399129u,
           "AP reward ledger must retain the exact signature/site/immediate/resume RVAs");
    Expect(RVA_FFX_GIL_MULTIPLIER_SIGNATURE == 0x0039913Cu &&
               RVA_FFX_GIL_MULTIPLIER_SITE == 0x0039913Eu &&
               RVA_FFX_GIL_MULTIPLIER_IMMEDIATE == 0x00399140u &&
               RVA_FFX_GIL_MULTIPLIER_RESUME == 0x00399144u,
           "Gil reward ledger must retain the exact signature/site/immediate/resume RVAs");
    Expect(ap.signatureRva == RVA_FFX_AP_MULTIPLIER_SIGNATURE &&
               ap.siteRva == RVA_FFX_AP_MULTIPLIER_SITE &&
               ap.immediateRva == RVA_FFX_AP_MULTIPLIER_IMMEDIATE &&
               ap.resumeRva == RVA_FFX_AP_MULTIPLIER_RESUME &&
               ap.storeDisplacement == 0xFCu && ap.signature == apSignature,
           "portable AP hook spec must match the complete immutable evidence");
    Expect(gil.signatureRva == RVA_FFX_GIL_MULTIPLIER_SIGNATURE &&
               gil.siteRva == RVA_FFX_GIL_MULTIPLIER_SITE &&
               gil.immediateRva == RVA_FFX_GIL_MULTIPLIER_IMMEDIATE &&
               gil.resumeRva == RVA_FFX_GIL_MULTIPLIER_RESUME &&
               gil.storeDisplacement == 0xF8u && gil.signature == gilSignature,
           "portable Gil hook spec must match the complete immutable evidence");
    Expect(ap.immediateRva - ap.signatureRva == 4u &&
               gil.immediateRva - gil.signatureRva == 4u &&
               ap.siteRva + kRewardPatchSize == ap.resumeRva &&
               gil.siteRva + kRewardPatchSize == gil.resumeRva &&
               kRewardPatchSize == 6u && kRewardStubSize == 15u,
           "reward overwrite and stub widths must remain exact");
    Expect(ValidateImageRange(ap.siteRva, kRewardPatchSize, 0x0237D000u) ==
               ProfileResult::Supported &&
               ValidateImageRange(gil.siteRva, kRewardPatchSize, 0x0237D000u) ==
               ProfileResult::Supported,
           "both reward patch spans must fit the supported image");

    Expect(ValidateRewardSignature(ap, apSignature.data(), apSignature.size()) &&
               ValidateRewardSignature(gil, gilSignature.data(), gilSignature.size()),
           "complete immutable reward signatures must validate");
    for (size_t byte = 0; byte < apSignature.size(); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto mutant = apSignature;
            mutant[byte] ^= static_cast<uint8_t>(1u << bit);
            Expect(!ValidateRewardSignature(ap, mutant.data(), mutant.size()),
                   "every one-bit AP signature mutation must fail closed");
        }
    }
    for (size_t byte = 0; byte < gilSignature.size(); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto mutant = gilSignature;
            mutant[byte] ^= static_cast<uint8_t>(1u << bit);
            Expect(!ValidateRewardSignature(gil, mutant.data(), mutant.size()),
                   "every one-bit Gil signature mutation must fail closed");
        }
    }

    constexpr uintptr_t stubAddress = 0x10000000u;
    constexpr uintptr_t scalarAddress = 0x20000000u;
    constexpr uintptr_t moduleBase = 0x00400000u;
    std::array<uint8_t, kRewardStubSize> stub{};
    Expect(EncodeRewardStub(ap, stubAddress, scalarAddress,
                            moduleBase + ap.resumeRva, &stub),
           "AP stub must encode with a rel32 resume");
    Expect(stub[0] == 0x0Fu && stub[1] == 0xAFu && stub[2] == 0x05u &&
               stub[7] == 0x89u && stub[8] == 0x45u && stub[9] == 0xFCu &&
               stub[10] == 0xE9u,
           "AP stub must use non-clobbering absolute IMUL, original store, and rel32 jump");
    uint32_t encodedScalar = 0;
    int32_t encodedResume = 0;
    memcpy(&encodedScalar, stub.data() + 3, sizeof(encodedScalar));
    memcpy(&encodedResume, stub.data() + 11, sizeof(encodedResume));
    Expect(encodedScalar == scalarAddress &&
               static_cast<int64_t>(stubAddress + kRewardStubSize) + encodedResume ==
                   static_cast<int64_t>(moduleBase + ap.resumeRva),
           "stub operands must target the exact aligned scalar and resume address");

    std::array<uint8_t, kRewardPatchSize> jump{};
    Expect(EncodeRewardSiteJump(moduleBase + ap.siteRva, stubAddress, &jump) &&
               jump[0] == 0xE9u && jump[5] == 0x90u,
           "reward site must encode exactly E9 rel32 plus one NOP");
    int32_t encodedTarget = 0;
    memcpy(&encodedTarget, jump.data() + 1, sizeof(encodedTarget));
    Expect(static_cast<int64_t>(moduleBase + ap.siteRva + 5u) + encodedTarget ==
               static_cast<int64_t>(stubAddress),
           "reward site rel32 must target its owned stub exactly");

    Expect(IsRewardMultiplierInRange(1) && IsRewardMultiplierInRange(100) &&
               !IsRewardMultiplierInRange(0) && !IsRewardMultiplierInRange(101),
           "portable multiplier range must remain exactly 1 through 100");
    Expect(IsRewardWorstCaseSafe(RewardKind::Ap, 100) &&
               IsRewardWorstCaseSafe(RewardKind::Gil, 100),
           "100x AP/Gil worst cases must remain below INT32_MAX");
}

struct FakeRewardHookMemory {
    const FfxHooks::F8Runtime::RewardHookSpec* spec = nullptr;
    uintptr_t moduleBase = 0x00400000u;
    uintptr_t stubAddress = 0x10000000u;
    std::array<uint8_t, FfxHooks::F8Runtime::kRewardSignatureSize> signature{};
    std::array<uint8_t, FfxHooks::F8Runtime::kRewardStubSize> stub{};
    std::vector<std::string> events;
    bool stubAllocated = false;
    bool stubRx = false;
    bool allocationSucceeds = true;
    bool stubWriteSucceeds = true;
    bool failFirstStubRead = false;
    bool failFinalStubRead = false;
    bool failProtectStub = false;
    bool failStubFlush = false;
    bool battleInactiveConfirmed = true;
    bool beginSiteSucceeds = true;
    bool beginSiteLeavesActiveToken = false;
    bool raceSiteBeforeCompare = false;
    bool siteWriteSucceeds = true;
    bool siteWriteMutatesOnFailure = false;
    bool failSiteFlush = false;
    bool failEndSite = false;
    bool failSiteReadback = false;
    int failSiteReadCall = 0;
    bool freeSucceeds = true;
    int stubReadCalls = 0;
    int siteReadCalls = 0;

    explicit FakeRewardHookMemory(FfxHooks::F8Runtime::RewardKind kind) {
        spec = &FfxHooks::F8Runtime::RewardHookSpecFor(kind);
        signature = spec->signature;
    }

    uintptr_t SignatureAddress() const { return moduleBase + spec->signatureRva; }
    uintptr_t SiteAddress() const { return moduleBase + spec->siteRva; }
};

bool FakeRewardRead(void* context, uintptr_t address, uint8_t* bytes, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    if (!bytes) return false;
    if (address == memory.SignatureAddress() && length == memory.signature.size()) {
        memory.events.emplace_back("read-signature");
        memcpy(bytes, memory.signature.data(), length);
        return true;
    }
    if (address == memory.SiteAddress() && length == FfxHooks::F8Runtime::kRewardPatchSize) {
        memory.events.emplace_back("read-site");
        ++memory.siteReadCalls;
        if (memory.failSiteReadback ||
            (memory.failSiteReadCall != 0 &&
             memory.siteReadCalls == memory.failSiteReadCall)) return false;
        memcpy(bytes, memory.signature.data() + 2, length);
        return true;
    }
    if (address == memory.stubAddress && length == memory.stub.size() && memory.stubAllocated) {
        memory.events.emplace_back("read-stub");
        ++memory.stubReadCalls;
        if ((memory.stubReadCalls == 1 && memory.failFirstStubRead) ||
            (memory.stubReadCalls == 2 && memory.failFinalStubRead)) {
            return false;
        }
        memcpy(bytes, memory.stub.data(), length);
        return true;
    }
    return false;
}

bool FakeRewardAllocateWritable(void* context, size_t length, uintptr_t* addressOut) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("allocate-rw");
    if (!memory.allocationSucceeds || !addressOut || length != memory.stub.size() ||
        memory.stubAllocated) return false;
    memory.stubAllocated = true;
    memory.stubRx = false;
    *addressOut = memory.stubAddress;
    return true;
}

FfxHooks::F8Runtime::MutationReport FakeRewardWriteWritable(
    void* context, uintptr_t address, const uint8_t* bytes, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("write-stub");
    if (!memory.stubWriteSucceeds || !bytes || address != memory.stubAddress ||
        length != memory.stub.size() ||
        !memory.stubAllocated || memory.stubRx) {
        return {FfxHooks::F8Runtime::MutationEffect::NotTouched, false};
    }
    memcpy(memory.stub.data(), bytes, length);
    return {FfxHooks::F8Runtime::MutationEffect::Verified, true};
}

bool FakeRewardProtectExecuteRead(void* context, uintptr_t address, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("protect-rx");
    if (memory.failProtectStub || address != memory.stubAddress || length != memory.stub.size()) {
        return false;
    }
    memory.stubRx = true;
    return true;
}

bool FakeRewardBeginCodeWrite(
    void* context, uintptr_t address, size_t length,
    FfxHooks::F8Runtime::PatchProtectionToken* token) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("begin-site");
    if (!token || address != memory.SiteAddress() ||
        length != FfxHooks::F8Runtime::kRewardPatchSize) return false;
    *token = {address, length, 0x20u, true};
    if (!memory.beginSiteSucceeds) {
        if (!memory.beginSiteLeavesActiveToken) token->active = false;
        return false;
    }
    return true;
}

bool FakeRewardConfirmBattleInactive(void* context) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("confirm-battle-inactive");
    return memory.battleInactiveConfirmed;
}

FfxHooks::F8Runtime::MutationReport FakeRewardWriteIfEqual(
    void* context, uintptr_t address, const uint8_t* expected,
    const uint8_t* desired, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("write-site");
    if (memory.raceSiteBeforeCompare) {
        memory.signature[2] = 0xCCu;
        memory.raceSiteBeforeCompare = false;
    }
    if (!expected || !desired || address != memory.SiteAddress() ||
        length != FfxHooks::F8Runtime::kRewardPatchSize ||
        memcmp(memory.signature.data() + 2, expected, length) != 0) {
        return {FfxHooks::F8Runtime::MutationEffect::NotTouched, false};
    }
    if (!memory.siteWriteSucceeds) {
        if (memory.siteWriteMutatesOnFailure) {
            memcpy(memory.signature.data() + 2, desired, length);
        }
        return {FfxHooks::F8Runtime::MutationEffect::MayHaveChanged, false};
    }
    memcpy(memory.signature.data() + 2, desired, length);
    return {FfxHooks::F8Runtime::MutationEffect::Verified, true};
}

bool FakeRewardFlush(void* context, uintptr_t address, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    if (address == memory.stubAddress && length == memory.stub.size()) {
        memory.events.emplace_back("flush-stub");
        return !memory.failStubFlush;
    }
    if (address == memory.SiteAddress() && length == FfxHooks::F8Runtime::kRewardPatchSize) {
        memory.events.emplace_back("flush-site");
        return !memory.failSiteFlush;
    }
    return false;
}

bool FakeRewardEndCodeWrite(
    void* context, FfxHooks::F8Runtime::PatchProtectionToken* token) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("end-site");
    if (!token || !token->active || memory.failEndSite) return false;
    token->active = false;
    return true;
}

bool FakeRewardFree(void* context, uintptr_t address, size_t length) {
    auto& memory = *static_cast<FakeRewardHookMemory*>(context);
    memory.events.emplace_back("free-stub");
    if (!memory.freeSucceeds || address != memory.stubAddress || length != memory.stub.size()) {
        return false;
    }
    memory.stubAllocated = false;
    memory.stubRx = false;
    memory.stub.fill(0);
    return true;
}

FfxHooks::F8Runtime::RewardHookIo MakeRewardHookIo(FakeRewardHookMemory& memory) {
    return {&memory, &FakeRewardRead, &FakeRewardAllocateWritable, &FakeRewardWriteWritable,
            &FakeRewardProtectExecuteRead, &FakeRewardConfirmBattleInactive,
            &FakeRewardBeginCodeWrite,
            &FakeRewardWriteIfEqual, &FakeRewardFlush, &FakeRewardEndCodeWrite, &FakeRewardFree};
}

bool RewardEventsEqual(const FakeRewardHookMemory& memory,
                       std::initializer_list<const char*> expected) {
    if (memory.events.size() != expected.size()) return false;
    size_t index = 0;
    for (const char* event : expected) {
        if (memory.events[index++] != event) return false;
    }
    return true;
}

void TestRewardHookInstallAndRemovalTransaction() {
    using namespace FfxHooks::F8Runtime;
    FakeRewardHookMemory memory(RewardKind::Ap);
    RewardHookState state{};
    Expect(PrepareRewardHookState(
               RewardKind::Ap, memory.moduleBase, 0x20000000u, &state),
           "reward hook state must prepare from exact base/spec/scalar addresses");
    Expect(InstallRewardHook(MakeRewardHookIo(memory), true, &state) ==
               RewardHookResult::DeferredBattleActive && memory.events.empty(),
           "reward installation must be inert while battle is active");
    Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::Installed,
           "battle-inactive Present work must install the exact reward hook");
    Expect(RewardEventsEqual(memory, {
               "read-signature", "allocate-rw", "write-stub", "read-stub",
               "protect-rx", "flush-stub", "read-stub", "confirm-battle-inactive", "begin-site",
               "write-site", "flush-site", "end-site", "read-site"}),
           "reward install must enforce signature, W^X stub, flush/readback, then site publication");
    Expect(state.state == RewardHookOwnership::Installed && state.stubOwned &&
               state.stubVerifiedRx && state.siteMayPointToStub &&
               memory.stubAllocated && memory.stubRx,
           "successful installation must retain exact site and stub ownership");
    const size_t installedEvents = memory.events.size();
    Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::AlreadyInstalled && memory.events.size() == installedEvents,
           "an installed site must never be rewritten by a later Present tick");

    Expect(RemoveRewardHook(MakeRewardHookIo(memory), true, &state) ==
               RewardHookResult::DeferredBattleActive && memory.stubAllocated,
           "normal-context removal must refuse while battle is active");
    memory.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::Removed,
           "safe normal-context removal must restore the original span before freeing the stub");
    Expect(RewardEventsEqual(memory, {
               "read-site", "confirm-battle-inactive", "begin-site", "write-site",
               "flush-site", "end-site", "read-site", "free-stub"}),
           "removal must read back original code before releasing its executable allocation");
    Expect(state.state == RewardHookOwnership::Empty && !state.stubOwned &&
               !state.siteMayPointToStub && !memory.stubAllocated,
           "completed removal must release all reward-hook ownership");
}

void TestRewardHookRetainedStubCleanupAndBattleResample() {
    using namespace FfxHooks::F8Runtime;

    enum class StubFault { Encode, Write, FirstRead, Protect, Flush, FinalRead };
    for (const StubFault fault : {StubFault::Encode, StubFault::Write, StubFault::FirstRead,
                                  StubFault::Protect, StubFault::Flush, StubFault::FinalRead}) {
        FakeRewardHookMemory memory(RewardKind::Ap);
        if (fault == StubFault::Encode) memory.stubAddress = 0xF0000000u;
        if (fault == StubFault::Write) memory.stubWriteSucceeds = false;
        if (fault == StubFault::FirstRead) memory.failFirstStubRead = true;
        if (fault == StubFault::Protect) memory.failProtectStub = true;
        if (fault == StubFault::Flush) memory.failStubFlush = true;
        if (fault == StubFault::FinalRead) memory.failFinalStubRead = true;
        memory.freeSucceeds = false;

        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Failed &&
                   state.state == RewardHookOwnership::StubCleanupPending &&
                   state.stubOwned && !state.stubVerifiedRx && !state.siteMayPointToStub &&
                   std::find(memory.events.begin(), memory.events.end(), "begin-site") ==
                       memory.events.end(),
               "every retained prepublication stub fault must become sticky cleanup-pending");

        memory.events.clear();
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending && memory.events.empty(),
               "a retained unverified stub retry must never reach site publication");

        memory.freeSucceeds = true;
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Removed &&
                   state.state == RewardHookOwnership::Empty && !memory.stubAllocated,
               "normal-context cleanup may free an unverified stub because no site targeted it");
    }

    FakeRewardHookMemory allocationFailure(RewardKind::Ap);
    allocationFailure.allocationSucceeds = false;
    RewardHookState allocationState{};
    PrepareRewardHookState(
        RewardKind::Ap, allocationFailure.moduleBase, 0x20000000u, &allocationState);
    Expect(InstallRewardHook(MakeRewardHookIo(allocationFailure), false, &allocationState) ==
               RewardHookResult::Failed && !allocationState.stubOwned &&
               allocationState.state == RewardHookOwnership::Empty,
           "allocation failure must leave no cleanup obligation or publication candidate");

    FakeRewardHookMemory battleFlip(RewardKind::Gil);
    battleFlip.battleInactiveConfirmed = false;
    RewardHookState battleState{};
    PrepareRewardHookState(RewardKind::Gil, battleFlip.moduleBase, 0x20000004u, &battleState);
    Expect(InstallRewardHook(MakeRewardHookIo(battleFlip), false, &battleState) ==
               RewardHookResult::DeferredBattleActive &&
               battleState.state == RewardHookOwnership::StubReady &&
               battleState.stubVerifiedRx &&
               std::find(battleFlip.events.begin(), battleFlip.events.end(), "begin-site") ==
                   battleFlip.events.end(),
           "battle must be re-sampled after stub work immediately before site publication");
    battleFlip.battleInactiveConfirmed = true;
    battleFlip.events.clear();
    Expect(InstallRewardHook(MakeRewardHookIo(battleFlip), false, &battleState) ==
               RewardHookResult::Installed,
           "a verified RX stub may publish on a later independently admitted inactive sample");
    battleFlip.battleInactiveConfirmed = false;
    battleFlip.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(battleFlip), false, &battleState) ==
               RewardHookResult::DeferredBattleActive && battleState.stubOwned &&
               battleState.siteMayPointToStub &&
               std::find(battleFlip.events.begin(), battleFlip.events.end(), "begin-site") ==
                   battleFlip.events.end(),
           "removal must also re-sample battle state after site observation and before code write");
    battleFlip.battleInactiveConfirmed = true;
    Expect(RemoveRewardHook(MakeRewardHookIo(battleFlip), false, &battleState) ==
               RewardHookResult::Removed,
           "deferred removal must complete on a later independently admitted inactive sample");
}

void TestRewardHookFailClosedTransactions() {
    using namespace FfxHooks::F8Runtime;
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.signature[0] ^= 1u;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::SignatureMismatch &&
                   RewardEventsEqual(memory, {"read-signature"}) && !memory.stubAllocated,
               "full-signature mismatch must fail before allocation or code mutation");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.signature[2] = 0xE9u;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Conflict && !memory.stubAllocated,
               "a pre-existing site jump must be classified as conflict without overwrite");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.failProtectStub = true;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Failed &&
                   std::find(memory.events.begin(), memory.events.end(), "begin-site") ==
                       memory.events.end(),
               "stub execute/read transition failure must prevent all site writes");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Gil);
        memory.failSiteFlush = true;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Gil, memory.moduleBase, 0x20000004u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending && state.stubOwned &&
                   state.siteMayPointToStub && memory.stubAllocated,
               "ambiguous site publication must retain its stub and restoration obligation");
        memory.failSiteFlush = false;
        memory.signature[2] = 0xCCu;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Conflict && memory.stubAllocated,
               "a third site value must block stub release and remain a sticky conflict");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.failEndSite = true;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending && state.protection.active,
               "failed protection restoration must retain its exact active token");
        memory.failEndSite = false;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Removed &&
                   RewardEventsEqual(memory, {
                       "end-site", "read-site", "confirm-battle-inactive", "begin-site",
                       "write-site", "flush-site", "end-site", "read-site", "free-stub"}),
               "normal-context removal must first discharge a pending protection token");
    }
}

void TestRewardHookSiteFaultRecoveryAndSharedPageIsolation() {
    using namespace FfxHooks::F8Runtime;

    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.beginSiteSucceeds = false;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Failed &&
                   state.state == RewardHookOwnership::StubReady &&
                   state.stubVerifiedRx && !state.protection.active,
               "a clean begin-code-write rejection must retain only a verified RX stub");
    }
    {
        FakeRewardHookMemory ap(RewardKind::Ap);
        FakeRewardHookMemory gil(RewardKind::Gil);
        ap.beginSiteSucceeds = false;
        ap.beginSiteLeavesActiveToken = true;
        RewardHookState states[2] = {};
        PrepareRewardHookState(RewardKind::Ap, ap.moduleBase, 0x20000000u, &states[0]);
        PrepareRewardHookState(RewardKind::Gil, gil.moduleBase, 0x20000004u, &states[1]);
        Expect(InstallRewardHook(MakeRewardHookIo(ap), false, &states[0]) ==
                   RewardHookResult::RestorePending && states[0].protection.active,
               "a begin failure that changed page protection must retain its active token");
        Expect(CanRunRewardHookTransaction(states, 2, 0) &&
                   !CanRunRewardHookTransaction(states, 2, 1),
               "the token owner may recover while the peer sharing its page must defer");
        ap.beginSiteSucceeds = true;
        ap.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(ap), false, &states[0]) ==
                   RewardHookResult::Removed &&
                   RewardEventsEqual(ap, {"end-site", "read-site", "free-stub"}),
               "recovery must discharge protection then free only after exact original readback");
        Expect(CanRunRewardHookTransaction(states, 2, 1),
               "the peer transaction may proceed only after the shared-page token is discharged");
        PrepareRewardHookState(RewardKind::Ap, ap.moduleBase, 0x20000000u, &states[0]);
        Expect(states[0].state == RewardHookOwnership::Empty && states[0].moduleBase != 0,
               "fresh hook preparation must occur only after completed removal");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.raceSiteBeforeCompare = true;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Conflict && state.state == RewardHookOwnership::Conflict &&
                   memory.signature[2] == 0xCCu,
               "a third byte appearing between signature validation and compare must not be overwritten");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Gil);
        memory.siteWriteSucceeds = false;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Gil, memory.moduleBase, 0x20000004u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending &&
               state.state == RewardHookOwnership::RestorePending &&
                   state.siteMayPointToStub && memory.stubAllocated,
               "a potentially-mutating install callback must retain cache provenance even when bytes read original");
        memory.battleInactiveConfirmed = false;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::DeferredBattleActive && state.stubOwned &&
                   state.siteMayPointToStub && memory.stubAllocated &&
                   RewardEventsEqual(memory, {"read-site", "confirm-battle-inactive"}),
               "ambiguous original-cache recovery must retain the stub when battle proof fails");
        memory.battleInactiveConfirmed = true;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Removed &&
                   RewardEventsEqual(memory, {
                       "read-site", "confirm-battle-inactive", "flush-site", "read-site",
                       "free-stub"}),
               "ambiguous install recovery must flush exact original bytes before freeing its stub");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        memory.failSiteReadback = true;
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending && state.siteMayPointToStub &&
                   memory.stubAllocated,
               "unreadable site publication must retain restoration and stub ownership");
    }
    {
        FakeRewardHookMemory memory(RewardKind::Ap);
        RewardHookState state{};
        PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
        Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Installed,
               "free-retry fixture must begin from an installed hook");
        memory.freeSucceeds = false;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::RestorePending && !state.siteMayPointToStub &&
                   memory.stubAllocated,
               "failed free after exact restoration must retain a retryable allocation obligation");
        memory.freeSucceeds = true;
        memory.events.clear();
        Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
                   RewardHookResult::Removed &&
                   RewardEventsEqual(memory, {"read-site", "free-stub"}),
               "free retry must re-prove exact original code and avoid another code write");
    }
    {
        FakeRewardHookMemory ap(RewardKind::Ap);
        FakeRewardHookMemory gil(RewardKind::Gil);
        ap.allocationSucceeds = false;
        RewardHookState apState{};
        RewardHookState gilState{};
        PrepareRewardHookState(RewardKind::Ap, ap.moduleBase, 0x20000000u, &apState);
        PrepareRewardHookState(RewardKind::Gil, gil.moduleBase, 0x20000004u, &gilState);
        Expect(InstallRewardHook(MakeRewardHookIo(ap), false, &apState) ==
                   RewardHookResult::Failed &&
                   InstallRewardHook(MakeRewardHookIo(gil), false, &gilState) ==
                       RewardHookResult::Installed,
               "an ordinary AP allocation failure must not disable the independent Gil path");
    }

    Expect(RewardBindingResultFromHook(RewardHookResult::DeferredBattleActive) ==
               RewardBindingResult::HookDeferred &&
               RewardBindingResultFromHook(RewardHookResult::Conflict) ==
                   RewardBindingResult::Conflict &&
               RewardBindingResultFromHook(RewardHookResult::Failed) ==
                   RewardBindingResult::HookUnavailable,
           "normal battle deferral must project to pending-without-failure, not hook unavailable");
    Expect(IsRewardPriorProtectionAdmitted(true, false) &&
               !IsRewardPriorProtectionAdmitted(false, false) &&
               !IsRewardPriorProtectionAdmitted(true, true) &&
               !IsRewardPriorProtectionAdmitted(false, true),
           "only executable non-writable prior page protection may enter a reward code write");
}

void TestRewardHookCacheProvenanceRecovery() {
    using namespace FfxHooks::F8Runtime;

    FakeRewardHookMemory memory(RewardKind::Ap);
    RewardHookState state{};
    PrepareRewardHookState(RewardKind::Ap, memory.moduleBase, 0x20000000u, &state);
    Expect(InstallRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::Installed,
           "cache-provenance fixture must begin from an installed hook");

    memory.failSiteFlush = true;
    memory.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::RestorePending && state.stubOwned &&
               state.siteMayPointToStub && memory.stubAllocated &&
               RewardEventsEqual(memory, {
                   "read-site", "confirm-battle-inactive", "begin-site", "write-site",
                   "flush-site", "end-site", "read-site"}),
           "failed restoration flush must retain the executable stub and sticky cache provenance");

    memory.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::RestorePending && state.stubOwned &&
               state.siteMayPointToStub && memory.stubAllocated &&
               RewardEventsEqual(memory, {
                   "read-site", "confirm-battle-inactive", "flush-site", "read-site"}),
           "observed-original retry must repeat battle proof, flush, and readback without another code write");

    memory.failSiteFlush = false;
    memory.failSiteReadCall = memory.siteReadCalls + 2;
    memory.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::RestorePending && state.stubOwned &&
               state.siteMayPointToStub && memory.stubAllocated &&
               RewardEventsEqual(memory, {
                   "read-site", "confirm-battle-inactive", "flush-site", "read-site"}),
           "failed final original readback must retain sticky cache provenance and the stub");

    memory.failSiteReadCall = 0;
    memory.events.clear();
    Expect(RemoveRewardHook(MakeRewardHookIo(memory), false, &state) ==
               RewardHookResult::Removed &&
               state.state == RewardHookOwnership::Empty && !state.stubOwned &&
               !state.siteMayPointToStub && !memory.stubAllocated &&
               RewardEventsEqual(memory, {
                   "read-site", "confirm-battle-inactive", "flush-site", "read-site",
                   "free-stub"}),
           "successful exact-original cache proof must precede the eventual stub free");
}

struct FakeRewardBindingIo {
    int32_t scalar = 100;
    uint8_t gate = 0;
    bool scalarWriteSucceeds = true;
    bool scalarReadSucceeds = true;
    bool gateWriteSucceeds = true;
    bool gateWriteMutatesOnFailure = false;
    int gateReadCalls = 0;
    int failGateReadCall = 0;
    int forceGateValueOnReadCall = 0;
    uint8_t forcedGateValue = 0;
    std::vector<std::string> events;
};

bool FakeRewardScalarExchange(void* context, int32_t value) {
    auto& io = *static_cast<FakeRewardBindingIo*>(context);
    io.events.emplace_back("scalar-write");
    if (!io.scalarWriteSucceeds) return false;
    io.scalar = value;
    return true;
}

bool FakeRewardScalarRead(void* context, int32_t* valueOut) {
    auto& io = *static_cast<FakeRewardBindingIo*>(context);
    io.events.emplace_back("scalar-read");
    if (!io.scalarReadSucceeds || !valueOut) return false;
    *valueOut = io.scalar;
    return true;
}

bool FakeRewardGateRead(void* context, uintptr_t, uint8_t* valueOut) {
    auto& io = *static_cast<FakeRewardBindingIo*>(context);
    io.events.emplace_back("gate-read");
    ++io.gateReadCalls;
    if (!valueOut || (io.failGateReadCall != 0 &&
                      io.gateReadCalls == io.failGateReadCall)) return false;
    if (io.forceGateValueOnReadCall != 0 &&
        io.gateReadCalls == io.forceGateValueOnReadCall) {
        io.gate = io.forcedGateValue;
    }
    *valueOut = io.gate;
    return true;
}

FfxHooks::F8Runtime::WriteResult FakeRewardGateWrite(
    void* context, uintptr_t, uint8_t value) {
    auto& io = *static_cast<FakeRewardBindingIo*>(context);
    io.events.emplace_back("gate-write");
    if (!io.gateWriteSucceeds) {
        if (io.gateWriteMutatesOnFailure) io.gate = value;
        return {FfxHooks::F8Runtime::WriteEffect::NotTouched};
    }
    io.gate = value;
    return {FfxHooks::F8Runtime::WriteEffect::Verified};
}

void TestRewardScalarBeforeGateTransaction() {
    using namespace FfxHooks::F8Runtime;
    FakeRewardBindingIo memory;
    RewardScalarIo scalarIo = {&memory, &FakeRewardScalarExchange, &FakeRewardScalarRead};
    ByteIo gateIo = {&memory, &FakeRewardGateRead, &FakeRewardGateWrite};
    OwnedByte gate{};
    int32_t applied = 0;

    Expect(UpdateRewardBinding(
               scalarIo, gateIo, 0x0112A912u, true, true, 25, true, &gate, &applied) ==
               RewardBindingResult::Applied && applied == 25 && memory.scalar == 25 &&
               memory.gate == 1,
           "enabled reward binding must publish the requested scalar and gate");
    const auto scalarWrite = std::find(memory.events.begin(), memory.events.end(), "scalar-write");
    const auto scalarRead = std::find(memory.events.begin(), memory.events.end(), "scalar-read");
    const auto gateWrite = std::find(memory.events.begin(), memory.events.end(), "gate-write");
    Expect(scalarWrite != memory.events.end() && scalarRead != memory.events.end() &&
               gateWrite != memory.events.end() && scalarWrite < scalarRead && scalarRead < gateWrite,
           "exact scalar exchange/readback must precede debug-gate enable");

    memory.events.clear();
    Expect(UpdateRewardBinding(
               scalarIo, gateIo, 0x0112A912u, true, true, 2, true, &gate, &applied) ==
               RewardBindingResult::Applied && applied == 2 && memory.scalar == 2,
           "an enabled 25x to 2x edit must update only aligned scalar data");
    Expect(std::find(memory.events.begin(), memory.events.end(), "gate-write") ==
               memory.events.end(),
           "reconfiguration must not rewrite an already-owned debug gate");

    memory.events.clear();
    Expect(UpdateRewardBinding(
               scalarIo, gateIo, 0x0112A912u, true, true, 2, false, &gate, &applied) ==
               RewardBindingResult::Disabled && memory.gate == 0,
           "disable must restore only the debug gate");
    Expect(std::find(memory.events.begin(), memory.events.end(), "scalar-write") ==
               memory.events.end(),
           "disable must perform no scalar or code rewrite");

    UpdateRewardBinding(
        scalarIo, gateIo, 0x0112A912u, true, true, 25, true, &gate, &applied);
    memory.events.clear();
    Expect(UpdateRewardBinding(
               scalarIo, gateIo, 0x0112A912u, true, false, 0, true, &gate, &applied) ==
               RewardBindingResult::InvalidConfiguration && memory.gate == 0,
           "invalid configuration must fail closed by restoring an owned gate");
    Expect(std::find(memory.events.begin(), memory.events.end(), "scalar-write") ==
               memory.events.end(),
           "invalid configuration must never publish scalar data");

    FakeRewardBindingIo external;
    external.gate = 1;
    RewardScalarIo externalScalar = {
        &external, &FakeRewardScalarExchange, &FakeRewardScalarRead};
    ByteIo externalGate = {&external, &FakeRewardGateRead, &FakeRewardGateWrite};
    OwnedByte externalOwner{};
    Expect(UpdateRewardBinding(
               externalScalar, externalGate, 0x0112A912u, true, true, 25, true,
               &externalOwner, &applied) == RewardBindingResult::Conflict &&
               std::find(external.events.begin(), external.events.end(), "scalar-write") ==
                   external.events.end(),
           "an already-ON unowned debug byte must be external conflict, not adopted ownership");

    external.events.clear();
    Expect(UpdateRewardBinding(
               externalScalar, externalGate, 0x0112A912u, true, false, 0, true,
               &externalOwner, &applied) == RewardBindingResult::Conflict &&
               std::find(external.events.begin(), external.events.end(), "scalar-write") ==
                   external.events.end() &&
               std::find(external.events.begin(), external.events.end(), "gate-write") ==
                   external.events.end(),
           "invalid config must still expose an unowned ON gate as conflict without mutation");

    FakeRewardBindingIo unavailable;
    RewardScalarIo unavailableScalar = {
        &unavailable, &FakeRewardScalarExchange, &FakeRewardScalarRead};
    ByteIo unavailableGate = {&unavailable, &FakeRewardGateRead, &FakeRewardGateWrite};
    OwnedByte unavailableOwner{};
    Expect(UpdateRewardBinding(
               unavailableScalar, unavailableGate, 0x0112A912u, false, true, 25, true,
               &unavailableOwner, &applied) == RewardBindingResult::HookUnavailable &&
               unavailable.events.empty(),
           "a missing inline hook must prevent scalar and gate publication");
}

void TestRewardActiveGateFaultsDisarmFailClosed() {
    using namespace FfxHooks::F8Runtime;
    constexpr uintptr_t gateAddress = 0x0112A912u;

    const auto arm = [gateAddress](FakeRewardBindingIo& memory, OwnedByte& owner) {
        RewardScalarIo scalarIo = {&memory, &FakeRewardScalarExchange, &FakeRewardScalarRead};
        ByteIo gateIo = {&memory, &FakeRewardGateRead, &FakeRewardGateWrite};
        int32_t applied = 0;
        return UpdateRewardBinding(
            scalarIo, gateIo, gateAddress, true, true, 25, true, &owner, &applied);
    };
    const auto reconfigure = [gateAddress](FakeRewardBindingIo& memory, OwnedByte& owner) {
        RewardScalarIo scalarIo = {&memory, &FakeRewardScalarExchange, &FakeRewardScalarRead};
        ByteIo gateIo = {&memory, &FakeRewardGateRead, &FakeRewardGateWrite};
        int32_t applied = 0;
        return UpdateRewardBinding(
            scalarIo, gateIo, gateAddress, true, true, 2, true, &owner, &applied);
    };

    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied, "scalar fault fixture must arm");
        memory.scalarWriteSucceeds = false;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::ScalarWriteFailed &&
                   memory.gate == 0 && !owner.captured,
               "active scalar exchange failure must immediately restore and disarm its owned gate");
    }
    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied, "read fault fixture must arm");
        memory.scalarReadSucceeds = false;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::ScalarReadbackFailed &&
                   memory.gate == 0 && !owner.captured,
               "active scalar readback failure must immediately restore and disarm its owned gate");
    }
    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied, "gate write fixture must arm");
        memory.gate = 0;
        memory.gateWriteSucceeds = false;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::GateFailed &&
                   memory.gate == 0 && !owner.captured,
               "post-exchange gate write failure must converge to proven OFF state");
    }
    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied, "gate read fixture must arm");
        memory.gateReadCalls = 0;
        memory.failGateReadCall = 3;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::GateFailed &&
                   memory.gate == 0 && !owner.captured,
               "post-exchange final gate readback failure must restore the owned gate");
    }
    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied,
               "failed-disarm fixture must arm");
        memory.scalarWriteSucceeds = false;
        memory.gateWriteSucceeds = false;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::GateFailed &&
                   memory.gate == 1 && owner.captured &&
                   owner.state == OwnershipState::RestorePending,
               "an unprovable post-failure disarm must retain a retryable restore obligation");
    }
    {
        FakeRewardBindingIo memory;
        OwnedByte owner{};
        Expect(arm(memory, owner) == RewardBindingResult::Applied, "conflict fixture must arm");
        memory.gate = 2;
        memory.events.clear();
        Expect(reconfigure(memory, owner) == RewardBindingResult::Conflict &&
                   memory.gate == 2 && owner.state == OwnershipState::Conflict &&
                   std::find(memory.events.begin(), memory.events.end(), "gate-write") ==
                       memory.events.end(),
               "an external third gate value must remain untouched and sticky conflict");
    }
}

void TestRewardUnownedGateRaceIsConflict() {
    using namespace FfxHooks::F8Runtime;
    FakeRewardBindingIo memory;
    memory.forceGateValueOnReadCall = 2;
    memory.forcedGateValue = 1;
    RewardScalarIo scalarIo = {&memory, &FakeRewardScalarExchange, &FakeRewardScalarRead};
    ByteIo gateIo = {&memory, &FakeRewardGateRead, &FakeRewardGateWrite};
    OwnedByte owner{};
    int32_t applied = -1;

    Expect(UpdateRewardBinding(
               scalarIo, gateIo, 0x0112A912u, true, true, 25, true, &owner, &applied) ==
               RewardBindingResult::Conflict && !owner.captured && memory.gate == 1 &&
               applied == 0 &&
               std::find(memory.events.begin(), memory.events.end(), "gate-write") ==
                   memory.events.end(),
           "an external 0-to-1 gate race must not be adopted or reported as an applied scalar");
}

struct ScriptedByteIo {
    uint8_t value = 0;
    int readCalls = 0;
    int writeCalls = 0;
    uintptr_t lastReadAddress = 0;
    uintptr_t lastWriteAddress = 0;
    int failReadCall = 0;
    FfxHooks::F8Runtime::WriteEffect writeEffect =
        FfxHooks::F8Runtime::WriteEffect::Verified;
    bool mutateOnWrite = true;
    bool forceAfterWrite = false;
    uint8_t forcedValue = 0;
};

bool ScriptedRead(void* context, uintptr_t address, uint8_t* valueOut) {
    auto& io = *static_cast<ScriptedByteIo*>(context);
    ++io.readCalls;
    io.lastReadAddress = address;
    if (io.failReadCall == io.readCalls) return false;
    *valueOut = io.value;
    return true;
}

FfxHooks::F8Runtime::WriteResult ScriptedWrite(
    void* context, uintptr_t address, uint8_t value) {
    auto& io = *static_cast<ScriptedByteIo*>(context);
    ++io.writeCalls;
    io.lastWriteAddress = address;
    if (io.writeEffect != FfxHooks::F8Runtime::WriteEffect::NotTouched && io.mutateOnWrite) {
        io.value = value;
    }
    if (io.forceAfterWrite) io.value = io.forcedValue;
    return {io.writeEffect};
}

FfxHooks::F8Runtime::ByteIo MakeByteIo(ScriptedByteIo& state) {
    return {&state, &ScriptedRead, &ScriptedWrite};
}

void TestRuntimeCoreOwnedByteFaultMatrix() {
    using namespace FfxHooks::F8Runtime;
    constexpr uintptr_t address = 0x12345678u;

    ScriptedByteIo memory{};
    OwnedByte owned{};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, false, &owned) ==
               ByteTransition::NoChange &&
               memory.readCalls == 0 && memory.writeCalls == 0,
           "disabled unowned byte must perform no I/O");

    memory = {};
    memory.value = 0x7A;
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "first verified write must apply");
    Expect(owned.captured && owned.address == address && owned.original == 0x7A &&
               owned.desired == 1 && owned.state == OwnershipState::Owned && memory.value == 1,
           "apply must retain the nonzero original and verified ownership");
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::NoChange &&
               memory.writeCalls == 1,
           "an owned byte already at the desired value must not be rewritten");
    memory.value = 0x7A;
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Reasserted &&
               memory.value == 1,
           "an owned byte reverted exactly to its original may be reasserted");
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               memory.value == 0x7A && !owned.captured &&
               owned.state == OwnershipState::Unowned,
           "restore must read back the original before clearing ownership");
    const int writesAfterRestore = memory.writeCalls;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::NoChange &&
               memory.writeCalls == writesAfterRestore,
           "repeated restore must be idempotent");

    memory = {};
    memory.value = 0x22;
    memory.failReadCall = 1;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::ReadFailed &&
               !owned.captured && memory.writeCalls == 0,
           "capture read failure must never write or claim ownership");

    memory = {};
    memory.value = 0x22;
    memory.writeEffect = WriteEffect::NotTouched;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::WriteFailed &&
               !owned.captured && owned.state == OwnershipState::Unowned && memory.value == 0x22,
           "proven-not-touched apply failure must release provisional ownership");

    memory = {};
    memory.value = 0x22;
    memory.writeEffect = WriteEffect::MayHaveChanged;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied &&
               owned.captured && owned.state == OwnershipState::Owned && memory.value == 1,
           "possibly changed apply becomes owned only after desired readback");

    memory = {};
    memory.value = 0x22;
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = 2;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::ReadbackFailed &&
               owned.captured && owned.state == OwnershipState::RestorePending,
           "failed apply readback must retain recoverable RestorePending ownership");

    memory = {};
    memory.value = 0x22;
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.forceAfterWrite = true;
    memory.forcedValue = 0x44;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Conflict &&
               owned.captured && owned.state == OwnershipState::Conflict,
           "observed third-party apply readback must enter sticky Conflict");
    const int writesBeforeConflict = memory.writeCalls;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Conflict &&
               owned.state == OwnershipState::Conflict &&
               memory.writeCalls == writesBeforeConflict && memory.value == 0x44,
           "restore must never overwrite a third-party value");

    memory = {};
    memory.value = 0x31;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "restore-fault setup must acquire ownership");
    memory.writeEffect = WriteEffect::NotTouched;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::WriteFailed &&
               owned.captured && owned.state == OwnershipState::RestorePending &&
               memory.value == 1,
           "failed restore write must retain RestorePending ownership");
    memory.writeEffect = WriteEffect::Verified;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               memory.value == 0x31 && !owned.captured,
           "a later restore attempt must recover a retained write failure");

    memory = {};
    memory.value = 0x41;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "restore read-fault setup must acquire ownership");
    memory.failReadCall = memory.readCalls + 1;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::RestorePending &&
               owned.captured && owned.state == OwnershipState::RestorePending,
           "failed restore read must retain RestorePending ownership");

    memory = {};
    memory.value = 0x51;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "already-original restore setup must acquire ownership");
    memory.value = 0x51;
    const int writesBeforeOriginal = memory.writeCalls;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               !owned.captured && memory.writeCalls == writesBeforeOriginal,
           "current already original must clear ownership without another write");

    memory = {};
    memory.value = 0x61;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "restore-readback fault setup must acquire ownership");
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = memory.readCalls + 2;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::ReadbackFailed &&
               owned.captured && owned.state == OwnershipState::RestorePending,
           "possibly changed restore with failed readback must retain RestorePending ownership");
    memory.failReadCall = 0;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               !owned.captured && memory.value == 0x61,
           "readback-failed restore must clear only after a later read proves the original");

    memory = {};
    memory.value = 0x71;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::Applied,
           "desired-original transition setup must acquire ownership");
    memory.value = 0x71;
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 0x71, true, &owned) ==
               ByteTransition::Restored &&
               !owned.captured && owned.state == OwnershipState::Unowned,
           "an owned byte proven original and requested original must clear immediately");

    memory = {};
    memory.value = 1;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), address, 1, true, &owned) ==
               ByteTransition::NoChange &&
               !owned.captured && memory.writeCalls == 0,
           "an unowned byte already desired must not be captured or later restored");
}

void TestRuntimeCoreOwnedByteChangedDesiredAndStickyConflict() {
    using namespace FfxHooks::F8Runtime;
    constexpr uintptr_t addressA = 0x12345000u;
    constexpr uintptr_t addressB = 0x12346000u;
    constexpr uint8_t original = 0x40;
    constexpr uint8_t desired1 = 0x11;
    constexpr uint8_t desired2 = 0x22;
    constexpr uint8_t desired3 = 0x33;

    ScriptedByteIo memory{};
    memory.value = original;
    OwnedByte owned{};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "D1-to-D2 success setup must acquire D1");
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::Reasserted &&
               owned.state == OwnershipState::Owned && owned.desired == desired2 &&
               !owned.hasPriorDesired && memory.value == desired2,
           "D1-to-D2 verified success must commit only D2 as the owned candidate");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "NotTouched rollback setup must acquire D1");
    memory.writeEffect = WriteEffect::NotTouched;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::WriteFailed &&
               owned.state == OwnershipState::Owned && owned.desired == desired1 &&
               !owned.hasPriorDesired && memory.value == desired1,
           "D1-to-D2 NotTouched must roll metadata back to the proven D1 owner");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "D1-candidate restore setup must acquire D1");
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = memory.readCalls + 2;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::ReadbackFailed &&
               owned.state == OwnershipState::RestorePending &&
               owned.desired == desired2 && owned.hasPriorDesired &&
               owned.priorDesired == desired1,
           "failed D1-to-D2 readback must retain both proven module-owned candidates");
    const int writesBeforeUnresolvedD3 = memory.writeCalls;
    memory.failReadCall = memory.readCalls + 1;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired3, true, &owned) ==
               ByteTransition::ReadFailed &&
               memory.writeCalls == writesBeforeUnresolvedD3 && owned.hasPriorDesired &&
               owned.desired == desired2 && owned.priorDesired == desired1,
           "D3 must fail closed while the D1/D2 candidate set cannot be resolved");
    memory.failReadCall = 0;
    memory.value = desired1;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               memory.value == original && !owned.captured &&
               memory.lastWriteAddress == addressA,
           "restore may safely recover original when unresolved memory is D1");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "D2-candidate restore setup must acquire D1");
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = memory.readCalls + 2;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::ReadbackFailed && owned.hasPriorDesired,
           "D2-candidate restore setup must retain D1 and D2");
    memory.failReadCall = 0;
    memory.value = desired2;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               memory.value == original && !owned.captured,
           "restore may safely recover original when unresolved memory is D2");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "D1-to-original setup must acquire D1");
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, original, true, &owned) ==
               ByteTransition::Restored &&
               memory.value == original && !owned.captured &&
               owned.state == OwnershipState::Unowned,
           "D1-to-original must clear ownership as Restored, never remain Owned");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "pending-to-original setup must acquire D1");
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = memory.readCalls + 2;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::ReadbackFailed && owned.hasPriorDesired &&
               owned.state == OwnershipState::RestorePending,
           "pending-to-original setup must retain an unresolved D1/D2 transition");
    memory.failReadCall = 0;
    memory.value = original;
    const int writesBeforePendingOriginal = memory.writeCalls;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, original, true, &owned) ==
               ByteTransition::Restored &&
               !owned.captured && owned.state == OwnershipState::Unowned &&
               memory.writeCalls == writesBeforePendingOriginal,
           "pending ambiguity resolved to original must clear as Restored without a write");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "sticky conflict setup must acquire D1");
    memory.writeEffect = WriteEffect::MayHaveChanged;
    memory.failReadCall = memory.readCalls + 2;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired2, true, &owned) ==
               ByteTransition::ReadbackFailed && owned.hasPriorDesired,
           "sticky conflict setup must retain both candidates");
    memory.failReadCall = 0;
    memory.value = 0x7E;
    const int writesBeforeConflict = memory.writeCalls;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Conflict &&
               owned.state == OwnershipState::Conflict &&
               memory.writeCalls == writesBeforeConflict,
           "a third value must enter Conflict without a write");
    memory.value = desired1;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Conflict &&
               owned.state == OwnershipState::Conflict &&
               memory.writeCalls == writesBeforeConflict,
           "sticky Conflict must never authorize a write when memory later equals D1");
    memory.value = desired2;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Conflict &&
               owned.state == OwnershipState::Conflict &&
               memory.writeCalls == writesBeforeConflict,
           "sticky Conflict must never authorize a write when memory later equals D2");
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Conflict &&
               memory.writeCalls == writesBeforeConflict,
           "repeated sticky Conflict retries must remain write-free");
    memory.value = original;
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               !owned.captured && owned.state == OwnershipState::Unowned &&
               memory.writeCalls == writesBeforeConflict,
           "only readback of original may clear sticky Conflict");

    memory = {};
    memory.value = original;
    owned = {};
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressA, desired1, true, &owned) ==
               ByteTransition::Applied,
           "address mismatch setup must acquire address A");
    const int readsBeforeMismatch = memory.readCalls;
    const int writesBeforeMismatch = memory.writeCalls;
    Expect(UpdateOwnedByte(MakeByteIo(memory), addressB, desired2, true, &owned) ==
               ByteTransition::Conflict &&
               owned.state == OwnershipState::Owned && owned.address == addressA &&
               owned.desired == desired1 && memory.readCalls == readsBeforeMismatch &&
               memory.writeCalls == writesBeforeMismatch,
           "address mismatch must reject B without poisoning the existing A owner");
    Expect(RestoreOwnedByte(MakeByteIo(memory), &owned) == ByteTransition::Restored &&
               memory.value == original && !owned.captured &&
               memory.lastReadAddress == addressA && memory.lastWriteAddress == addressA,
           "address A must remain safely restorable after a mismatched B request");
}

constexpr uintptr_t kRt0SeymourSite1 = 0x004A8F47u;
constexpr uintptr_t kRt0SeymourSite2 = 0x004A8F9Au;
constexpr uintptr_t kRt0SeymourPage = 0x004A8000u;
constexpr uintptr_t kRt0SeymourParty = 0x00D32494u;
constexpr size_t kRt0SeymourSpanLength = 0x57u;
constexpr uint8_t kRt0SeymourMask = 0x11u;
constexpr std::array<uint8_t, 4> kRt0SeymourSignature1 = {0x3Cu, 0x07u, 0x74u, 0x24u};
constexpr std::array<uint8_t, 4> kRt0SeymourSignature2 = {0x3Cu, 0x07u, 0x74u, 0x1Eu};
constexpr std::array<uint8_t, 4> kRt0SeymourNops = {0x90u, 0x90u, 0x90u, 0x90u};

enum class PatchEventKind : uint8_t { Read = 0, Begin, Write, Flush, End };
struct PatchEvent {
    PatchEventKind kind = PatchEventKind::Read;
    uintptr_t address = 0;
    size_t length = 0;
    std::array<uint8_t, 4> bytes{};
};
struct PatchReadFault {
    uintptr_t address = 0;
    int occurrence = 0;
};
struct PatchPartyReadMutation {
    int occurrence = 0;
    uint8_t value = 0;
};
struct PatchWriteDirective {
    int call = 0;
    FfxHooks::F8Runtime::MutationReport report{
        FfxHooks::F8Runtime::MutationEffect::Verified, true};
    bool mutate = true;
    size_t forcedLength = 0;
    std::array<uint8_t, 4> forcedBytes{};
    size_t beforeLength = 0;
    std::array<uint8_t, 4> beforeBytes{};
    bool reportOnCompareMismatch = false;
};
struct PatchEndDirective {
    int call = 0;
    bool result = true;
    bool replaceToken = false;
    FfxHooks::F8Runtime::PatchProtectionToken token{};
};

struct ScriptedPatchIo {
    std::array<uint8_t, 0x3000> code{};
    uint8_t party = 0;
    uintptr_t partyAddress = kRt0SeymourParty;
    std::vector<PatchEvent> events;
    std::vector<PatchReadFault> readFaults;
    std::vector<PatchPartyReadMutation> partyReadMutations;
    std::vector<PatchWriteDirective> writeDirectives;
    std::vector<PatchEndDirective> endDirectives;
    std::vector<int> failedBeginCalls;
    std::vector<int> failedFlushCalls;
    std::vector<int> failedEndCalls;
    bool failedBeginLeavesActive = false;
    int site1ReadCalls = 0;
    int site2ReadCalls = 0;
    int partyReadCalls = 0;
    int beginCalls = 0;
    int writeCalls = 0;
    int flushCalls = 0;
    int endCalls = 0;

    explicit ScriptedPatchIo(uint8_t initialParty = 0xA4u) : party(initialParty) {
        SetCode(kRt0SeymourSite1, kRt0SeymourSignature1);
        SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
    }

    void ClearScript() {
        events.clear();
        readFaults.clear();
        partyReadMutations.clear();
        writeDirectives.clear();
        endDirectives.clear();
        failedBeginCalls.clear();
        failedFlushCalls.clear();
        failedEndCalls.clear();
        failedBeginLeavesActive = false;
        site1ReadCalls = 0;
        site2ReadCalls = 0;
        partyReadCalls = 0;
        beginCalls = 0;
        writeCalls = 0;
        flushCalls = 0;
        endCalls = 0;
    }

    bool IsCodeRange(uintptr_t address, size_t length) const {
        if (address < kRt0SeymourPage) return false;
        const uintptr_t offset = address - kRt0SeymourPage;
        return offset <= code.size() && length <= code.size() - static_cast<size_t>(offset);
    }

    std::array<uint8_t, 4> CodeAt(uintptr_t address) const {
        std::array<uint8_t, 4> result{};
        if (!IsCodeRange(address, result.size())) return result;
        memcpy(result.data(), code.data() + (address - kRt0SeymourPage), result.size());
        return result;
    }

    void SetCode(uintptr_t address, const std::array<uint8_t, 4>& value) {
        if (!IsCodeRange(address, value.size())) return;
        memcpy(code.data() + (address - kRt0SeymourPage), value.data(), value.size());
    }
};

bool ContainsCall(const std::vector<int>& calls, int call) {
    for (const int candidate : calls) {
        if (candidate == call) return true;
    }
    return false;
}

int IncrementPatchReadOccurrence(ScriptedPatchIo& memory, uintptr_t address) {
    if (address == kRt0SeymourSite1) return ++memory.site1ReadCalls;
    if (address == kRt0SeymourSite2) return ++memory.site2ReadCalls;
    if (address == memory.partyAddress) return ++memory.partyReadCalls;
    return 0;
}

bool ShouldFailPatchRead(const ScriptedPatchIo& memory, uintptr_t address, int occurrence) {
    for (const PatchReadFault& fault : memory.readFaults) {
        if (fault.address == address && fault.occurrence == occurrence) return true;
    }
    return false;
}

bool ScriptedPatchRead(void* context, uintptr_t address, uint8_t* bytes, size_t length) {
    if (!context || !bytes || length == 0) return false;
    auto& memory = *static_cast<ScriptedPatchIo*>(context);
    memory.events.push_back({PatchEventKind::Read, address, length, {}});
    const int occurrence = IncrementPatchReadOccurrence(memory, address);
    if (ShouldFailPatchRead(memory, address, occurrence)) return false;
    if (address == memory.partyAddress && length == 1) {
        for (const PatchPartyReadMutation& mutation : memory.partyReadMutations) {
            if (mutation.occurrence == occurrence) memory.party = mutation.value;
        }
        bytes[0] = memory.party;
        return true;
    }
    if (!memory.IsCodeRange(address, length)) return false;
    memcpy(bytes, memory.code.data() + (address - kRt0SeymourPage), length);
    return true;
}

bool ScriptedBeginCodeWrite(void* context, uintptr_t address, size_t length,
                            FfxHooks::F8Runtime::PatchProtectionToken* token) {
    if (!context || !token) return false;
    auto& memory = *static_cast<ScriptedPatchIo*>(context);
    ++memory.beginCalls;
    memory.events.push_back({PatchEventKind::Begin, address, length, {}});
    const bool fail = ContainsCall(memory.failedBeginCalls, memory.beginCalls);
    if (!fail || memory.failedBeginLeavesActive) {
        token->address = address;
        token->length = length;
        token->originalProtection = 0x20u;
        token->active = true;
    }
    return !fail;
}

const PatchWriteDirective* FindWriteDirective(const ScriptedPatchIo& memory, int call) {
    for (const PatchWriteDirective& directive : memory.writeDirectives) {
        if (directive.call == call) return &directive;
    }
    return nullptr;
}

void ForcePatchBytes(ScriptedPatchIo& memory, uintptr_t address,
                     const std::array<uint8_t, 4>& bytes, size_t length) {
    if (address == memory.partyAddress && length == 1) {
        memory.party = bytes[0];
    } else if (memory.IsCodeRange(address, length)) {
        memcpy(memory.code.data() + (address - kRt0SeymourPage), bytes.data(), length);
    }
}

bool PatchBytesEqual(const ScriptedPatchIo& memory, uintptr_t address,
                     const uint8_t* expected, size_t length) {
    if (address == memory.partyAddress && length == 1) {
        return memory.party == expected[0];
    }
    return memory.IsCodeRange(address, length) &&
           memcmp(memory.code.data() + (address - kRt0SeymourPage), expected, length) == 0;
}

FfxHooks::F8Runtime::MutationReport ScriptedPatchWriteIfEqual(
    void* context, uintptr_t address, const uint8_t* expected,
    const uint8_t* desired, size_t length) {
    using namespace FfxHooks::F8Runtime;
    if (!context || !expected || !desired || length == 0 || length > 4) {
        return {MutationEffect::NotTouched, false};
    }
    auto& memory = *static_cast<ScriptedPatchIo*>(context);
    ++memory.writeCalls;
    PatchEvent event{PatchEventKind::Write, address, length, {}};
    memcpy(event.bytes.data(), desired, length);
    memory.events.push_back(event);

    const PatchWriteDirective* directive = FindWriteDirective(memory, memory.writeCalls);
    if (directive && directive->beforeLength != 0) {
        ForcePatchBytes(memory, address, directive->beforeBytes, directive->beforeLength);
    }
    if (!PatchBytesEqual(memory, address, expected, length)) {
        if (directive && directive->forcedLength != 0) {
            // Deterministic external ABA seam: the compared value may reappear before core readback.
            ForcePatchBytes(memory, address, directive->forcedBytes,
                            directive->forcedLength);
        }
        return directive && directive->reportOnCompareMismatch
            ? directive->report
            : MutationReport{MutationEffect::NotTouched, false};
    }
    const MutationReport report = directive
        ? directive->report
        : MutationReport{MutationEffect::Verified, true};
    const bool mutate = report.effect != MutationEffect::NotTouched &&
                        (!directive || directive->mutate);
    if (mutate) {
        if (address == memory.partyAddress && length == 1) {
            memory.party = desired[0];
        } else if (memory.IsCodeRange(address, length)) {
            memcpy(memory.code.data() + (address - kRt0SeymourPage), desired, length);
        }
    }
    if (directive && directive->forcedLength != 0) {
        ForcePatchBytes(memory, address, directive->forcedBytes, directive->forcedLength);
    }
    return report;
}

bool ScriptedPatchFlush(void* context, uintptr_t address, size_t length) {
    if (!context) return false;
    auto& memory = *static_cast<ScriptedPatchIo*>(context);
    ++memory.flushCalls;
    memory.events.push_back({PatchEventKind::Flush, address, length, {}});
    return !ContainsCall(memory.failedFlushCalls, memory.flushCalls);
}

bool ScriptedEndCodeWrite(void* context,
                          FfxHooks::F8Runtime::PatchProtectionToken* token) {
    if (!context || !token) return false;
    auto& memory = *static_cast<ScriptedPatchIo*>(context);
    ++memory.endCalls;
    memory.events.push_back({PatchEventKind::End, token->address, token->length, {}});
    for (const PatchEndDirective& directive : memory.endDirectives) {
        if (directive.call != memory.endCalls) continue;
        if (directive.replaceToken) *token = directive.token;
        return directive.result;
    }
    if (ContainsCall(memory.failedEndCalls, memory.endCalls)) return false;
    token->active = false;
    return true;
}

FfxHooks::F8Runtime::PatchIo MakePatchIo(ScriptedPatchIo& memory) {
    return {&memory, &ScriptedPatchRead, &ScriptedBeginCodeWrite, &ScriptedPatchWriteIfEqual,
            &ScriptedPatchFlush, &ScriptedEndCodeWrite};
}

FfxHooks::F8Runtime::SeymourBundleState MakeSeymourState(uintptr_t imageBase = 0) {
    using namespace FfxHooks::F8Runtime;
    SeymourBundleState state{};
    state.sites[0].address = imageBase + kRt0SeymourSite1;
    state.sites[1].address = imageBase + kRt0SeymourSite2;
    state.party.address = imageBase + kRt0SeymourParty;
    state.party.mask = kRt0SeymourMask;
    return state;
}

void ConfigureShiftedSeymourMemory(ScriptedPatchIo& memory, uintptr_t imageBase) {
    memory.SetCode(imageBase + kRt0SeymourSite1, kRt0SeymourSignature1);
    memory.SetCode(imageBase + kRt0SeymourSite2, kRt0SeymourSignature2);
    memory.partyAddress = imageBase + kRt0SeymourParty;
}

bool HasExactProtectionSnapshot(
    const FfxHooks::F8Runtime::PatchProtectionToken& token) {
    return token.address == kRt0SeymourSite1 &&
           token.length == kRt0SeymourSpanLength &&
           token.originalProtection == 0x20u && token.active;
}

FfxHooks::F8Runtime::SeymourBundleState MakePendingOriginalSeymourState() {
    using namespace FfxHooks::F8Runtime;
    SeymourBundleState state = MakeSeymourState();
    state.state = BundleState::RestorePending;
    state.sites[0].original = kRt0SeymourSignature1;
    state.sites[1].original = kRt0SeymourSignature2;
    state.sites[0].possiblyOwned = true;
    state.sites[1].possiblyOwned = true;
    state.party.originalMaskedBits = 0;
    state.party.possiblyOwned = true;
    state.protection = {
        kRt0SeymourSite1, kRt0SeymourSpanLength, 0x20u, true};
    return state;
}

size_t CountPatchEvents(const ScriptedPatchIo& memory, PatchEventKind kind) {
    size_t count = 0;
    for (const PatchEvent& event : memory.events) {
        if (event.kind == kind) ++count;
    }
    return count;
}

size_t FindPatchEvent(const ScriptedPatchIo& memory, PatchEventKind kind,
                      uintptr_t address, size_t start = 0) {
    for (size_t index = start; index < memory.events.size(); ++index) {
        const PatchEvent& event = memory.events[index];
        if (event.kind == kind && event.address == address) return index;
    }
    return std::string::npos;
}

bool PatchWritesMatch(const ScriptedPatchIo& memory,
                      std::initializer_list<uintptr_t> expected) {
    auto wanted = expected.begin();
    for (const PatchEvent& event : memory.events) {
        if (event.kind != PatchEventKind::Write) continue;
        if (wanted == expected.end() || event.address != *wanted) return false;
        ++wanted;
    }
    return wanted == expected.end();
}

bool AllSpanEventsExact(const ScriptedPatchIo& memory, PatchEventKind kind) {
    bool found = false;
    for (const PatchEvent& event : memory.events) {
        if (event.kind != kind) continue;
        found = true;
        if (event.address != kRt0SeymourSite1 || event.length != kRt0SeymourSpanLength) {
            return false;
        }
    }
    return found;
}

bool SeymourOwnershipClear(const FfxHooks::F8Runtime::SeymourBundleState& state) {
    using FfxHooks::F8Runtime::ResourceRestorePhase;
    return !state.sites[0].possiblyOwned && !state.sites[1].possiblyOwned &&
           !state.sites[0].conflictWitness && !state.sites[1].conflictWitness &&
           !state.party.possiblyOwned && !state.party.conflictWitness &&
           state.sites[0].restorePhase == ResourceRestorePhase::None &&
           state.sites[1].restorePhase == ResourceRestorePhase::None &&
           state.party.restorePhase == ResourceRestorePhase::None &&
           !state.protection.active;
}

bool SeymourMemoryIsOriginal(const ScriptedPatchIo& memory, uint8_t expectedParty) {
    return memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
           memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2 &&
           memory.party == expectedParty;
}

bool FirstThreeEventsArePreflight(const ScriptedPatchIo& memory) {
    if (memory.events.size() < 3) return false;
    const PatchEvent& site1 = memory.events[0];
    const PatchEvent& site2 = memory.events[1];
    const PatchEvent& party = memory.events[2];
    return site1.kind == PatchEventKind::Read && site1.address == kRt0SeymourSite1 &&
           site1.length == 4 && site2.kind == PatchEventKind::Read &&
           site2.address == kRt0SeymourSite2 && site2.length == 4 &&
           party.kind == PatchEventKind::Read && party.address == kRt0SeymourParty &&
           party.length == 1;
}

void TestSeymourAddressLedgerAndRoundTrip() {
    using namespace FfxHooks::F8Runtime;
    Expect(RVA_FFX_SEYMOUR_PATCH_SITE1 == kRt0SeymourSite1,
           "Seymour site1 RVA must be 0x004A8F47");
    Expect(RVA_FFX_SEYMOUR_PATCH_SITE2 == kRt0SeymourSite2,
           "Seymour site2 RVA must be 0x004A8F9A");
    Expect(RVA_FFX_SEYMOUR_PATCH_PAGE == kRt0SeymourPage,
           "Seymour containing page RVA must be 0x004A8000");
    Expect(FFX_SEYMOUR_PATCH_SPAN_LENGTH == kRt0SeymourSpanLength,
           "Seymour combined code span must be 0x57 bytes");
    Expect(RVA_FFX_PARTY_SEYMOUR_IN_PARTY == kRt0SeymourParty,
           "Seymour party byte RVA must be 0x00D32494");
    Expect(FFX_PARTY_SEYMOUR_MASK == kRt0SeymourMask,
           "Seymour party ownership mask must be 0x11");
    Expect((kRt0SeymourSite1 & ~static_cast<uintptr_t>(0xFFFu)) == kRt0SeymourPage &&
               (kRt0SeymourSite2 & ~static_cast<uintptr_t>(0xFFFu)) == kRt0SeymourPage,
           "both Seymour signatures must share only the asserted 4 KiB page");
    Expect(kRt0SeymourSite2 - kRt0SeymourSite1 + kRt0SeymourSignature2.size() ==
               kRt0SeymourSpanLength,
           "Seymour span must start at site1 and include all four bytes of site2");

    ScriptedPatchIo memory(0xA4u);
    SeymourBundleState state = MakeSeymourState();
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
           "verified Seymour apply must become Active");
    Expect(state.state == BundleState::Active && state.sites[0].possiblyOwned &&
               state.sites[1].possiblyOwned && state.party.possiblyOwned &&
               state.sites[0].original == kRt0SeymourSignature1 &&
               state.sites[1].original == kRt0SeymourSignature2 &&
               state.party.originalMaskedBits == 0,
           "apply must capture both signatures and only the original controlled party bits");
    Expect(memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourNops &&
               memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops &&
               memory.party == 0xB5u,
           "apply must write two four-NOP sites then preserve non-party bits while setting 0x11");
    Expect(FirstThreeEventsArePreflight(memory),
           "both signatures and party must be read before protection or any write");
    Expect(PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                      kRt0SeymourParty}),
           "apply write order must be site1, site2, then party");
    Expect(memory.beginCalls == 1 && memory.flushCalls == 1 && memory.endCalls == 1 &&
               AllSpanEventsExact(memory, PatchEventKind::Begin) &&
               AllSpanEventsExact(memory, PatchEventKind::Flush),
           "apply must use one exact site1-plus-0x57 protection and flush span");
    const size_t site2Write = FindPatchEvent(memory, PatchEventKind::Write, kRt0SeymourSite2);
    const size_t flush = FindPatchEvent(memory, PatchEventKind::Flush, kRt0SeymourSite1);
    const size_t end = FindPatchEvent(memory, PatchEventKind::End, kRt0SeymourSite1);
    const size_t partyWrite = FindPatchEvent(memory, PatchEventKind::Write, kRt0SeymourParty);
    Expect(site2Write < flush && flush < end && end < partyWrite,
           "code readback/flush/protection restoration must finish before the party write");

    memory.ClearScript();
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::AlreadyInState &&
               CountPatchEvents(memory, PatchEventKind::Write) == 0,
           "repeated enabled calls must verify rather than reassert an Active bundle");

    memory.party = 0x7Bu;
    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored,
           "verified Seymour restore must return Restored");
    Expect(state.state == BundleState::Inactive && SeymourOwnershipClear(state) &&
               SeymourMemoryIsOriginal(memory, 0x6Au),
           "restore must preserve current non-owned party bits and clear ownership only after proof");
    Expect(PatchWritesMatch(memory, {kRt0SeymourParty, kRt0SeymourSite2,
                                      kRt0SeymourSite1}),
           "restore write order must be party, site2, then site1");
    Expect(memory.beginCalls == 1 && memory.flushCalls == 1 && memory.endCalls == 1 &&
               AllSpanEventsExact(memory, PatchEventKind::Begin) &&
               AllSpanEventsExact(memory, PatchEventKind::Flush),
           "restore must use one exact combined code protection and flush span");

    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::AlreadyInState && memory.events.empty(),
           "repeated disable/stop after confirmed restoration must perform no I/O");
}

void TestSeymourPreflightAndInvalidInputs() {
    using namespace FfxHooks::F8Runtime;
    const std::array<uintptr_t, 3> addresses = {
        kRt0SeymourSite1, kRt0SeymourSite2, kRt0SeymourParty};
    for (const uintptr_t address : addresses) {
        ScriptedPatchIo memory{};
        memory.readFaults.push_back({address, 1});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed &&
                   CountPatchEvents(memory, PatchEventKind::Begin) == 0 &&
                   CountPatchEvents(memory, PatchEventKind::Write) == 0 &&
                   CountPatchEvents(memory, PatchEventKind::Flush) == 0 &&
                   state.state == BundleState::Inactive && SeymourOwnershipClear(state),
               "each preflight read failure must perform zero mutation operations or ownership");
    }

    for (const uintptr_t address : {kRt0SeymourSite1, kRt0SeymourSite2}) {
        ScriptedPatchIo memory{};
        std::array<uint8_t, 4> mismatch = memory.CodeAt(address);
        mismatch[2] ^= 0x01u;
        memory.SetCode(address, mismatch);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::SignatureMismatch && FirstThreeEventsArePreflight(memory) &&
                   CountPatchEvents(memory, PatchEventKind::Begin) == 0 &&
                   CountPatchEvents(memory, PatchEventKind::Write) == 0 &&
                   CountPatchEvents(memory, PatchEventKind::Flush) == 0 &&
                   state.state == BundleState::Inactive && SeymourOwnershipClear(state),
               "either exact signature mismatch must read the full preflight and perform zero mutations");
    }

    ScriptedPatchIo memory{};
    SeymourBundleState state = MakeSeymourState();
    PatchIo invalid = MakePatchIo(memory);
    invalid.context = nullptr;
    Expect(ApplySeymourBundle(invalid, &state) == BundleResult::PreflightFailed &&
               memory.events.empty(),
           "null PatchIo context must fail closed without a callback");

    invalid = MakePatchIo(memory);
    invalid.writeIfEqual = nullptr;
    Expect(ApplySeymourBundle(invalid, &state) == BundleResult::PreflightFailed &&
               memory.events.empty(),
           "missing PatchIo callback must fail closed before preflight");
    Expect(ApplySeymourBundle(MakePatchIo(memory), nullptr) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "null Seymour state must fail closed before a callback");

    state = MakeSeymourState();
    state.sites[1].address = state.sites[0].address + 1;
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "noncanonical site spacing must reject a page-base-plus-short-span mistake");
    state = MakeSeymourState();
    state.party.mask = 0x10u;
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "a non-0x11 controlled mask must fail closed");
    state = MakeSeymourState();
    state.sites[0].address = 0x00ABCFF0u;
    state.sites[1].address = state.sites[0].address +
                             (kRt0SeymourSite2 - kRt0SeymourSite1);
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "a Seymour span crossing its 4 KiB code page must fail closed");
    state = MakeSeymourState();
    state.sites[0].address = (std::numeric_limits<uintptr_t>::max)() -
                             (kRt0SeymourSite2 - kRt0SeymourSite1);
    state.sites[1].address = (std::numeric_limits<uintptr_t>::max)();
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "an address whose exact 0x57 span overflows uintptr_t must fail closed");
    state = MakeSeymourState();
    state.party.address = state.sites[0].address;
    Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
               BundleResult::PreflightFailed && memory.events.empty(),
           "party address overlapping the code span must fail closed");
}

void TestSeymourProductionInertSourceContracts() {
    std::string boosterSource;
    std::string boosterHeader;
    std::string coreSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), boosterSource) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.h"), boosterHeader) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\F8RuntimeCore.cpp"), coreSource),
           "legacy UnX and portable F8 sources must be readable for Seymour containment proof");
    const std::string legacySurfaces = boosterSource + boosterHeader;
    const char* forbiddenLegacyTokens[] = {
        "PatchAntiSeymour", "ApplyPlayableSeymour", "UnXBoosterMenuSafePointTick",
        "ApplySeymourBundle", "RestoreSeymourBundle", "boosters.playable_seymour",
    };
    for (const char* token : forbiddenLegacyTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "legacy UnX Seymour surfaces must remain retired: %s", token);
        ExpectSourceExcludes(legacySurfaces, token, message);
    }
    const char* forbiddenCoreTokens[] = {
        "VirtualProtect", "FlushInstructionCache", "GetModuleHandle", "GetEnvironmentVariable",
        "std::getenv", "windows.h", "MenuSafePoint", "FrameTick",
    };
    for (const char* token : forbiddenCoreTokens) {
        char message[192] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "portable Seymour core must exclude Win32/invoker token %s", token);
        ExpectSourceExcludes(coreSource, token, message);
    }
}

void TestSeymourApplyFaultMatrix() {
    using namespace FfxHooks::F8Runtime;

    for (const bool leavesProtectionActive : {false, true}) {
        ScriptedPatchIo memory{};
        memory.failedBeginCalls.push_back(1);
        memory.failedBeginLeavesActive = leavesProtectionActive;
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   memory.writeCalls == 0 && memory.flushCalls == 0 &&
                   memory.endCalls == (leavesProtectionActive ? 1 : 0),
               "begin failure before site1 must perform zero writes and close any exposed token");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {1, {MutationEffect::NotTouched, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   state.state == BundleState::Inactive &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1}),
               "site1 NotTouched must fail without ownership and restore the inactive baseline");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {2, {MutationEffect::NotTouched, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourSite1}),
               "site2 NotTouched must roll verified site1 back and clear all ownership");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {1, {MutationEffect::Verified, false}, true, 0, {}});
        memory.writeDirectives.push_back(
            {2, {MutationEffect::MayHaveChanged, false}, true, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied &&
                   state.state == BundleState::Active,
               "independent readback must resolve Verified and MayHaveChanged callback ambiguity");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourMemoryIsOriginal(memory, 0xA4u),
               "resolved callback ambiguity must remain fully reversible");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {1, {MutationEffect::MayHaveChanged, false}, true, 0, {}});
        memory.readFaults.push_back({kRt0SeymourSite1, 2});
        memory.readFaults.push_back({kRt0SeymourSite1, 3});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[0].possiblyOwned &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourNops,
               "MayHaveChanged plus unreadable apply/rollback must retain site1 ownership");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state),
               "a later restore must recover an ambiguously written site1");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {2, {MutationEffect::MayHaveChanged, false}, true, 0, {}});
        memory.readFaults.push_back({kRt0SeymourSite2, 2});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourSite2, kRt0SeymourSite1}),
               "MayHaveChanged site2 with unreadable apply readback must resolve by reverse rollback");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {1, {MutationEffect::MayHaveChanged, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   memory.flushCalls == 1 && memory.endCalls == 1,
               "MayHaveChanged without a mutation must release ownership only after original readback");
    }

    {
        const std::array<uint8_t, 4> third = {0xDEu, 0xADu, 0xBEu, 0xEFu};
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {1, {MutationEffect::Verified, true}, true, third.size(), third});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && state.sites[0].possiblyOwned &&
                   memory.CodeAt(kRt0SeymourSite1) == third && memory.writeCalls == 1,
               "optimistic callback readback claim must not hide divergent site1 Conflict");
        memory.SetCode(kRt0SeymourSite1, kRt0SeymourSignature1);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only later observation of original site1 may clear its Conflict");
    }

    {
        const std::array<uint8_t, 4> third = {0xCCu, 0xCCu, 0xCCu, 0xCCu};
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {2, {MutationEffect::MayHaveChanged, false}, true, third.size(), third});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == third &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourSite1}),
               "divergent site2 must be preserved while site1 rolls back in reverse order");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored,
               "only later observation of original site2 may clear its Conflict");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls.push_back(1);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourSite2, kRt0SeymourSite1}) &&
                   memory.flushCalls == 2 && AllSpanEventsExact(memory, PatchEventKind::Flush),
               "combined apply flush failure must reverse both sites and flush the rollback");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedEndCalls.push_back(1);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourSite2, kRt0SeymourSite1}) &&
                   memory.flushCalls == 2 && memory.endCalls == 2,
               "failed original-protection restoration must roll back under the retained token");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {3, {MutationEffect::NotTouched, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourParty, kRt0SeymourSite2,
                                              kRt0SeymourSite1}) &&
                   memory.beginCalls == 2 && memory.flushCalls == 2 && memory.endCalls == 2,
               "party NotTouched after both NOPs must reopen one guard and roll code back");
    }

    {
        ScriptedPatchIo memory{};
        memory.readFaults.push_back({kRt0SeymourParty, 3});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourParty, kRt0SeymourParty,
                                              kRt0SeymourSite2, kRt0SeymourSite1}),
               "unreadable party write must roll party, site2, and site1 back in reverse order");
    }

    {
        ScriptedPatchIo memory{};
        const std::array<uint8_t, 4> divergentParty = {0xB4u, 0, 0, 0};
        memory.writeDirectives.push_back(
            {3, {MutationEffect::MayHaveChanged, false}, true, 1, divergentParty});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && state.party.possiblyOwned &&
                   memory.party == 0xB4u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2 &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1, kRt0SeymourSite2,
                                              kRt0SeymourParty, kRt0SeymourSite2,
                                              kRt0SeymourSite1}),
               "divergent party controlled bits must remain untouched while code rolls back");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored,
               "later observation of original controlled party bits may clear Conflict");
    }
}

void TestSeymourRollbackFaultMatrix() {
    using namespace FfxHooks::F8Runtime;

    for (const bool failFlush : {true, false}) {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "global ownership-clear fault setup must apply");
        memory.ClearScript();
        if (failFlush) {
            memory.failedFlushCalls.push_back(1);
        } else {
            memory.failedEndCalls.push_back(1);
        }
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.party.possiblyOwned &&
                   state.sites[0].possiblyOwned && state.sites[1].possiblyOwned &&
                   memory.party == 0xA4u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2 &&
                   state.protection.active == !failFlush,
               "flush/end failure must retain every bundle owner after successful readbacks");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.writeCalls == 0 && memory.flushCalls == 1 &&
                   SeymourOwnershipClear(state),
               "one later global readback/flush/protection proof must clear the whole bundle");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedEndCalls = {1, 2};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.protection.active &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2 &&
                   (state.sites[0].possiblyOwned || state.sites[1].possiblyOwned),
               "repeated protection restore failure must retain token and code ownership");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state) && memory.writeCalls == 0 &&
                   memory.flushCalls == 1 && memory.endCalls == 1,
               "later restore must flush original bytes and close a retained protection token");
    }

    for (const int rollbackWriteCall : {3, 4}) {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls.push_back(1);
        memory.writeDirectives.push_back(
            {rollbackWriteCall, {MutationEffect::MayHaveChanged, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        const uintptr_t pendingAddress = rollbackWriteCall == 3
            ? kRt0SeymourSite2
            : kRt0SeymourSite1;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::Conflict && state.state == BundleState::Conflict &&
                   ((pendingAddress == kRt0SeymourSite1 &&
                     state.sites[0].possiblyOwned && state.sites[0].conflictWitness) ||
                    (pendingAddress == kRt0SeymourSite2 &&
                     state.sites[1].possiblyOwned && state.sites[1].conflictWitness)) &&
                   memory.CodeAt(pendingAddress) == kRt0SeymourNops,
               "each rollback code MayHaveChanged Candidate must become a witness");
        memory.SetCode(
            pendingAddress,
            pendingAddress == kRt0SeymourSite1
                ? kRt0SeymourSignature1
                : kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   memory.writeCalls == 0,
               "only later Original may release either witnessed rollback site");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls = {1, 2};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && SeymourMemoryIsOriginal(memory, 0xA4u) &&
                   (state.sites[0].possiblyOwned || state.sites[1].possiblyOwned),
               "failed rollback flush must retain code ownership even after original readback");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.writeCalls == 0 && memory.flushCalls == 1 &&
                   SeymourOwnershipClear(state),
               "later exact-span flush may clear already-original pending code");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls.push_back(1);
        memory.readFaults.push_back({kRt0SeymourSite2, 3});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.sites[1].possiblyOwned &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1,
               "rollback read failure must preserve site2 while continuing reverse recovery");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored,
               "later restore must recover a rollback read failure");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls.push_back(1);
        memory.readFaults.push_back({kRt0SeymourSite2, 4});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.sites[1].possiblyOwned &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "rollback write readback failure must retain ownership despite original memory");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.writeCalls == 0 && memory.flushCalls == 1,
               "later read/flush proof must clear rollback readback ambiguity without rewriting");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {3, {MutationEffect::NotTouched, false}, false, 0, {}});
        memory.failedBeginCalls.push_back(2);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.sites[0].possiblyOwned &&
                   state.sites[1].possiblyOwned &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourNops &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "rollback begin-protection failure must leave both NOP sites owned and pending");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored,
               "later restore must recover a rollback protection-begin failure");
    }

    {
        ScriptedPatchIo memory{};
        memory.readFaults.push_back({kRt0SeymourParty, 3});
        memory.writeDirectives.push_back(
            {4, {MutationEffect::MayHaveChanged, false}, false, 0, {}});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::Conflict && state.state == BundleState::Conflict &&
                   state.party.possiblyOwned && state.party.conflictWitness &&
                   memory.party == 0xB5u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "rollback party MayHaveChanged Candidate must become a witness");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.party == 0xA4u && SeymourOwnershipClear(state) &&
                   memory.writeCalls == 0,
               "only later Original may release a witnessed party rollback");
    }

    {
        ScriptedPatchIo memory{};
        memory.readFaults.push_back({kRt0SeymourParty, 3});
        memory.readFaults.push_back({kRt0SeymourParty, 4});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.party.possiblyOwned &&
                   memory.party == 0xB5u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "unreadable party rollback must retain its owner while code continues reversing");
        PatchIo invalid = MakePatchIo(memory);
        invalid.writeIfEqual = nullptr;
        const int writesBeforeInvalidRetry = memory.writeCalls;
        Expect(RestoreSeymourBundle(invalid, &state) == BundleResult::RestorePending &&
                   state.party.possiblyOwned && memory.writeCalls == writesBeforeInvalidRetry,
               "invalid retry I/O must retain pending ownership without another write");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.party == 0xA4u && SeymourOwnershipClear(state),
               "later valid retry must recover an unreadable party rollback");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "party restore readback-fault setup must apply");
        memory.ClearScript();
        memory.readFaults.push_back({kRt0SeymourParty, 2});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.party.possiblyOwned &&
                   memory.party == 0xA4u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "party restore write with failed readback must retain owner after reversing code");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.writeCalls == 0 && SeymourOwnershipClear(state),
               "later original party readback must clear ambiguity without rewriting");
    }

    {
        ScriptedPatchIo memory{};
        memory.failedFlushCalls.push_back(1);
        memory.readFaults.push_back({kRt0SeymourSite2, 4});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.sites[1].possiblyOwned,
               "repeated restore setup must retain ambiguous site2 provenance");
        memory.ClearScript();
        memory.readFaults.push_back({kRt0SeymourSite2, 1});
        memory.readFaults.push_back({kRt0SeymourSite2, 2});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending &&
                   RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[1].possiblyOwned && memory.writeCalls == 0,
               "repeated unreadable pre-write observations must stay bounded and write-free");
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "a later repeated disable/stop call must complete once the fault clears");
    }
}

void TestSeymourOwnershipAndConflict() {
    using namespace FfxHooks::F8Runtime;

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "Active controlled-party drift setup must apply");
        memory.ClearScript();
        memory.party = static_cast<uint8_t>(memory.party & 0xEEu);
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && memory.writeCalls == 0,
               "Active unexpected party bits must become Conflict without reassertion");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourMemoryIsOriginal(memory, 0xA4u),
               "Active drift back to captured bits must allow reverse code restoration");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "third-party controlled-bit conflict setup must apply");
        memory.party = 0xB4u;
        memory.ClearScript();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.writeCalls == 0,
               "Active third controlled value must become sticky Conflict without a write");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.party == 0xB4u &&
                   PatchWritesMatch(memory, {kRt0SeymourSite2, kRt0SeymourSite1}),
               "Conflict restore must preserve third-party bits while restoring owned code");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.writeCalls == 0 && memory.party == 0xB4u,
               "sticky controlled-bit Conflict must remain write-free on repeated restore");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only later observation of captured controlled bits may clear sticky Conflict");
    }

    {
        const std::array<uint8_t, 4> third = {0xF1u, 0xF2u, 0xF3u, 0xF4u};
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "third code value conflict setup must apply");
        memory.SetCode(kRt0SeymourSite2, third);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.CodeAt(kRt0SeymourSite2) == third &&
                   PatchWritesMatch(memory, {kRt0SeymourParty, kRt0SeymourSite1}),
               "restore must skip third-party site2 while reversing party and site1");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.writeCalls == 0 && memory.CodeAt(kRt0SeymourSite2) == third,
               "sticky site2 Conflict must never authorize a repeated overwrite");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only later observation of original site2 may clear sticky Conflict");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "already-original restoration setup must apply");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.party = 0xC4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   PatchWritesMatch(memory, {kRt0SeymourSite1}) &&
                   SeymourMemoryIsOriginal(memory, 0xC4u),
               "already-original party/site2 count as restored without another write");
    }

    {
        ScriptedPatchIo memory(0x10u);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied &&
                   memory.party == 0x11u && state.party.originalMaskedBits == 0x10u,
               "apply must capture a nonzero original controlled-bit subset");
        memory.party = 0x7Bu;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   memory.party == 0x7Au,
               "masked restore must use current non-owned bits plus captured 0x10");
    }
}

void TestSeymourExactDerivedLayout() {
    using namespace FfxHooks::F8Runtime;

    {
        constexpr uintptr_t imageBase = 0x1000u;
        ScriptedPatchIo memory{};
        ConfigureShiftedSeymourMemory(memory, imageBase);
        SeymourBundleState state = MakeSeymourState(imageBase);
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "an exact nonzero page-aligned derived image base must remain admissible");
    }

    {
        ScriptedPatchIo memory{};
        memory.partyAddress = kRt0SeymourParty + 1u;
        SeymourBundleState state = MakeSeymourState();
        state.party.address = memory.partyAddress;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed && memory.events.empty() &&
                   SeymourOwnershipClear(state),
               "correct code sites plus a readable party+1 must fail layout before I/O");
    }

    {
        constexpr uintptr_t imageBase = 0x1000u;
        ScriptedPatchIo memory{};
        ConfigureShiftedSeymourMemory(memory, imageBase);
        memory.partyAddress = kRt0SeymourParty;
        SeymourBundleState state = MakeSeymourState(imageBase);
        state.party.address = kRt0SeymourParty;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed && memory.events.empty() &&
                   SeymourOwnershipClear(state),
               "shifted readable sites plus an unshifted party must fail exact base binding");
    }

    {
        constexpr uintptr_t unalignedBase = 1u;
        ScriptedPatchIo memory{};
        ConfigureShiftedSeymourMemory(memory, unalignedBase);
        SeymourBundleState state = MakeSeymourState(unalignedBase);
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed && memory.events.empty() &&
                   SeymourOwnershipClear(state),
               "an unaligned derived base and wrong derived page must fail before I/O");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        state.sites[0].address = kRt0SeymourSite1 - 1u;
        state.sites[1].address = state.sites[0].address +
                                 (kRt0SeymourSite2 - kRt0SeymourSite1);
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed && memory.events.empty() &&
                   SeymourOwnershipClear(state),
               "site1 below its RVA must fail derived-base underflow before callbacks");
    }

    {
        constexpr uintptr_t pageMask = ~static_cast<uintptr_t>(0xFFFu);
        const uintptr_t nearTopBase =
            ((std::numeric_limits<uintptr_t>::max)() - kRt0SeymourSite2 - 4u) & pageMask;
        ScriptedPatchIo memory{};
        SeymourBundleState state{};
        state.sites[0].address = nearTopBase + kRt0SeymourSite1;
        state.sites[1].address = nearTopBase + kRt0SeymourSite2;
        state.party.address = kRt0SeymourParty;
        state.party.mask = kRt0SeymourMask;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::PreflightFailed && memory.events.empty() &&
                   SeymourOwnershipClear(state),
               "a derived base whose approved party RVA overflows must fail before callbacks");
    }
}

void TestSeymourConditionalWriteRaces() {
    using namespace FfxHooks::F8Runtime;
    const std::array<uint8_t, 4> third = {0xCCu, 0xCCu, 0xCCu, 0xCCu};

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {2, {MutationEffect::Verified, true}, true, 0, {}, third.size(), third});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict &&
                   !state.sites[1].possiblyOwned && state.sites[1].conflictWitness &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == third && memory.party == 0xA4u,
               "site2 drift after preflight/site1 must survive the conditional apply attempt");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && memory.writeCalls == 0 &&
                   !state.sites[1].possiblyOwned && state.sites[1].conflictWitness &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "unowned desired after NotTouched must remain witnessed and never become ownership");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only Original at the unowned witnessed site may complete cleanup");
    }

    {
        const std::array<uint8_t, 4> nonOwnedPartyDrift = {0xC4u, 0, 0, 0};
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {3, {MutationEffect::Verified, true}, true, 0, {}, 1, nonOwnedPartyDrift});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack &&
                   SeymourMemoryIsOriginal(memory, 0xC4u) && SeymourOwnershipClear(state),
               "party non-owned drift between observation and write must never be lost");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "conditional restore-race setup must apply");
        memory.ClearScript();
        memory.writeDirectives.push_back(
            {2, {MutationEffect::Verified, true}, true, 0, {}, third.size(), third});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::Conflict && state.state == BundleState::Conflict &&
                   memory.party == 0xA4u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == third,
               "site2 drift after restore observation must survive the conditional restore attempt");
    }

    {
        ScriptedPatchIo memory{};
        memory.writeDirectives.push_back(
            {2, {MutationEffect::Verified, true}, false, 0, {}, third.size(), third, true});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.CodeAt(kRt0SeymourSite2) == third,
               "optimistic callback readback metadata must not bypass core readback after compare drift");
    }
}

void TestSeymourActiveMaskAndRestoreAba() {
    using namespace FfxHooks::F8Runtime;

    {
        ScriptedPatchIo memory(0xB5u);
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied &&
                   state.party.originalMaskedBits == kRt0SeymourMask &&
                   !state.party.possiblyOwned,
               "already-set original party mask must allow code-only apply");
        memory.ClearScript();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::AlreadyInState && state.state == BundleState::Active &&
                   !state.party.conflictWitness && memory.writeCalls == 0,
               "Active party comparison must use raw controlled bits when original is 0x11");
    }

    {
        ScriptedPatchIo memory{};
        memory.partyReadMutations.push_back({2, 0xB5u});
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && state.party.conflictWitness &&
                   !state.party.possiblyOwned && memory.party == 0xB5u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "fresh unowned desired party bits must become a witness rather than Active");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.party.conflictWitness && !state.party.possiblyOwned &&
                   memory.writeCalls == 0 && memory.party == 0xB5u,
               "unowned desired party witness must remain sticky and write-free");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only Original may clear a fresh unowned desired party witness");
    }

    {
        const std::array<uint8_t, 4> third = {0xCCu, 0xCCu, 0xCCu, 0xCCu};
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "code restore ABA setup must apply");
        memory.ClearScript();
        memory.writeDirectives.push_back(
            {2, {MutationEffect::NotTouched, false}, false,
             kRt0SeymourNops.size(), kRt0SeymourNops, third.size(), third});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && state.sites[1].conflictWitness &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "NotTouched code restore whose expected NOP reappears must latch a witness");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.writeCalls == 0 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "code ABA witness must block a later restore retry over Candidate");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only Original at the code ABA resource may clear its witness");
    }

    {
        const std::array<uint8_t, 4> thirdParty = {0xB4u, 0, 0, 0};
        const std::array<uint8_t, 4> candidateParty = {0xB5u, 0, 0, 0};
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "party restore ABA setup must apply");
        memory.ClearScript();
        memory.writeDirectives.push_back(
            {1, {MutationEffect::NotTouched, false}, false,
             1, candidateParty, 1, thirdParty});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && state.party.conflictWitness &&
                   memory.party == 0xB5u,
               "NotTouched party restore whose expected byte reappears must latch a witness");
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.writeCalls == 0 && memory.party == 0xB5u,
               "party ABA witness must block a later restore retry over Candidate");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only Original at the party ABA resource may clear its witness");
    }
}

void ExpectSeymourCodeCandidateRemainsExternal(
    ScriptedPatchIo& memory, FfxHooks::F8Runtime::SeymourBundleState& state,
    const char* conflictMessage, const char* originalMessage) {
    using namespace FfxHooks::F8Runtime;
    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
               state.state == BundleState::Conflict && state.sites[1].conflictWitness &&
               memory.writeCalls == 0 &&
               memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
           conflictMessage);
    memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
               state.state == BundleState::Inactive && SeymourOwnershipClear(state),
           originalMessage);
}

void ExpectSeymourPartyCandidateRemainsExternal(
    ScriptedPatchIo& memory, FfxHooks::F8Runtime::SeymourBundleState& state,
    const char* conflictMessage, const char* originalMessage) {
    using namespace FfxHooks::F8Runtime;
    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
               state.state == BundleState::Conflict && state.party.conflictWitness &&
               memory.writeCalls == 0 && memory.party == 0xB5u,
           conflictMessage);
    memory.party = 0xA4u;
    memory.ClearScript();
    Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
               state.state == BundleState::Inactive && SeymourOwnershipClear(state),
           originalMessage);
}

void TestSeymourRestoreProvenance() {
    using namespace FfxHooks::F8Runtime;
    const std::array<MutationReport, 2> mutatingReports = {{
        {MutationEffect::Verified, true},
        {MutationEffect::MayHaveChanged, false},
    }};
    const std::array<uint8_t, 4> candidateParty = {0xB5u, 0, 0, 0};

    for (const MutationReport report : mutatingReports) {
        {
            ScriptedPatchIo memory{};
            SeymourBundleState state = MakeSeymourState();
            Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
                   "post-write code Candidate provenance setup must apply");
            memory.ClearScript();
            memory.writeDirectives.push_back(
                {2, report, true, kRt0SeymourNops.size(), kRt0SeymourNops});
            Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                       BundleResult::Conflict && state.state == BundleState::Conflict &&
                       state.sites[1].conflictWitness &&
                       memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
                   "Verified/MayHaveChanged code restore followed by Candidate must latch conflict");
            ExpectSeymourCodeCandidateRemainsExternal(
                memory, state,
                "post-write code Candidate must never regain restore write authority",
                "post-write code witness must clear only when that site is Original");
        }

        {
            ScriptedPatchIo memory{};
            SeymourBundleState state = MakeSeymourState();
            Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
                   "post-write party Candidate provenance setup must apply");
            memory.ClearScript();
            memory.writeDirectives.push_back(
                {1, report, true, 1, candidateParty});
            Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                       BundleResult::Conflict && state.state == BundleState::Conflict &&
                       state.party.conflictWitness && memory.party == 0xB5u,
                   "Verified/MayHaveChanged party restore followed by Candidate must latch conflict");
            ExpectSeymourPartyCandidateRemainsExternal(
                memory, state,
                "post-write party Candidate must never regain restore write authority",
                "post-write party witness must clear only when party is Original");
        }
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "flush-pending code provenance setup must apply");
        memory.ClearScript();
        memory.failedFlushCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[1].restorePhase ==
                       ResourceRestorePhase::OriginalConfirmed &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "code Original confirmation must survive global flush pending");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        ExpectSeymourCodeCandidateRemainsExternal(
            memory, state,
            "code Candidate after Original plus flush pending must be external",
            "flush-pending code provenance must clear only at site Original");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "flush-pending party provenance setup must apply");
        memory.ClearScript();
        memory.failedFlushCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.party.restorePhase == ResourceRestorePhase::OriginalConfirmed &&
                   memory.party == 0xA4u,
               "party Original confirmation must survive global flush pending");
        memory.party = 0xB5u;
        ExpectSeymourPartyCandidateRemainsExternal(
            memory, state,
            "party Candidate after Original plus flush pending must be external",
            "flush-pending party provenance must clear only at party Original");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "end-pending code provenance setup must apply");
        memory.ClearScript();
        memory.failedEndCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   HasExactProtectionSnapshot(state.protection) &&
                   state.sites[1].restorePhase ==
                       ResourceRestorePhase::OriginalConfirmed &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "code Original confirmation must survive protection-end pending");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        ExpectSeymourCodeCandidateRemainsExternal(
            memory, state,
            "code Candidate after Original plus end pending must be external",
            "end-pending code provenance must clear only at site Original");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "end-pending party provenance setup must apply");
        memory.ClearScript();
        memory.failedEndCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   HasExactProtectionSnapshot(state.protection) &&
                   state.party.restorePhase == ResourceRestorePhase::OriginalConfirmed &&
                   memory.party == 0xA4u,
               "party Original confirmation must survive protection-end pending");
        memory.party = 0xB5u;
        ExpectSeymourPartyCandidateRemainsExternal(
            memory, state,
            "party Candidate after Original plus end pending must be external",
            "end-pending party provenance must clear only at party Original");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "unreadable code restore provenance setup must apply");
        memory.ClearScript();
        memory.writeDirectives.push_back(
            {2, {MutationEffect::MayHaveChanged, false}, true, 0, {}});
        memory.readFaults.push_back({kRt0SeymourSite2, 2});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[1].restorePhase ==
                       ResourceRestorePhase::MutationReadbackUnknown &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourSignature2,
               "potentially-mutating code restore with unreadable readback must stay pending");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        ExpectSeymourCodeCandidateRemainsExternal(
            memory, state,
            "code Candidate after ambiguous restore readback must be external",
            "ambiguous code restore provenance must clear only at site Original");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "unreadable party restore provenance setup must apply");
        memory.ClearScript();
        memory.writeDirectives.push_back(
            {1, {MutationEffect::MayHaveChanged, false}, true, 0, {}});
        memory.readFaults.push_back({kRt0SeymourParty, 2});
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.party.restorePhase ==
                       ResourceRestorePhase::MutationReadbackUnknown &&
                   memory.party == 0xA4u,
               "potentially-mutating party restore with unreadable readback must stay pending");
        memory.party = 0xB5u;
        ExpectSeymourPartyCandidateRemainsExternal(
            memory, state,
            "party Candidate after ambiguous restore readback must be external",
            "ambiguous party restore provenance must clear only at party Original");
    }
}

void TestSeymourRestorePhaseIsObligation() {
    using namespace FfxHooks::F8Runtime;

    {
        ScriptedPatchIo memory{};
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        SeymourBundleState state = MakeSeymourState();
        state.sites[1].original = kRt0SeymourSignature2;
        state.sites[1].restorePhase = ResourceRestorePhase::OriginalConfirmed;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict &&
                   !state.sites[1].possiblyOwned && state.sites[1].conflictWitness &&
                   memory.writeCalls == 0 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "code restore phase alone must remain an obligation and revoke Candidate authority");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state) && memory.writeCalls == 0,
               "phase-only code obligation must clear only after that site is Original");
    }

    {
        ScriptedPatchIo memory(0xB5u);
        SeymourBundleState state = MakeSeymourState();
        state.party.originalMaskedBits = 0;
        state.party.restorePhase = ResourceRestorePhase::OriginalConfirmed;
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict &&
                   !state.party.possiblyOwned && state.party.conflictWitness &&
                   memory.writeCalls == 0 && memory.party == 0xB5u,
               "party restore phase alone must remain an obligation and revoke Candidate authority");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state) && memory.writeCalls == 0,
               "phase-only party obligation must clear only after party is Original");
    }
}

void TestSeymourPerResourceConflictWitness() {
    using namespace FfxHooks::F8Runtime;
    const std::array<uint8_t, 4> third = {0xF1u, 0xF2u, 0xF3u, 0xF4u};

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "code conflict-witness setup must apply");
        memory.SetCode(kRt0SeymourSite2, third);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.CodeAt(kRt0SeymourSite2) == third,
               "site2 Third must establish its own sticky conflict witness");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourNops);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && memory.writeCalls == 0 &&
                   memory.CodeAt(kRt0SeymourSite2) == kRt0SeymourNops,
               "site2 Third-to-Candidate must remain unwritable when other resources are Original");
        memory.SetCode(kRt0SeymourSite2, kRt0SeymourSignature2);
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only site2 itself observed Original may clear the site2 conflict witness");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "party conflict-witness setup must apply");
        memory.party = 0xB4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.party == 0xB4u,
               "party Third must establish its own sticky conflict witness");
        memory.party = 0xB5u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   state.state == BundleState::Conflict && memory.writeCalls == 0 &&
                   memory.party == 0xB5u,
               "party Third-to-Candidate must remain unwritable when code is Original");
        memory.party = 0xA4u;
        memory.ClearScript();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) == BundleResult::Restored &&
                   SeymourOwnershipClear(state),
               "only party itself observed Original may clear the party conflict witness");
    }
}

void TestSeymourProtectionSnapshotValidation() {
    using namespace FfxHooks::F8Runtime;
    const std::array<PatchProtectionToken, 5> invalidSuccessTokens = {{
        {kRt0SeymourSite1, kRt0SeymourSpanLength, 0x20u, true},
        {kRt0SeymourSite1 + 1u, kRt0SeymourSpanLength, 0x20u, false},
        {kRt0SeymourSite1, kRt0SeymourSpanLength - 1u, 0x20u, false},
        {kRt0SeymourSite1, kRt0SeymourSpanLength, 0x40u, false},
        {kRt0SeymourSite1 + 1u, kRt0SeymourSpanLength, 0x40u, true},
    }};
    for (const PatchProtectionToken& returned : invalidSuccessTokens) {
        ScriptedPatchIo memory{};
        memory.endDirectives.push_back({1, true, true, returned});
        SeymourBundleState state = MakePendingOriginalSeymourState();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   HasExactProtectionSnapshot(state.protection) && memory.writeCalls == 0 &&
                   memory.flushCalls == 1 && memory.endCalls == 1,
               "end success must close a copy and preserve exact token identity or remain pending");
    }

    {
        ScriptedPatchIo memory{};
        memory.endDirectives.push_back({1, false, true, {0, 0, 0, false}});
        SeymourBundleState state = MakePendingOriginalSeymourState();
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && HasExactProtectionSnapshot(state.protection) &&
                   memory.writeCalls == 0 && memory.endCalls == 1,
               "false end that clears/corrupts its copy must retain the exact active snapshot");
    }
}

void TestSeymourPendingPrecedenceAndApplyCleanup() {
    using namespace FfxHooks::F8Runtime;
    const std::array<uint8_t, 4> third = {0xF1u, 0xF2u, 0xF3u, 0xF4u};

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "conflict-plus-flush setup must apply");
        memory.SetCode(kRt0SeymourSite2, third);
        memory.ClearScript();
        memory.failedFlushCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[1].possiblyOwned && memory.CodeAt(kRt0SeymourSite2) == third,
               "flush uncertainty must outrank a simultaneous site2 conflict witness");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "conflict-plus-end setup must apply");
        memory.SetCode(kRt0SeymourSite2, third);
        memory.ClearScript();
        memory.failedEndCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && state.state == BundleState::RestorePending &&
                   state.sites[1].possiblyOwned && HasExactProtectionSnapshot(state.protection) &&
                   memory.CodeAt(kRt0SeymourSite2) == third,
               "protection uncertainty must outrank a simultaneous site2 conflict witness");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "Apply cleanup retry setup must apply");
        memory.ClearScript();
        memory.failedEndCalls.push_back(1);
        Expect(RestoreSeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::RestorePending && HasExactProtectionSnapshot(state.protection),
               "Apply cleanup retry setup must retain a pending protection token");
        memory.ClearScript();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) ==
                   BundleResult::ApplyFailedRolledBack && state.state == BundleState::Inactive &&
                   SeymourMemoryIsOriginal(memory, 0xA4u) && SeymourOwnershipClear(state) &&
                   memory.writeCalls == 0 && memory.flushCalls == 1 && memory.endCalls == 1,
               "Apply while RestorePending must actively finish cleanup without reapplying");
    }

    {
        ScriptedPatchIo memory{};
        SeymourBundleState state = MakeSeymourState();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Applied,
               "Apply-in-Conflict cleanup setup must apply");
        memory.SetCode(kRt0SeymourSite2, third);
        memory.ClearScript();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict,
               "Active third value must first publish Conflict");
        memory.ClearScript();
        Expect(ApplySeymourBundle(MakePatchIo(memory), &state) == BundleResult::Conflict &&
                   memory.party == 0xA4u &&
                   memory.CodeAt(kRt0SeymourSite1) == kRt0SeymourSignature1 &&
                   memory.CodeAt(kRt0SeymourSite2) == third,
               "Apply in Conflict must conservatively clean owned peers without touching witness");
    }
}

struct AdmissionTestBarrier {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
};

void PauseAfterProvisionalIncrement(void* context) {
    auto& barrier = *static_cast<AdmissionTestBarrier*>(context);
    std::unique_lock<std::mutex> lock(barrier.mutex);
    barrier.entered = true;
    barrier.condition.notify_all();
    barrier.condition.wait(lock, [&]() { return barrier.release; });
}

void TestRuntimeCoreCadenceAndLifecycle() {
    using namespace FfxHooks::F8Runtime;
    TickGate gate{};
    Expect(ArmTickGate(&gate), "first cadence arm must succeed");
    Expect(!ArmTickGate(&gate), "duplicate cadence arm must be idempotently rejected");
    Expect(TryBeginTick(&gate, 1000, 33), "first armed tick must run immediately");
    Expect(!TryBeginTick(&gate, 1001, 33), "tick gate must reject reentrancy");
    EndTick(&gate);
    Expect(!TryBeginTick(&gate, 1032, 33), "32 ms cadence boundary must remain closed");
    Expect(TryBeginTick(&gate, 1033, 33), "33 ms cadence boundary must open");
    EndTick(&gate);
    Expect(DisarmTickGate(&gate), "first cadence disarm must succeed");
    Expect(!DisarmTickGate(&gate), "repeated cadence disarm must be idempotent");
    Expect(!TryBeginTick(&gate, 2000, 33), "disarmed cadence gate must reject ticks");

    TickGate wrapping{};
    Expect(ArmTickGate(&wrapping) && TryBeginTick(&wrapping, 0xFFFFFFF0u, 33),
           "wraparound cadence setup must tick immediately");
    EndTick(&wrapping);
    Expect(!TryBeginTick(&wrapping, 0x00000010u, 33),
           "32 ms unsigned wrap delta must remain closed");
    Expect(TryBeginTick(&wrapping, 0x00000011u, 33),
           "33 ms unsigned wrap delta must open");
    EndTick(&wrapping);

    AtomicLifecycle lifecycle{};
    Expect(StartLifecycle(&lifecycle), "stopped lifecycle must start once");
    Expect(!StartLifecycle(&lifecycle), "duplicate lifecycle start must be rejected");
    std::atomic<bool> begin{false};
    std::atomic<bool> stopObserved{false};
    std::atomic<uint32_t> admitted{0};
    std::atomic<uint32_t> left{0};
    std::atomic<uint32_t> postStopAttempts{0};
    std::atomic<uint32_t> postStopAdmissions{0};
    std::array<std::thread, 8> workers;
    for (std::thread& worker : workers) {
        worker = std::thread([&]() {
            while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
            while (!stopObserved.load(std::memory_order_acquire)) {
                if (TryEnterFrame(&lifecycle)) {
                    admitted.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                    LeaveFrame(&lifecycle);
                    left.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (int attempt = 0; attempt < 128; ++attempt) {
                if (lifecycle.state.load(std::memory_order_acquire) ==
                    static_cast<uint32_t>(RuntimeLifecycle::Stopping)) {
                    postStopAttempts.fetch_add(1, std::memory_order_relaxed);
                    if (TryEnterFrame(&lifecycle)) {
                        postStopAdmissions.fetch_add(1, std::memory_order_relaxed);
                        LeaveFrame(&lifecycle);
                    }
                }
            }
        });
    }
    begin.store(true, std::memory_order_release);
    const auto admissionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (admitted.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < admissionDeadline) {
        std::this_thread::yield();
    }
    RequestLifecycleStop(&lifecycle);
    stopObserved.store(true, std::memory_order_release);
    for (std::thread& worker : workers) worker.join();
    Expect(admitted.load() != 0, "lifecycle race must exercise at least one admitted frame");
    Expect(postStopAttempts.load() != 0 && postStopAdmissions.load() == 0,
           "after Stopping is visible no new frame admission may succeed");
    Expect(admitted.load() == left.load() && lifecycle.frameInFlight.load() == 0,
           "every admitted frame must leave exactly once and drain frameInFlight");
    Expect(!StartLifecycle(&lifecycle), "a stop request must prevent later restart");

    AtomicLifecycle deterministicStop{};
    AdmissionTestBarrier admissionBarrier{};
    deterministicStop.testContext = &admissionBarrier;
    deterministicStop.afterProvisionalIncrementForTests = &PauseAfterProvisionalIncrement;
    Expect(StartLifecycle(&deterministicStop),
           "deterministic stop-admission setup must start");
    std::atomic<bool> deterministicAdmission{true};
    std::thread provisionalAdmission([&]() {
        deterministicAdmission.store(
            TryEnterFrame(&deterministicStop), std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(admissionBarrier.mutex);
        Expect(WaitForCondition(admissionBarrier.condition, lock,
                   [&]() { return admissionBarrier.entered; }),
               "admission seam must pause after provisional increment");
        Expect(deterministicStop.frameInFlight.load(std::memory_order_acquire) == 1,
               "provisional admission must be accounted before the second state check");
        RequestLifecycleStop(&deterministicStop);
        Expect(deterministicStop.state.load(std::memory_order_acquire) ==
                   static_cast<uint32_t>(RuntimeLifecycle::Stopping),
               "stop must become visible while provisional admission is paused");
        admissionBarrier.release = true;
        admissionBarrier.condition.notify_all();
    }
    provisionalAdmission.join();
    Expect(!deterministicAdmission.load(std::memory_order_acquire) &&
               deterministicStop.frameInFlight.load(std::memory_order_acquire) == 0,
           "stop at the post-increment barrier must reject admission and drain its count");

    AtomicLifecycle stopBeforeStart{};
    RequestLifecycleStop(&stopBeforeStart);
    Expect(!StartLifecycle(&stopBeforeStart) &&
               stopBeforeStart.state.load() ==
                   static_cast<uint32_t>(RuntimeLifecycle::Stopping),
           "stop-before-start must permanently close frame admission");

    AtomicLifecycle startThenReady{};
    Expect(StartLifecycle(&startThenReady), "producer order setup must start");
    PublishProducerState(&startThenReady, ProducerState::Ready);
    Expect(ReadProducerState(&startThenReady) == ProducerState::Ready,
           "Start then Ready must publish Ready");

    AtomicLifecycle readyThenStart{};
    PublishProducerState(&readyThenStart, ProducerState::Ready);
    Expect(StartLifecycle(&readyThenStart) &&
               ReadProducerState(&readyThenStart) == ProducerState::Ready,
           "Ready then Start must preserve Ready");

    AtomicLifecycle startThenFailure{};
    Expect(StartLifecycle(&startThenFailure), "terminal producer setup must start");
    PublishProducerState(&startThenFailure, ProducerState::TerminalFailure);
    PublishProducerState(&startThenFailure, ProducerState::Ready);
    Expect(ReadProducerState(&startThenFailure) == ProducerState::TerminalFailure,
           "Start then TerminalFailure must remain terminal after Ready");

    AtomicLifecycle readyThenFailure{};
    PublishProducerState(&readyThenFailure, ProducerState::Ready);
    PublishProducerState(&readyThenFailure, ProducerState::TerminalFailure);
    Expect(ReadProducerState(&readyThenFailure) == ProducerState::TerminalFailure,
           "Ready then TerminalFailure must end in sticky terminal failure");

    AtomicLifecycle failureThenStart{};
    PublishProducerState(&failureThenStart, ProducerState::TerminalFailure);
    Expect(StartLifecycle(&failureThenStart), "failure-before-start lifecycle may still arm");
    PublishProducerState(&failureThenStart, ProducerState::Unknown);
    Expect(ReadProducerState(&failureThenStart) == ProducerState::TerminalFailure,
           "TerminalFailure then Start/Pending initialization must remain terminal");

    bool everyConcurrentPublisherRaceWasTerminal = true;
    for (int iteration = 0; iteration < 64; ++iteration) {
        AtomicLifecycle concurrentPublish{};
        std::atomic<int> publishersReady{0};
        std::atomic<bool> publish{false};
        std::thread readyPublisher([&]() {
            publishersReady.fetch_add(1, std::memory_order_release);
            while (!publish.load(std::memory_order_acquire)) std::this_thread::yield();
            PublishProducerState(&concurrentPublish, ProducerState::Ready);
        });
        std::thread failurePublisher([&]() {
            publishersReady.fetch_add(1, std::memory_order_release);
            while (!publish.load(std::memory_order_acquire)) std::this_thread::yield();
            PublishProducerState(&concurrentPublish, ProducerState::TerminalFailure);
        });
        while (publishersReady.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        publish.store(true, std::memory_order_release);
        readyPublisher.join();
        failurePublisher.join();
        everyConcurrentPublisherRaceWasTerminal = everyConcurrentPublisherRaceWasTerminal &&
            ReadProducerState(&concurrentPublish) == ProducerState::TerminalFailure;
    }
    Expect(everyConcurrentPublisherRaceWasTerminal,
           "synchronized concurrent Ready/TerminalFailure publishers must always end terminal");
}

void TestRuntimeCorePresentHookArbiter() {
    using namespace FfxHooks::F8Runtime;

    AtomicPresentHookArbiter failedInstall{};
    Expect(ReadPresentHookPhysicalState(&failedInstall) == PresentHookPhysicalState::Idle,
           "physical Present arbiter must start Idle");
    Expect(TryBeginPresentHookInstall(&failedInstall) &&
               ReadPresentHookPhysicalState(&failedInstall) ==
                   PresentHookPhysicalState::Installing,
           "first physical Present install must acquire Installing");
    Expect(RequestPresentHookTerminal(&failedInstall) == PresentHookResult::None,
           "terminal request during Installing must defer publication");
    Expect(CompletePresentHookInstall(&failedInstall, false) ==
               PresentHookResult::PublishTerminal &&
               ReadPresentHookPhysicalState(&failedInstall) ==
                   PresentHookPhysicalState::Idle,
           "failed install must resolve a deferred terminal request exactly once");
    Expect(RequestPresentHookTerminal(&failedInstall) == PresentHookResult::None,
           "published terminal result must remain sticky across repeated requests");

    AtomicPresentHookArbiter successfulInstall{};
    Expect(TryBeginPresentHookInstall(&successfulInstall),
           "successful physical Present setup must acquire Installing");
    Expect(RequestPresentHookTerminal(&successfulInstall) == PresentHookResult::None,
           "terminal request during the successful attempt must defer");
    Expect(CompletePresentHookInstall(&successfulInstall, true) == PresentHookResult::Ready &&
               ReadPresentHookPhysicalState(&successfulInstall) ==
                   PresentHookPhysicalState::Ready &&
               RequestPresentHookTerminal(&successfulInstall) == PresentHookResult::None,
           "successful Ready completion must win over the deferred terminal request");

    AtomicPresentHookArbiter terminalClaimBeforeExternalPublish{};
    AtomicLifecycle delayedTerminalLifecycle{};
    const PresentHookResult delayedTerminal =
        RequestPresentHookTerminal(&terminalClaimBeforeExternalPublish);
    Expect(delayedTerminal == PresentHookResult::PublishTerminal,
           "terminal-before-install must claim exactly one delayed UnX publication");

    const bool physicalInstallBegan =
        TryBeginPresentHookInstall(&terminalClaimBeforeExternalPublish);
    const PresentHookResult physicalCompletion =
        CompletePresentHookInstall(&terminalClaimBeforeExternalPublish, true);
    bool unxReadyStartedLifecycle = false;
    if (physicalCompletion == PresentHookResult::Ready) {
        PublishProducerState(&delayedTerminalLifecycle, ProducerState::Ready);
        unxReadyStartedLifecycle = StartLifecycle(&delayedTerminalLifecycle);
    }
    PublishProducerState(&delayedTerminalLifecycle, ProducerState::TerminalFailure);
    RequestLifecycleStop(&delayedTerminalLifecycle);
    Expect(physicalInstallBegan && physicalCompletion == PresentHookResult::None &&
               ReadPresentHookPhysicalState(&terminalClaimBeforeExternalPublish) ==
                   PresentHookPhysicalState::Ready &&
               !unxReadyStartedLifecycle &&
               delayedTerminalLifecycle.state.load(std::memory_order_acquire) !=
                   static_cast<uint32_t>(RuntimeLifecycle::Running) &&
               ReadProducerState(&delayedTerminalLifecycle) ==
                   ProducerState::TerminalFailure,
           "claimed terminal must suppress later UnX Ready while preserving physical Ready");
    Expect(ResetPresentHookPhysicalState(&terminalClaimBeforeExternalPublish) &&
               ReadPresentHookPhysicalState(&terminalClaimBeforeExternalPublish) ==
                   PresentHookPhysicalState::Idle &&
               RequestPresentHookTerminal(&terminalClaimBeforeExternalPublish) ==
                   PresentHookResult::None,
           "normal-context physical reset must preserve the once-only UnX terminal latch");

    constexpr size_t kRequesters = 16;
    AtomicPresentHookArbiter concurrentRequests{};
    std::atomic<uint32_t> requestersReady{0};
    std::atomic<bool> releaseRequests{false};
    std::atomic<uint32_t> terminalPublications{0};
    std::array<std::thread, kRequesters> requesters;
    for (std::thread& requester : requesters) {
        requester = std::thread([&]() {
            requestersReady.fetch_add(1, std::memory_order_release);
            while (!releaseRequests.load(std::memory_order_acquire)) std::this_thread::yield();
            if (RequestPresentHookTerminal(&concurrentRequests) ==
                PresentHookResult::PublishTerminal) {
                terminalPublications.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    while (requestersReady.load(std::memory_order_acquire) != kRequesters) {
        std::this_thread::yield();
    }
    releaseRequests.store(true, std::memory_order_release);
    for (std::thread& requester : requesters) requester.join();
    Expect(terminalPublications.load(std::memory_order_acquire) == 1 &&
               RequestPresentHookTerminal(&concurrentRequests) == PresentHookResult::None,
           "concurrent and repeated terminal requests must publish exactly once");

    bool everyFailureRacePublishedExactlyOnce = true;
    bool everyReadyRaceSuppressedTerminal = true;
    for (int iteration = 0; iteration < 64; ++iteration) {
        AtomicPresentHookArbiter failureRace{};
        const bool failureRaceBegan = TryBeginPresentHookInstall(&failureRace);
        everyFailureRacePublishedExactlyOnce =
            failureRaceBegan && everyFailureRacePublishedExactlyOnce;
        std::atomic<uint32_t> failureRacersReady{0};
        std::atomic<bool> releaseFailureRace{false};
        PresentHookResult failureRequest = PresentHookResult::None;
        PresentHookResult failureCompletion = PresentHookResult::None;
        std::thread requestFailure([&]() {
            failureRacersReady.fetch_add(1, std::memory_order_release);
            while (!releaseFailureRace.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            failureRequest = RequestPresentHookTerminal(&failureRace);
        });
        std::thread completeFailure([&]() {
            failureRacersReady.fetch_add(1, std::memory_order_release);
            while (!releaseFailureRace.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            failureCompletion = CompletePresentHookInstall(&failureRace, false);
        });
        while (failureRacersReady.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        releaseFailureRace.store(true, std::memory_order_release);
        requestFailure.join();
        completeFailure.join();
        const int failurePublications =
            (failureRequest == PresentHookResult::PublishTerminal ? 1 : 0) +
            (failureCompletion == PresentHookResult::PublishTerminal ? 1 : 0);
        everyFailureRacePublishedExactlyOnce = everyFailureRacePublishedExactlyOnce &&
            failurePublications == 1 &&
            ReadPresentHookPhysicalState(&failureRace) == PresentHookPhysicalState::Idle &&
            RequestPresentHookTerminal(&failureRace) == PresentHookResult::None;

        AtomicPresentHookArbiter readyRace{};
        const bool readyRaceBegan = TryBeginPresentHookInstall(&readyRace);
        everyReadyRaceSuppressedTerminal =
            readyRaceBegan && everyReadyRaceSuppressedTerminal;
        std::atomic<uint32_t> readyRacersReady{0};
        std::atomic<bool> releaseReadyRace{false};
        PresentHookResult readyRequest = PresentHookResult::PublishTerminal;
        PresentHookResult readyCompletion = PresentHookResult::None;
        std::thread requestReady([&]() {
            readyRacersReady.fetch_add(1, std::memory_order_release);
            while (!releaseReadyRace.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            readyRequest = RequestPresentHookTerminal(&readyRace);
        });
        std::thread completeReady([&]() {
            readyRacersReady.fetch_add(1, std::memory_order_release);
            while (!releaseReadyRace.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            readyCompletion = CompletePresentHookInstall(&readyRace, true);
        });
        while (readyRacersReady.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        releaseReadyRace.store(true, std::memory_order_release);
        requestReady.join();
        completeReady.join();
        everyReadyRaceSuppressedTerminal = everyReadyRaceSuppressedTerminal &&
            readyRequest == PresentHookResult::None &&
            readyCompletion == PresentHookResult::Ready &&
            ReadPresentHookPhysicalState(&readyRace) == PresentHookPhysicalState::Ready;
    }
    Expect(everyFailureRacePublishedExactlyOnce,
           "every concurrent Installing terminal/failure race must publish exactly once");
    Expect(everyReadyRaceSuppressedTerminal,
           "every concurrent Installing terminal/success race must end physical Ready without terminal");
}

void TestRuntimeCoreApTransform() {
    using namespace FfxHooks::F8Runtime;
    static_assert(std::tuple_size<std::array<ApSlotUpdate, kApSlotCount>>::value == 7,
                  "AP transform must contain exactly seven outputs");

    const std::array<uint8_t, kApSlotCount> inParty = {0, 0x10, 0x11, 2, 1, 0xFF, 0};
    const std::array<uint8_t, kApSlotCount> participation = {1, 1, 1, 0, 2, 1, 0xFF};
    Expect(HasSeededBattleParticipant(inParty, participation),
           "eligible nonzero/non-0x10 slot with participation 1 must seed AP updates");
    const std::array<uint8_t, kApSlotCount> noSeedParticipation = {1, 1, 2, 0, 2, 3, 1};
    Expect(!HasSeededBattleParticipant(inParty, noSeedParticipation),
           "without eligible participation exactly 1 the AP gate must remain closed");
    const std::array<uint8_t, kApSlotCount> allIneligible = {0, 0x10, 0, 0x10, 0, 0x10, 0};
    const std::array<uint8_t, kApSlotCount> allOne = {1, 1, 1, 1, 1, 1, 1};
    Expect(!HasSeededBattleParticipant(allIneligible, allOne),
           "participation 1 does not seed an ineligible slot");

    struct ApEnvelope {
        uint32_t before;
        std::array<ApSlotUpdate, kApSlotCount> updates;
        uint32_t after;
    } envelope{0xA1B2C3D4u, {}, 0x5A6B7C8Du};
    envelope.updates = ComputeApUpdates(inParty, participation);
    Expect(envelope.before == 0xA1B2C3D4u && envelope.after == 0x5A6B7C8Du,
           "seven-slot AP transform must preserve surrounding canaries");
    const std::array<ApSlotUpdate, kApSlotCount> expected = {
        ApSlotUpdate{0, 0}, ApSlotUpdate{0, 0}, ApSlotUpdate{1, 1},
        ApSlotUpdate{2, 1}, ApSlotUpdate{2, 1}, ApSlotUpdate{1, 1},
        ApSlotUpdate{0, 0},
    };
    bool exact = true;
    for (size_t index = 0; index < kApSlotCount; ++index) {
        exact = exact && envelope.updates[index].participation == expected[index].participation &&
                envelope.updates[index].earn == expected[index].earn;
    }
    Expect(exact,
           "AP transform must preserve eligible participation 1, map other eligible to 2, and zero ineligible slots");
}

void TestMaechenStateMachineAndGeneration() {
    using namespace FfxHooks::Maechen;

    State state{};
    Actions actions = Advance(&state, Event{EventKind::F9Sample, true, 0});
    Expect(state.phase == Phase::Disabled && state.f9WasDown && !actions.requestOpen,
           "disabled Maechen must observe F9 without opening");

    actions = Advance(&state, Event{EventKind::SetEnabled, true, 0});
    Expect(state.phase == Phase::Closed && state.releaseRequired && !actions.requestOpen,
           "enabling Maechen while F9 is held must require a release before opening");
    actions = Advance(&state, Event{EventKind::F9Sample, true, 0});
    Expect(state.phase == Phase::Closed && !actions.requestOpen,
           "a held F9 sample must not bypass release-to-arm");
    actions = Advance(&state, Event{EventKind::F9Sample, false, 0});
    Expect(state.phase == Phase::Closed && !state.releaseRequired && !state.f9WasDown,
           "an observed F9 release must arm exactly one later rising edge");

    actions = Advance(&state, Event{EventKind::F9Sample, true, 0});
    Expect(state.phase == Phase::Editing && actions.requestOpen && !actions.requestClose &&
               state.releaseRequired,
           "an armed F9 rising edge must open the editor and consume the arm");
    actions = Advance(&state, Event{EventKind::F9Sample, true, 0});
    Expect(state.phase == Phase::Editing && !actions.requestOpen && !actions.requestClose,
           "a held F9 key must not retrigger an open editor");
    Advance(&state, Event{EventKind::F9Sample, false, 0});
    actions = Advance(&state, Event{EventKind::F9Sample, true, 0});
    Expect(state.phase == Phase::Closed && actions.requestClose && !actions.requestOpen,
           "the next armed F9 rising edge must close the editor");

    Advance(&state, Event{EventKind::F9Sample, false, 0});
    Advance(&state, Event{EventKind::F9Sample, true, 0});
    actions = Advance(&state, Event{EventKind::Submit, false, 0});
    Expect(state.phase == Phase::Requesting && state.generation == 1 &&
               actions.startRequest && !state.cancelRequested,
           "submitting from the editor must start generation one");
    actions = Advance(&state, Event{EventKind::Close, false, 0});
    Expect(state.phase == Phase::Closed && state.cancelRequested && actions.requestCancel &&
               actions.requestClose,
           "closing during a request must close locally and request cancellation");
    actions = Advance(&state, Event{EventKind::Complete, false, 1});
    Expect(state.phase == Phase::Closed && !actions.acceptCompletion,
           "a completion after the menu closes must not overwrite closed state");

    Advance(&state, Event{EventKind::F9Sample, false, 0});
    Advance(&state, Event{EventKind::F9Sample, true, 0});
    actions = Advance(&state, Event{EventKind::Submit, false, 0});
    Expect(state.phase == Phase::Requesting && state.generation == 2 &&
               actions.startRequest && !state.cancelRequested,
           "retry after close must own a new request generation");
    actions = Advance(&state, Event{EventKind::Complete, false, 1});
    Expect(state.phase == Phase::Requesting && !actions.acceptCompletion,
           "a stale generation must be rejected while a newer request is active");
    actions = Advance(&state, Event{EventKind::Complete, false, 2});
    Expect(state.phase == Phase::Answer && actions.acceptCompletion && !state.cancelRequested,
           "the matching active generation must be accepted as an answer");

    state.page = 1;
    state.pageCount = 3;
    actions = Advance(&state, Event{EventKind::AskAgain, false, 0});
    Expect(state.phase == Phase::Editing && state.page == 0 && state.pageCount == 0 &&
               state.failure == FailureKind::None && !actions.requestClose &&
               !actions.startRequest && state.generation == 2,
           "Enter on the answer screen must reopen editing without closing the menu");
    actions = Advance(&state, Event{EventKind::Submit, false, 0});
    Expect(state.phase == Phase::Requesting && state.generation == 3 && actions.startRequest,
           "a question asked after the answer must own a fresh generation");
    actions = Advance(&state, Event{EventKind::Complete, false, 3});
    Expect(state.phase == Phase::Answer && actions.acceptCompletion,
           "the follow-up answer must still complete normally");

    Advance(&state, Event{EventKind::Close, false, 0});
    Advance(&state, Event{EventKind::F9Sample, false, 0});
    Advance(&state, Event{EventKind::F9Sample, true, 0});
    Advance(&state, Event{EventKind::Submit, false, 0});
    actions = Advance(&state, Event{EventKind::Fail, false, 2,
                                    FailureKind::RateLimited});
    Expect(state.phase == Phase::Requesting &&
               state.failure == FailureKind::None && !actions.acceptCompletion,
           "a stale failure must not replace the active request or its error family");
    actions = Advance(&state, Event{EventKind::Fail, false, 4,
                                    FailureKind::InvalidQuestion});
    Expect(state.phase == Phase::Error &&
               state.failure == FailureKind::InvalidQuestion &&
               actions.acceptCompletion,
           "a matching failure must preserve its deterministic user-visible error family");
    actions = Advance(&state, Event{EventKind::Submit, false, 0});
    Expect(state.phase == Phase::Requesting && state.generation == 5 && actions.startRequest,
           "submit from an error must retry with a fresh generation");
    actions = Advance(&state, Event{EventKind::Stop, false, 0});
    Expect(state.phase == Phase::Stopping && actions.requestCancel && actions.requestClose &&
               state.cancelRequested,
           "normal-context stop must close and cancel an active request");

    State wrapped{};
    wrapped.phase = Phase::Editing;
    wrapped.generation = UINT32_MAX;
    actions = Advance(&wrapped, Event{EventKind::Submit, false, 0});
    Expect(wrapped.phase == Phase::Requesting && wrapped.generation == 1 &&
               actions.startRequest,
           "generation overflow must skip zero and resume at one");
    actions = Advance(&wrapped, Event{EventKind::Complete, false, 0});
    Expect(wrapped.phase == Phase::Requesting && !actions.acceptCompletion,
           "generation zero must never identify a request completion");
    actions = Advance(&wrapped, Event{EventKind::SetEnabled, false, 0});
    Expect(wrapped.phase == Phase::Disabled && actions.requestCancel && actions.requestClose,
           "disabling during a request must cancel and close before becoming inert");

    Expect(ClampPage(-1, 3) == 0 && ClampPage(0, 3) == 0 && ClampPage(2, 3) == 2 &&
               ClampPage(3, 3) == 2 && ClampPage(200, 3) == 2 && ClampPage(7, 0) == 0,
           "page selection must clamp to the actual zero-based page range");

    State externallyClosed{};
    externallyClosed.phase = Phase::Editing;
    externallyClosed.f9WasDown = false;
    externallyClosed.releaseRequired = false;
    actions = Advance(&externallyClosed, Event{EventKind::Close, false, 0});
    Expect(externallyClosed.phase == Phase::Closed && !externallyClosed.releaseRequired &&
               actions.requestClose,
           "non-F9 close must preserve an already observed F9 release");
    actions = Advance(&externallyClosed, Event{EventKind::F9Sample, true, 0});
    Expect(externallyClosed.phase == Phase::Editing && actions.requestOpen &&
               externallyClosed.releaseRequired,
           "an externally closed editor with an observed release must reopen on the next F9 edge");

    State f9Closed{};
    f9Closed.phase = Phase::Editing;
    f9Closed.f9WasDown = false;
    f9Closed.releaseRequired = false;
    actions = Advance(&f9Closed, Event{EventKind::F9Sample, true, 0});
    Expect(f9Closed.phase == Phase::Closed && f9Closed.f9WasDown &&
               f9Closed.releaseRequired && actions.requestClose,
           "F9 close while held must consume the edge and require a release");
    actions = Advance(&f9Closed, Event{EventKind::F9Sample, true, 0});
    Expect(f9Closed.phase == Phase::Closed && !actions.requestOpen,
           "held F9 after an F9 close must not reopen the editor");
    Advance(&f9Closed, Event{EventKind::F9Sample, false, 0});
    actions = Advance(&f9Closed, Event{EventKind::F9Sample, true, 0});
    Expect(f9Closed.phase == Phase::Editing && actions.requestOpen,
           "F9 close must rearm only after a later release and rising edge");
}

void TestMaechenForegroundInputGate() {
    using namespace FfxHooks::Maechen;

    FocusedEdgeState f9{};
    Expect(!ConsumeFocusedRisingEdge(&f9, false, true) &&
               f9.releaseRequired && !f9.wasDown,
           "background F9 must be rejected without becoming an observed key sample");
    Expect(!ConsumeFocusedRisingEdge(&f9, true, true) &&
               f9.releaseRequired && f9.wasDown,
           "F9 held across foreground regain must remain behind the release barrier");
    Expect(!ConsumeFocusedRisingEdge(&f9, true, false) &&
               !f9.releaseRequired && !f9.wasDown,
           "a focused physical release must re-arm the F9 edge");
    Expect(ConsumeFocusedRisingEdge(&f9, true, true) &&
               f9.releaseRequired && f9.wasDown,
           "only a fresh focused F9 rising edge may activate Maechen");
    Expect(!ConsumeFocusedRisingEdge(&f9, false, false) &&
               f9.releaseRequired && !f9.wasDown,
           "focus loss must reset F9 edge state and restore the release barrier");

    ForegroundInputGate pump{};
    Expect(ObserveForegroundInput(&pump, false) == ForegroundInputDecision::Blocked,
           "background editor input must be blocked without sampling text or navigation keys");
    Expect(ObserveForegroundInput(&pump, true) == ForegroundInputDecision::Prime,
           "the first foreground pump after focus loss must prime held-key state only");
    Expect(ObserveForegroundInput(&pump, true) == ForegroundInputDecision::Sample,
           "a later continuously focused pump may sample editor input");
    Expect(ObserveForegroundInput(&pump, false) == ForegroundInputDecision::Blocked &&
               ObserveForegroundInput(&pump, true) == ForegroundInputDecision::Prime,
           "each focus-loss cycle must restore the held-key regain barrier");

    struct FocusLossCase {
        Phase phase;
        bool expectClose;
        bool expectCancel;
    };
    const FocusLossCase cases[] = {
        {Phase::Closed, false, false},
        {Phase::Editing, true, false},
        {Phase::Requesting, true, true},
        {Phase::Answer, true, false},
        {Phase::Error, true, false},
    };
    for (const FocusLossCase& item : cases) {
        State state{};
        state.phase = item.phase;
        state.f9WasDown = true;
        state.releaseRequired = false;
        state.generation = 17;
        state.page = 2;
        state.pageCount = 3;
        const Actions actions = Advance(&state, Event{EventKind::FocusLost});
        Expect(state.phase == Phase::Closed && !state.f9WasDown &&
                   state.releaseRequired && actions.requestClose == item.expectClose &&
                   actions.requestCancel == item.expectCancel,
               "focus loss must close only Maechen, cancel only its pending request, and reset F9 ownership");
    }
}

bool SameMaechenState(const FfxHooks::Maechen::State& left,
                      const FfxHooks::Maechen::State& right) {
    return left.phase == right.phase && left.f9WasDown == right.f9WasDown &&
           left.releaseRequired == right.releaseRequired &&
           left.cancelRequested == right.cancelRequested &&
           left.generation == right.generation && left.page == right.page &&
           left.pageCount == right.pageCount && left.failure == right.failure;
}

bool NoMaechenActions(const FfxHooks::Maechen::Actions& actions) {
    return !actions.requestOpen && !actions.requestClose && !actions.startRequest &&
           !actions.requestCancel && !actions.acceptCompletion;
}

void TestMaechenStoppingIsAbsorbing() {
    using namespace FfxHooks::Maechen;

    State stopped{};
    stopped.phase = Phase::Stopping;
    stopped.f9WasDown = false;
    stopped.releaseRequired = true;
    stopped.cancelRequested = true;
    stopped.generation = 77;
    stopped.page = 2;
    stopped.pageCount = 4;
    const std::array<Event, 10> events = {
        Event{EventKind::SetEnabled, true, 0}, Event{EventKind::SetEnabled, false, 0},
        Event{EventKind::F9Sample, true, 0},   Event{EventKind::F9Sample, false, 0},
        Event{EventKind::Submit, false, 0},   Event{EventKind::Complete, false, 77},
        Event{EventKind::Fail, false, 77},    Event{EventKind::Close, false, 0},
        Event{EventKind::FocusLost, false, 0}, Event{EventKind::Stop, false, 0},
    };
    for (const Event& event : events) {
        State candidate = stopped;
        const Actions stoppedActions = Advance(&candidate, event);
        Expect(SameMaechenState(candidate, stopped) && NoMaechenActions(stoppedActions),
               "Stopping must absorb every current event without state or action changes");
    }
}

void TestMaechenSerializationContract() {
    using namespace FfxHooks::Maechen;

    char output[4096]{};
    size_t outputLength = 999;
    SerializeResult result =
        SerializeRequest("pt", "Where do I go next?", output, sizeof(output), &outputLength);
    Expect(result == SerializeResult::Ok && outputLength == 60 &&
               strcmp(output,
                      "{\"version\":1,\"locale\":\"pt\",\"question\":\"Where do I go next?\"}") == 0,
           "request serialization must match the literal protocol-v1 JSON fixture");

    result = SerializeRequest("en", "Say \"A\\B\"", output, sizeof(output), &outputLength);
    Expect(result == SerializeResult::Ok &&
               strcmp(output,
                      "{\"version\":1,\"locale\":\"en\",\"question\":\"Say \\\"A\\\\B\\\"\"}") == 0,
           "request serialization must escape quote and backslash bytes exactly once");
    result = SerializeRequest("pt", "  Hi  ", output, sizeof(output), &outputLength);
    Expect(result == SerializeResult::Ok &&
               strcmp(output, "{\"version\":1,\"locale\":\"pt\",\"question\":\"Hi\"}") == 0,
           "question bounds must apply after trimming outer ASCII spaces");

    const char* locales[] = {"pt", "en", "es", "fr", "it", "de"};
    for (const char* locale : locales) {
        Expect(SerializeRequest(locale, "A", output, sizeof(output), &outputLength) ==
                   SerializeResult::Ok,
               "each of the six protocol-v1 locales must serialize");
    }
    Expect(SerializeRequest("ja", "A", output, sizeof(output), &outputLength) ==
               SerializeResult::InvalidLocale &&
               SerializeRequest("ko", "A", output, sizeof(output), &outputLength) ==
                   SerializeResult::InvalidLocale &&
               SerializeRequest("zh-TW", "A", output, sizeof(output), &outputLength) ==
                   SerializeResult::InvalidLocale,
           "unadvertised renderer locales must be rejected");

    constexpr char kExactAtlas[] =
        "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    Expect(SerializeRequest("pt", kExactAtlas, output, sizeof(output), &outputLength) ==
               SerializeResult::Ok,
           "every exact FFX native-atlas byte must be accepted");
    Expect(SerializeRequest("pt", "@", output, sizeof(output), &outputLength) ==
               SerializeResult::InvalidQuestionCharacter,
           "at-sign outside the exact FFX atlas must be rejected");
    Expect(SerializeRequest("pt", "line\nbreak", output, sizeof(output), &outputLength) ==
               SerializeResult::InvalidQuestionCharacter,
           "control bytes must be rejected before JSON serialization");
    const char nonAsciiQuestion[] = {static_cast<char>(0xC3), static_cast<char>(0xA9), '\0'};
    Expect(SerializeRequest("pt", nonAsciiQuestion, output, sizeof(output), &outputLength) ==
               SerializeResult::InvalidQuestionCharacter,
           "non-ASCII question bytes must be rejected by the native-atlas gate");

    Expect(SerializeRequest("pt", "", output, sizeof(output), &outputLength) ==
               SerializeResult::EmptyQuestion &&
               SerializeRequest("pt", "   ", output, sizeof(output), &outputLength) ==
                   SerializeResult::EmptyQuestion,
           "empty and trim-empty questions must be rejected");
    std::string question1024(1024, 'A');
    std::string question1025(1025, 'A');
    std::array<char, 1066> exactCapacity{};
    outputLength = 0;
    Expect(SerializeRequest("pt", question1024.c_str(), exactCapacity.data(),
                            exactCapacity.size(), &outputLength) == SerializeResult::Ok &&
               outputLength == 1065 && exactCapacity[1065] == '\0',
           "a 1024-byte question must fit its exact independently derived output capacity");
    std::array<char, 1065> oneByteShort{};
    Expect(SerializeRequest("pt", question1024.c_str(), oneByteShort.data(),
                            oneByteShort.size(), &outputLength) ==
               SerializeResult::OutputTooSmall,
           "serialization must reserve one trailing NUL beyond the complete JSON body");
    Expect(SerializeRequest("pt", question1025.c_str(), output, sizeof(output), &outputLength) ==
               SerializeResult::QuestionTooLong,
           "a 1025-byte question must be rejected");
    const std::string padded1024 = std::string(" ") + question1024 + " ";
    outputLength = 0;
    Expect(SerializeRequest("pt", padded1024.c_str(), exactCapacity.data(),
                            exactCapacity.size(), &outputLength) == SerializeResult::Ok &&
               outputLength == 1065,
           "outer ASCII spaces must be trimmed before accepting a 1024-byte question");
    const std::string padded1025 = std::string(" ") + question1025 + " ";
    Expect(SerializeRequest("pt", padded1025.c_str(), output, sizeof(output), &outputLength) ==
               SerializeResult::QuestionTooLong,
           "outer ASCII spaces must be trimmed before rejecting a 1025-byte question");

    outputLength = 77;
    Expect(SerializeRequest("pt", "A", nullptr, sizeof(output), &outputLength) ==
               SerializeResult::NullOutput &&
               outputLength == 0,
           "a null output buffer must fail closed and clear the reported length");
    Expect(SerializeRequest(nullptr, "A", output, sizeof(output), &outputLength) ==
               SerializeResult::InvalidLocale &&
               SerializeRequest("pt", nullptr, output, sizeof(output), &outputLength) ==
                   SerializeResult::EmptyQuestion,
           "null locale and question inputs must map to deterministic validation results");
}

std::string MakeMaechenLines(size_t lineCount, size_t columns, char fill) {
    std::string text;
    for (size_t line = 0; line < lineCount; ++line) {
        if (line != 0) text.push_back('\n');
        text.append(columns, fill);
    }
    return text;
}

void TestMaechenResponseValidationContract() {
    using namespace FfxHooks::Maechen;

    const uint8_t valid[] = "Hello Maechen\nWorld";
    const size_t validLength = sizeof(valid) - 1;
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", nullptr, "1", valid,
                            validLength) == ResponseResult::Ok,
           "the exact status, metadata, and atlas body must validate");
    Expect(ValidateResponse(200, "TEXT/PLAIN ; CHARSET=UTF-8", "IDENTITY", "1", valid,
                            validLength) == ResponseResult::Ok &&
               ValidateResponse(200, "text/plain;charset=utf-8", "", "1", valid,
                                validLength) == ResponseResult::Ok,
           "content type must be case-insensitive with only optional semicolon spaces");
    Expect(ValidateResponse(201, "text/plain; charset=utf-8", "identity", "1", valid,
                            validLength) == ResponseResult::WrongStatus,
           "a non-200 HTTP status must never expose its response body");
    Expect(ValidateResponse(200, "application/json", "identity", "1", valid,
                            validLength) == ResponseResult::WrongContentType &&
               ValidateResponse(200, "text/event-stream", "identity", "1", valid,
                                validLength) == ResponseResult::WrongContentType,
           "JSON and SSE content types must not be accepted as text protocol success");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "gzip", "1", valid,
                            validLength) == ResponseResult::WrongContentEncoding,
           "compressed responses must be rejected instead of entering bounded text buffers");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "2", valid,
                            validLength) == ResponseResult::WrongProtocol &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", nullptr,
                                valid, validLength) == ResponseResult::WrongProtocol,
           "missing or mismatched protocol metadata must fail closed");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1", valid, 0) ==
               ResponseResult::EmptyBody,
           "an empty 200 body must not become an empty answer page");

    std::string body4096;
    for (size_t line = 0; line < 71; ++line) {
        body4096.append(56, 'A');
        body4096.push_back('\n');
    }
    body4096.append(49, 'B');
    Expect(body4096.size() == 4096,
           "the hand-derived response fixture must exercise exactly 4096 bytes");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(body4096.data()),
                            body4096.size()) == ResponseResult::Ok,
           "an exact 4096-byte response within line bounds must validate");
    body4096.push_back('C');
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(body4096.data()),
                            body4096.size()) == ResponseResult::BodyTooLarge,
           "a 4097-byte response must be rejected before body parsing");

    const std::string line56(56, 'A');
    const std::string line57(57, 'A');
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(line56.data()), line56.size()) ==
               ResponseResult::Ok,
           "a canonical 56-column server line must validate");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(line57.data()), line57.size()) ==
               ResponseResult::InvalidAscii,
           "a 57-character server line must be rejected rather than wrapped as canonical data");
    const std::string lines78 = MakeMaechenLines(78, 1, 'A');
    const std::string lines79 = MakeMaechenLines(79, 1, 'A');
    const std::string lines72 = MakeMaechenLines(72, 1, 'A');
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(lines72.data()), lines72.size()) ==
               ResponseResult::Ok,
           "exactly 72 canonical server lines must validate");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(lines78.data()), lines78.size()) ==
               ResponseResult::InvalidAscii,
           "a remote response must reject the 73rd through 78th lines despite local page capacity");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(lines79.data()), lines79.size()) ==
               ResponseResult::InvalidAscii,
           "a 79th response line must be rejected before pagination");
    const std::string terminal6 = MakeMaechenLines(6, 1, 'A') + "\n";
    const std::string terminal72 = lines72 + "\n";
    const std::string terminal78 = lines78 + "\n";
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(terminal6.data()),
                            terminal6.size()) == ResponseResult::Ok &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                                reinterpret_cast<const uint8_t*>(terminal72.data()),
                                terminal72.size()) == ResponseResult::Ok &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                                reinterpret_cast<const uint8_t*>(terminal78.data()),
                                terminal78.size()) == ResponseResult::InvalidAscii,
           "terminal LF must not synthesize a 7th or 73rd remote response line");

    const uint8_t atSign[] = {'A', '@', 'B'};
    const uint8_t carriageReturn[] = {'A', '\r', 'B'};
    const uint8_t embeddedNul[] = {'A', 0, 'B'};
    const uint8_t nonAscii[] = {'A', 0x80, 'B'};
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1", atSign,
                            sizeof(atSign)) == ResponseResult::InvalidAscii &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                                carriageReturn, sizeof(carriageReturn)) ==
                   ResponseResult::InvalidAscii &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                                embeddedNul, sizeof(embeddedNul)) ==
                   ResponseResult::InvalidAscii &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1", nonAscii,
                                sizeof(nonAscii)) == ResponseResult::InvalidAscii,
           "non-atlas ASCII, controls, embedded NUL, and non-ASCII bytes must be rejected");
}

void TestMaechenReleasedProtocolRemoteLimits() {
    using namespace FfxHooks::Maechen;

    const std::string lines72 = MakeMaechenLines(72, 1, 'A');
    const std::string lines73 = MakeMaechenLines(73, 1, 'A');
    const uint8_t remoteTab[] = "Hello\tMaechen";
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                            reinterpret_cast<const uint8_t*>(lines72.data()), lines72.size()) ==
                   ResponseResult::Ok &&
               ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1",
                                reinterpret_cast<const uint8_t*>(lines73.data()), lines73.size()) ==
                   ResponseResult::InvalidAscii,
           "released protocol must reject a 73rd remote line after accepting exactly 72");
    Expect(ValidateResponse(200, "text/plain; charset=utf-8", "identity", "1", remoteTab,
                            sizeof(remoteTab) - 1) == ResponseResult::InvalidAscii,
           "released protocol must reject a remote TAB because the server canonicalizes it to a space");
}

void TestMaechenPaginationContract() {
    using namespace FfxHooks::Maechen;

    Pages pages{};
    constexpr char kSevenLines[] = "one\ntwo\nthree\nfour\nfive\nsix\nseven";
    Expect(Paginate(kSevenLines, sizeof(kSevenLines) - 1, &pages) &&
               pages.pageCount == 2 && pages.lineCount[0] == 6 &&
               pages.lineCount[1] == 1 && strcmp(pages.lines[0][0], "one") == 0 &&
               strcmp(pages.lines[0][5], "six") == 0 &&
               strcmp(pages.lines[1][0], "seven") == 0,
           "pagination must preserve canonical lines and pack exactly six per page");

    const std::string lines72 = MakeMaechenLines(72, 1, 'A');
    Expect(Paginate(lines72.data(), lines72.size(), &pages) && pages.pageCount == 12 &&
               pages.lineCount[11] == 6,
           "a canonical 72-line server answer must occupy exactly twelve pages");
    const std::string lines78 = MakeMaechenLines(78, 1, 'B');
    Expect(Paginate(lines78.data(), lines78.size(), &pages) && pages.pageCount == 13 &&
               pages.lineCount[12] == 6,
           "the defensive 78-line client capacity must occupy exactly thirteen pages");

    struct GuardedPages {
        uint32_t before = 0x12345678u;
        Pages value{};
        uint32_t after = 0x89ABCDEFu;
    } guarded;
    const std::string lines79 = MakeMaechenLines(79, 1, 'C');
    Expect(!Paginate(lines79.data(), lines79.size(), &guarded.value) &&
               guarded.before == 0x12345678u && guarded.after == 0x89ABCDEFu &&
               guarded.value.pageCount == 0,
           "pagination must reject a fourteenth page without touching surrounding storage");

    const std::string exactLine(56, 'D');
    Expect(Paginate(exactLine.data(), exactLine.size(), &pages) && pages.pageCount == 1 &&
               pages.lineCount[0] == 1 && strlen(pages.lines[0][0]) == 56,
           "an exact 56-character local line must not be split");
    const std::string localLongWord(57, 'E');
    Expect(Paginate(localLongWord.data(), localLongWord.size(), &pages) &&
               pages.pageCount == 1 && pages.lineCount[0] == 2 &&
               strlen(pages.lines[0][0]) == 56 && strcmp(pages.lines[0][1], "E") == 0,
           "local error text must defensively hard-split a 57-character word");

    const std::string terminal6 = MakeMaechenLines(6, 1, 'A') + "\n";
    Expect(Paginate(terminal6.data(), terminal6.size(), &pages) && pages.pageCount == 1 &&
               pages.lineCount[0] == 6,
           "terminal LF after six lines must not create a seventh display line or page");
    const std::string terminal72 = MakeMaechenLines(72, 1, 'B') + "\n";
    Expect(Paginate(terminal72.data(), terminal72.size(), &pages) &&
               pages.pageCount == 12 && pages.lineCount[11] == 6,
           "terminal LF after 72 lines must retain exactly twelve full pages");
    const std::string terminal78 = MakeMaechenLines(78, 1, 'C') + "\n";
    Expect(Paginate(terminal78.data(), terminal78.size(), &pages) &&
               pages.pageCount == 13 && pages.lineCount[12] == 6,
           "terminal LF after 78 lines must retain exactly thirteen full pages");

    constexpr char kInteriorBlank[] = "A\n\nB\n";
    Expect(Paginate(kInteriorBlank, sizeof(kInteriorBlank) - 1, &pages) &&
               pages.pageCount == 1 && pages.lineCount[0] == 3 &&
               strcmp(pages.lines[0][0], "A") == 0 && pages.lines[0][1][0] == '\0' &&
               strcmp(pages.lines[0][2], "B") == 0,
           "interior consecutive LF must preserve one explicit blank display line");

    constexpr size_t kMaxRepresentablePageBytes = 13u * 6u * (56u + 1u);
    std::string maximumPayload = MakeMaechenLines(78, 56, 'D') + "\n";
    Expect(maximumPayload.size() == kMaxRepresentablePageBytes &&
               Paginate(maximumPayload.data(), maximumPayload.size(), &pages) &&
               pages.pageCount == 13 && pages.lineCount[12] == 6,
           "the exact maximum fixed-page payload with terminal LF must paginate");
    maximumPayload.push_back('E');
    Expect(!Paginate(maximumPayload.data(), maximumPayload.size(), &pages) &&
               pages.pageCount == 0,
           "pagination must reject a length above fixed-page capacity before exposing pages");

    std::string coreSource;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenCore.cpp"), coreSource),
           "Maechen portable core source must be readable for traversal-bound proof");
    const size_t lengthGate = coreSource.find("if (length > kMaxRepresentablePageBytes)");
    const size_t traversal = coreSource.find("for (size_t cursor = 0; cursor < length;");
    Expect(lengthGate != std::string::npos && traversal != std::string::npos &&
               lengthGate < traversal,
           "pagination length rejection must occur before any payload traversal");
    Expect(coreSource.find("cursor <= length") == std::string::npos,
           "pagination traversal must not use a cursor-less-than-or-equal length wrap pattern");
}

void TestMaechenLocalDefensivePageCapacity() {
    using namespace FfxHooks::Maechen;

    Pages pages{};
    const std::string localLines78 = MakeMaechenLines(78, 1, 'L');
    Expect(Paginate(localLines78.data(), localLines78.size(), &pages) && pages.pageCount == 13 &&
               pages.lineCount[12] == 6,
           "local defensive text capacity must remain thirteen pages even though remote success stops at 72 lines");
}

void TestMaechenOfficialHostNoFallbackAndIdentityHeaders() {
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), source),
           "Maechen adapter source must be readable for the released-origin contract");
    Expect(source.find("L\"ffxmodstudio.com\"") != std::string::npos &&
               source.find("ffx-mod-website.vercel.app") == std::string::npos &&
               source.find("ffx-mod-studio.web.app") == std::string::npos,
           "native transport must hardcode only the official host with no technical-host fallback");
    const std::string exactHeaders =
        "L\"Content-Type: application/json; charset=utf-8\\r\\n\"\n"
        "    L\"Accept: text/plain\\r\\n\"\n"
        "    L\"Accept-Encoding: identity\\r\\n\"\n"
        "    L\"X-Maechen-Protocol: 1\\r\\n\";";
    Expect(source.find(exactHeaders) != std::string::npos,
           "native transport must retain the exact identity and protocol request headers");
}

void TestSourceContractReaderTreatsLfAndCrlfAlike() {
    const std::string lfSignature =
        "bool RewardBeginCodeWrite(\n    void*, uintptr_t address, size_t length,\n"
        "    FfxHooks::F8Runtime::PatchProtectionToken* token)";
    std::string source;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\UnXBoosterHook.cpp"), source),
           "UnX booster source must be readable for the newline-agnostic source contract");
    const SourceBlock lf = SourceFunctionBody(lfSignature + " { return true; }", lfSignature.c_str());
    const SourceBlock checkout = SourceFunctionBody(source, lfSignature.c_str());
    Expect(lf.Valid() && checkout.Valid(),
           "source-contract parsing must treat LF fixtures and CRLF checkouts as the same signature");
}

struct FakeMaechenTransport {
    std::mutex mutex;
    std::condition_variable condition;
    uintptr_t nextHandle = 0x10000u;
    uintptr_t latestCancelHandle = 0;
    std::unordered_map<uintptr_t, HANDLE> cancelEvents;
    std::unordered_map<uintptr_t, std::string> transportHandles;
    std::vector<std::string> operations;
    std::vector<uint32_t> readCapacities;
    std::wstring userAgent;
    std::wstring host;
    std::wstring method;
    std::wstring path;
    std::wstring headers;
    std::string requestBody;
    uint16_t port = 0;
    uint32_t requestFlags = 0;
    uint32_t disableFeatures = 0;
    int resolveTimeout = 0;
    int connectTimeout = 0;
    int sendTimeout = 0;
    int receiveTimeout = 0;
    uint32_t mainThreadId = 0;
    uint32_t workerThreadId = 0;
    uint32_t status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string protocol = "1";
    std::string contentEncoding = "identity";
    std::string responseBody = "Hello\nWorld";
    size_t responseOffset = 0;
    uint32_t readChunk = 257;
    uint32_t readCalls = 0;
    uint32_t sendCalls = 0;
    uint32_t receiveCalls = 0;
    uint32_t statusQueries = 0;
    uint32_t contentTypeQueries = 0;
    uint32_t protocolQueries = 0;
    uint32_t contentEncodingQueries = 0;
    uint32_t cancelProbes = 0;
    uint32_t cancelAtProbe = 0;
    uint32_t cancelAfterRead = 0;
    bool cancelAfterReceive = false;
    bool missingContentType = false;
    bool missingProtocol = false;
    bool missingContentEncoding = false;
    bool sendFails = false;
    bool receiveFails = false;
    bool statusFails = false;
    bool readFails = false;
    uint32_t failureError = 0;
    bool blockReceive = false;
    bool receiveEntered = false;
    bool releaseReceive = false;
    bool transportFinished = false;
    uint32_t workerCreates = 0;
    uint32_t workerCloses = 0;
    uint32_t liveWorkerCloseAttempts = 0;
    HANDLE latestWorkerHandle = nullptr;
    bool blockFirstWorkerPoll = false;
    bool workerPollEntered = false;
    bool releaseWorkerPoll = false;
    bool workerCreateFails = false;
    bool cancelCloseFailsOnce = false;
    uint32_t cancelCloseFailures = 0;
    bool cancelManualReset = false;
    bool cancelInitiallySignaled = true;
};

std::mutex g_maechenLogMutex;
std::condition_variable g_maechenLogCondition;
std::vector<std::string> g_maechenLogs;
bool g_blockMaechenCompletionLog = false;
bool g_maechenCompletionLogEntered = false;
bool g_releaseMaechenCompletionLog = false;

struct NativeReservationPauseState {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
};

void PauseAfterNativeRequestConsumed(void* context) {
    auto* state = static_cast<NativeReservationPauseState*>(context);
    if (!state) return;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->entered = true;
    state->condition.notify_all();
    state->condition.wait(lock, [state]() { return state->release; });
}

void FakeMaechenLog(const char* message) {
    std::unique_lock<std::mutex> lock(g_maechenLogMutex);
    if (g_blockMaechenCompletionLog && message &&
        strstr(message, "request finished") != nullptr) {
        g_maechenCompletionLogEntered = true;
        g_maechenLogCondition.notify_all();
        g_maechenLogCondition.wait(lock, []() {
            return g_releaseMaechenCompletionLog;
        });
    }
    g_maechenLogs.emplace_back(message ? message : "");
}

void TestNativeOpenReservationHasNoFalseIdleWindow() {
    volatile long openRequest = 1;
    volatile long ownerPublished = 0;
    long consumed = 0;
    NativeReservationPauseState pause{};
    FfxHooks::Maechen_TestSetNativeReservationPause(
        &PauseAfterNativeRequestConsumed, &pause);

    std::thread pump([&]() {
        consumed = FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &openRequest, 0, &ownerPublished);
    });
    {
        std::unique_lock<std::mutex> lock(pause.mutex);
        Expect(pause.condition.wait_for(lock, std::chrono::seconds(5), [&pause]() {
                   return pause.entered;
               }),
               "native reservation helper must reach the deterministic post-consume barrier");
    }

    const bool presentObservesOtherOwner =
        InterlockedCompareExchange(&openRequest, 0, 0) != 0 ||
        InterlockedCompareExchange(&ownerPublished, 0, 0) != 0;
    const LONG maechenReservationResult =
        InterlockedCompareExchange(&ownerPublished, 2, 0);
    Expect(openRequest == 0 && ownerPublished == 1 && presentObservesOtherOwner &&
               maechenReservationResult == 1,
           "after request consumption Present must still observe native ownership and F9 must not reserve a second modal owner");

    {
        std::lock_guard<std::mutex> lock(pause.mutex);
        pause.release = true;
        pause.condition.notify_all();
    }
    pump.join();
    FfxHooks::Maechen_TestSetNativeReservationPause(nullptr, nullptr);
    Expect(consumed == 1 && openRequest == 0 && ownerPublished == 1,
           "reservation must remain published through the allocation handoff");
}

uintptr_t NewFakeMaechenHandle(FakeMaechenTransport& fake, const char* label) {
    const uintptr_t handle = fake.nextHandle++;
    fake.transportHandles.emplace(handle, label);
    return handle;
}

HANDLE FindFakeCancelHandle(FakeMaechenTransport& fake, uintptr_t logicalHandle) {
    const auto found = fake.cancelEvents.find(logicalHandle);
    return found == fake.cancelEvents.end() ? nullptr : found->second;
}

void SignalLatestFakeCancelLocked(FakeMaechenTransport& fake) {
    const HANDLE event = FindFakeCancelHandle(fake, fake.latestCancelHandle);
    if (event) SetEvent(event);
}

FfxHooks::MaechenTestHandle FFXHOOKS_MAECHEN_TEST_CALL
FakeMaechenCreateCancel(void* context, bool manualReset, bool initiallySignaled) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    const HANDLE event = CreateEventW(nullptr, manualReset ? TRUE : FALSE,
                                      initiallySignaled ? TRUE : FALSE, nullptr);
    if (!event) return 0;
    std::lock_guard<std::mutex> lock(fake.mutex);
    const uintptr_t logicalHandle = fake.nextHandle++;
    fake.cancelEvents.emplace(logicalHandle, event);
    fake.latestCancelHandle = logicalHandle;
    fake.cancelManualReset = manualReset;
    fake.cancelInitiallySignaled = initiallySignaled;
    fake.operations.emplace_back("create-cancel");
    return logicalHandle;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenSignalCancel(
    void* context, FfxHooks::MaechenTestHandle logicalHandle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    HANDLE event = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        event = FindFakeCancelHandle(fake, logicalHandle);
        fake.operations.emplace_back("signal-cancel");
    }
    return event && SetEvent(event) != FALSE;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenIsCancelled(
    void* context, FfxHooks::MaechenTestHandle logicalHandle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    HANDLE event = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        event = FindFakeCancelHandle(fake, logicalHandle);
        ++fake.cancelProbes;
        fake.operations.emplace_back("probe-cancel");
        if (event && fake.cancelAtProbe != 0 && fake.cancelProbes == fake.cancelAtProbe) {
            SetEvent(event);
        }
    }
    return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

FfxHooks::MaechenTestHandle FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenCreateWorker(
    void* context, FfxHooks::MaechenTestWorkerProc workerProc, void* parameter) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        if (fake.workerCreateFails) {
            fake.operations.emplace_back("create-worker-failed");
            return 0;
        }
    }
    const HANDLE worker = CreateThread(nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(workerProc), parameter, 0, nullptr);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.operations.emplace_back("create-worker");
    if (worker) {
        ++fake.workerCreates;
        fake.latestWorkerHandle = worker;
    }
    return reinterpret_cast<uintptr_t>(worker);
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenIsWorkerComplete(
    void* context, FfxHooks::MaechenTestHandle workerHandle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    const DWORD wait = WaitForSingleObject(reinterpret_cast<HANDLE>(workerHandle), 0);
    std::unique_lock<std::mutex> lock(fake.mutex);
    fake.operations.emplace_back("poll-worker");
    if (fake.blockFirstWorkerPoll && !fake.workerPollEntered) {
        fake.workerPollEntered = true;
        fake.condition.notify_all();
        fake.condition.wait(lock, [&]() { return fake.releaseWorkerPoll; });
    }
    return wait == WAIT_OBJECT_0;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenCloseWorker(
    void* context, FfxHooks::MaechenTestHandle workerHandle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    const bool complete =
        WaitForSingleObject(reinterpret_cast<HANDLE>(workerHandle), 0) == WAIT_OBJECT_0;
    if (!complete) {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.operations.emplace_back("close-live-worker");
        ++fake.liveWorkerCloseAttempts;
        return false;
    }
    const bool closed = CloseHandle(reinterpret_cast<HANDLE>(workerHandle)) != FALSE;
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.operations.emplace_back("close-worker");
    if (closed) {
        ++fake.workerCloses;
        if (fake.latestWorkerHandle == reinterpret_cast<HANDLE>(workerHandle)) {
            fake.latestWorkerHandle = nullptr;
        }
    }
    return closed;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenCloseCancel(
    void* context, FfxHooks::MaechenTestHandle logicalHandle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    HANDLE event = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        event = FindFakeCancelHandle(fake, logicalHandle);
        if (!event) return false;
        if (fake.cancelCloseFailsOnce && fake.cancelCloseFailures == 0) {
            ++fake.cancelCloseFailures;
            fake.operations.emplace_back("close-cancel-failed");
            return false;
        }
        fake.cancelEvents.erase(logicalHandle);
        fake.operations.emplace_back("close-cancel");
    }
    return CloseHandle(event) != FALSE;
}

FfxHooks::MaechenTestHandle FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenOpenSession(
    void* context, const wchar_t* userAgent) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.workerThreadId = GetCurrentThreadId();
    fake.userAgent = userAgent ? userAgent : L"";
    fake.operations.emplace_back("open-session");
    return NewFakeMaechenHandle(fake, "session");
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenSetTimeouts(
    void* context, FfxHooks::MaechenTestHandle, int resolveTimeout, int connectTimeout,
    int sendTimeout, int receiveTimeout) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.resolveTimeout = resolveTimeout;
    fake.connectTimeout = connectTimeout;
    fake.sendTimeout = sendTimeout;
    fake.receiveTimeout = receiveTimeout;
    fake.operations.emplace_back("set-timeouts");
    return true;
}

FfxHooks::MaechenTestHandle FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenConnect(
    void* context, FfxHooks::MaechenTestHandle, const wchar_t* host, uint16_t port) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.host = host ? host : L"";
    fake.port = port;
    fake.operations.emplace_back("open-connect");
    return NewFakeMaechenHandle(fake, "connect");
}

FfxHooks::MaechenTestHandle FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenOpenRequest(
    void* context, FfxHooks::MaechenTestHandle, const wchar_t* method,
    const wchar_t* path, uint32_t flags) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.method = method ? method : L"";
    fake.path = path ? path : L"";
    fake.requestFlags = flags;
    fake.operations.emplace_back("open-request");
    return NewFakeMaechenHandle(fake, "request");
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenDisableFeatures(
    void* context, FfxHooks::MaechenTestHandle, uint32_t disableFeatures) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.disableFeatures = disableFeatures;
    fake.operations.emplace_back("disable-features");
    return true;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenSend(
    void* context, FfxHooks::MaechenTestHandle, const wchar_t* headers,
    uint32_t headerLength, const uint8_t* body, uint32_t bodyLength, uint32_t* errorOut) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    ++fake.sendCalls;
    fake.operations.emplace_back("send");
    fake.headers.assign(headers ? headers : L"", headerLength);
    fake.requestBody.assign(reinterpret_cast<const char*>(body), bodyLength);
    if (errorOut) *errorOut = fake.sendFails ? fake.failureError : 0;
    return !fake.sendFails;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenReceive(
    void* context, FfxHooks::MaechenTestHandle, uint32_t* errorOut) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::unique_lock<std::mutex> lock(fake.mutex);
    ++fake.receiveCalls;
    fake.operations.emplace_back("receive");
    fake.receiveEntered = true;
    fake.condition.notify_all();
    while (fake.blockReceive && !fake.releaseReceive) fake.condition.wait(lock);
    if (fake.cancelAfterReceive) SignalLatestFakeCancelLocked(fake);
    if (errorOut) *errorOut = fake.receiveFails ? fake.failureError : 0;
    return !fake.receiveFails;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenQueryStatus(
    void* context, FfxHooks::MaechenTestHandle, uint32_t* statusOut, uint32_t* errorOut) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    ++fake.statusQueries;
    fake.operations.emplace_back("query-status");
    if (statusOut) *statusOut = fake.status;
    if (errorOut) *errorOut = fake.statusFails ? fake.failureError : 0;
    return !fake.statusFails;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenQueryHeader(
    void* context, FfxHooks::MaechenTestHandle, FfxHooks::MaechenTestHeader header,
    char* output, uint32_t capacity, uint32_t* lengthOut, bool* missingOut,
    uint32_t* errorOut) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    const std::string* value = nullptr;
    bool missing = false;
    switch (header) {
        case FfxHooks::MaechenTestHeader::ContentType:
            ++fake.contentTypeQueries;
            fake.operations.emplace_back("query-content-type");
            value = &fake.contentType;
            missing = fake.missingContentType;
            break;
        case FfxHooks::MaechenTestHeader::Protocol:
            ++fake.protocolQueries;
            fake.operations.emplace_back("query-protocol");
            value = &fake.protocol;
            missing = fake.missingProtocol;
            break;
        case FfxHooks::MaechenTestHeader::ContentEncoding:
            ++fake.contentEncodingQueries;
            fake.operations.emplace_back("query-content-encoding");
            value = &fake.contentEncoding;
            missing = fake.missingContentEncoding;
            break;
    }
    if (missingOut) *missingOut = missing;
    if (errorOut) *errorOut = 0;
    if (missing || !value || capacity <= value->size()) return false;
    memcpy(output, value->data(), value->size());
    output[value->size()] = '\0';
    if (lengthOut) *lengthOut = static_cast<uint32_t>(value->size());
    return true;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenRead(
    void* context, FfxHooks::MaechenTestHandle, uint8_t* output, uint32_t capacity,
    uint32_t* readOut, uint32_t* errorOut) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    ++fake.readCalls;
    fake.readCapacities.push_back(capacity);
    fake.operations.emplace_back("read");
    if (fake.readFails) {
        if (readOut) *readOut = 0;
        if (errorOut) *errorOut = fake.failureError;
        return false;
    }
    const size_t remaining = fake.responseBody.size() - fake.responseOffset;
    const size_t count = (std::min)(remaining, static_cast<size_t>((std::min)(capacity, fake.readChunk)));
    if (count != 0) memcpy(output, fake.responseBody.data() + fake.responseOffset, count);
    fake.responseOffset += count;
    if (readOut) *readOut = static_cast<uint32_t>(count);
    if (errorOut) *errorOut = 0;
    if (fake.cancelAfterRead != 0 && fake.readCalls == fake.cancelAfterRead) {
        SignalLatestFakeCancelLocked(fake);
    }
    return true;
}

bool FFXHOOKS_MAECHEN_TEST_CALL FakeMaechenCloseTransport(
    void* context, FfxHooks::MaechenTestHandle handle) {
    auto& fake = *static_cast<FakeMaechenTransport*>(context);
    std::lock_guard<std::mutex> lock(fake.mutex);
    const auto found = fake.transportHandles.find(handle);
    if (found == fake.transportHandles.end()) return false;
    fake.operations.emplace_back(std::string("close-") + found->second);
    const bool session = found->second == "session";
    fake.transportHandles.erase(found);
    if (session) {
        fake.transportFinished = true;
        fake.condition.notify_all();
    }
    return true;
}

FfxHooks::MaechenTestOps MakeFakeMaechenOps(FakeMaechenTransport& fake) {
    FfxHooks::MaechenTestOps ops{};
    ops.context = &fake;
    ops.createCancelEvent = &FakeMaechenCreateCancel;
    ops.signalCancelEvent = &FakeMaechenSignalCancel;
    ops.isCancelSignaled = &FakeMaechenIsCancelled;
    ops.createWorker = &FakeMaechenCreateWorker;
    ops.isWorkerComplete = &FakeMaechenIsWorkerComplete;
    ops.closeWorker = &FakeMaechenCloseWorker;
    ops.closeCancelEvent = &FakeMaechenCloseCancel;
    ops.openSession = &FakeMaechenOpenSession;
    ops.setTimeouts = &FakeMaechenSetTimeouts;
    ops.connect = &FakeMaechenConnect;
    ops.openRequest = &FakeMaechenOpenRequest;
    ops.disableFeatures = &FakeMaechenDisableFeatures;
    ops.sendRequest = &FakeMaechenSend;
    ops.receiveResponse = &FakeMaechenReceive;
    ops.queryStatus = &FakeMaechenQueryStatus;
    ops.queryHeader = &FakeMaechenQueryHeader;
    ops.readData = &FakeMaechenRead;
    ops.closeTransport = &FakeMaechenCloseTransport;
    return ops;
}

void ResetFakeMaechen(FakeMaechenTransport& fake) {
    std::lock_guard<std::mutex> lock(fake.mutex);
    fake.operations.clear();
    fake.readCapacities.clear();
    fake.userAgent.clear();
    fake.host.clear();
    fake.method.clear();
    fake.path.clear();
    fake.headers.clear();
    fake.requestBody.clear();
    fake.port = 0;
    fake.requestFlags = 0;
    fake.disableFeatures = 0;
    fake.resolveTimeout = 0;
    fake.connectTimeout = 0;
    fake.sendTimeout = 0;
    fake.receiveTimeout = 0;
    fake.mainThreadId = GetCurrentThreadId();
    fake.workerThreadId = 0;
    fake.status = 200;
    fake.contentType = "text/plain; charset=utf-8";
    fake.protocol = "1";
    fake.contentEncoding = "identity";
    fake.responseBody = "Hello\nWorld";
    fake.responseOffset = 0;
    fake.readChunk = 257;
    fake.readCalls = 0;
    fake.sendCalls = 0;
    fake.receiveCalls = 0;
    fake.statusQueries = 0;
    fake.contentTypeQueries = 0;
    fake.protocolQueries = 0;
    fake.contentEncodingQueries = 0;
    fake.cancelProbes = 0;
    fake.cancelAtProbe = 0;
    fake.cancelAfterRead = 0;
    fake.cancelAfterReceive = false;
    fake.missingContentType = false;
    fake.missingProtocol = false;
    fake.missingContentEncoding = false;
    fake.sendFails = false;
    fake.receiveFails = false;
    fake.statusFails = false;
    fake.readFails = false;
    fake.failureError = 0;
    fake.blockReceive = false;
    fake.receiveEntered = false;
    fake.releaseReceive = false;
    fake.transportFinished = false;
    fake.liveWorkerCloseAttempts = 0;
    fake.latestWorkerHandle = nullptr;
    fake.blockFirstWorkerPoll = false;
    fake.workerPollEntered = false;
    fake.releaseWorkerPoll = false;
    fake.workerCreateFails = false;
    fake.cancelCloseFailsOnce = false;
    fake.cancelCloseFailures = 0;
    fake.cancelManualReset = false;
    fake.cancelInitiallySignaled = true;
}

bool ConfigureFakeMaechen(FakeMaechenTransport& fake) {
    ResetFakeMaechen(fake);
    const FfxHooks::MaechenTestOps ops = MakeFakeMaechenOps(fake);
    return FfxHooks::Maechen_TestInstallTransport(&ops);
}

bool PumpMaechenUntilIdle(int timeoutMilliseconds = 5000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMilliseconds);
    while (FfxHooks::Maechen_RequestBusy() && std::chrono::steady_clock::now() < deadline) {
        FfxHooks::Maechen_PumpTick(true);
        Sleep(1);
    }
    FfxHooks::Maechen_PumpTick(true);
    return !FfxHooks::Maechen_RequestBusy();
}

bool WaitForFakeMaechenFlag(FakeMaechenTransport& fake, bool* field,
                            int timeoutMilliseconds = 5000) {
    std::unique_lock<std::mutex> lock(fake.mutex);
    return WaitForConditionMillis(fake.condition, lock, timeoutMilliseconds,
                                  [&]() { return *field; });
}

bool OperationsContainInOrder(const std::vector<std::string>& operations,
                              std::initializer_list<const char*> expected) {
    size_t cursor = 0;
    for (const char* item : expected) {
        while (cursor < operations.size() && operations[cursor] != item) ++cursor;
        if (cursor == operations.size()) return false;
        ++cursor;
    }
    return true;
}

bool MaechenPagesEmpty(const FfxHooks::Maechen::Pages& pages) {
    if (pages.pageCount != 0) return false;
    for (uint8_t count : pages.lineCount) {
        if (count != 0) return false;
    }
    return true;
}

std::vector<unsigned char> EncodeExpectedMaechenAtlas(const char* ascii) {
    static constexpr char kAtlas[] =
        "0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    std::vector<unsigned char> encoded;
    for (const char* cursor = ascii; cursor && *cursor; ++cursor) {
        const char* found = strchr(kAtlas, *cursor);
        const size_t index = found ? static_cast<size_t>(found - kAtlas) : 10u;
        encoded.push_back(static_cast<unsigned char>(0x30u + index));
    }
    encoded.push_back(0);
    return encoded;
}

void TestMaechenPublishedErrorTaxonomy() {
    using FfxHooks::MaechenAdapterResult;
    struct ErrorCase {
        MaechenAdapterResult result;
        const char* expectedStatus;
    };
    const ErrorCase cases[] = {
        {MaechenAdapterResult::InvalidQuestion,
         "Question rejected. Edit and retry"},
        {MaechenAdapterResult::RateLimited,
         "Too many requests. Try again later"},
        {MaechenAdapterResult::RetryLater,
         "Service unavailable. Try again later"},
        {MaechenAdapterResult::Timeout,
         "Service unavailable. Try again later"},
    };

    uint32_t generation = 60;
    for (const ErrorCase& item : cases) {
        unsigned char published[64]{};
        const uint32_t activeGeneration = generation++;
        FfxHooks::Maechen_TestPrepareRequestingState(activeGeneration);
        Expect(FfxHooks::Maechen_TestApplyCompletionToUi(
                   item.result, activeGeneration) &&
                   FfxHooks::Maechen_TestCopyPublishedStatus(
                       published, sizeof(published)),
               "a matching failed completion must reach the real published UI snapshot");
        const std::vector<unsigned char> expected =
            EncodeExpectedMaechenAtlas(item.expectedStatus);
        Expect(expected.size() <= sizeof(published) &&
                   memcmp(published, expected.data(), expected.size()) == 0 &&
                   strlen(item.expectedStatus) <= FfxHooks::Maechen::kLineColumns,
               "each adapter failure family must publish its fixed native-atlas status within 56 columns");
    }

    unsigned char before[64]{};
    unsigned char after[64]{};
    FfxHooks::Maechen_TestPrepareRequestingState(90);
    Expect(FfxHooks::Maechen_TestCopyPublishedStatus(before, sizeof(before)),
           "the stale-generation check must begin with a published requesting snapshot");
    Expect(!FfxHooks::Maechen_TestApplyCompletionToUi(
               MaechenAdapterResult::InvalidQuestion, 89) &&
               FfxHooks::Maechen_TestCopyPublishedStatus(after, sizeof(after)) &&
               memcmp(before, after, sizeof(before)) == 0,
           "a stale failed completion must not replace the active generation's published snapshot");
}

void TestMaechenModalAllocationArbitration() {
    Expect(!FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "an idle closed Maechen client must allow the normal native modal path");
    Expect(FfxHooks::Maechen_TestPublishMenuOwnedForClose() &&
               FfxHooks::Maechen_MenuOwned() &&
               FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "Maechen pending or owned publication must synchronously block a second native modal allocation");
    FfxHooks::Maechen_TestCloseMenuFromPump();
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               !FfxHooks::Maechen_PumpWakePending() &&
               !FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "after a local close with no worker wake, normal F8/F7 allocation must be available again");
}

void TestMaechenForegroundAdapterLifecycle() {
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "the foreground adapter fixture must begin without Maechen ownership");
    FfxHooks::Maechen_TestPresentInput(false, true, false, false);
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "a physical F9 sample while backgrounded must not publish an open request");

    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "F9 held while foreground returns must remain blocked by release-to-arm");
    FfxHooks::Maechen_TestPresentInput(true, false, false, false);
    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    Expect(FfxHooks::Maechen_MenuOwned(),
           "a fresh focused F9 edge must publish exactly one Maechen open reservation");

    FfxHooks::Maechen_TestPresentInput(false, false, false, false);
    FfxHooks::Maechen_PumpTick(false);
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               !FfxHooks::Maechen_PumpWakePending(),
           "focus loss between Present publication and Pump allocation must cancel only the pending Maechen owner");
}

void TestMaechenQueuedFocusLossAndCoreOpenTransition() {
    using FfxHooks::Maechen::ForegroundInputDecision;
    using FfxHooks::Maechen::Phase;

    Expect(FfxHooks::Maechen_TestPublishMenuOwnedForClose(),
           "the queued-focus fixture must begin with an existing Maechen owner");
    FfxHooks::Maechen_NotifyForegroundLost();

    // Both callbacks resume after focus has already returned. Each must still
    // observe the queued loss independently instead of trusting current focus.
    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    FfxHooks::Maechen_PumpTick(true);
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               FfxHooks::Maechen_TestUiPhase() == Phase::Closed &&
               FfxHooks::Maechen_TestLastPumpInputDecision() ==
                   ForegroundInputDecision::Blocked,
           "queued WndProc loss must close Maechen when Present and Pump resume with foreground already true");

    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "F9 held through queued focus loss must not publish a resumed open");
    FfxHooks::Maechen_PumpTick(true);
    Expect(FfxHooks::Maechen_TestLastPumpInputDecision() ==
               ForegroundInputDecision::Prime,
           "the first resumed Pump must prime held text and navigation keys without emitting input");
    FfxHooks::Maechen_PumpTick(true);
    Expect(FfxHooks::Maechen_TestLastPumpInputDecision() ==
               ForegroundInputDecision::Sample,
           "text and navigation sampling may resume only after the held-key prime frame");

    FfxHooks::Maechen_TestPresentInput(true, false, false, false);
    FfxHooks::Maechen_PumpTick(true);
    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    Expect(FfxHooks::Maechen_MenuOwned(),
           "a physical release followed by a fresh focused F9 must reserve Maechen");

    FfxHooks::Maechen_TestSuppressNativeAllocation(true);
    FfxHooks::Maechen_PumpTick(true);
    FfxHooks::Maechen_TestSuppressNativeAllocation(false);
    Expect(FfxHooks::Maechen_TestUiPhase() == Phase::Editing &&
               FfxHooks::Maechen_MenuOwned(),
           "Pump must feed the physical release and enter Editing only when core explicitly requests open");

    FfxHooks::Maechen_TestPresentInput(false, false, false, false);
    FfxHooks::Maechen_PumpTick(false);
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "the queued-focus regression fixture must release its Maechen-only reservation");
}

void TestMaechenReleaseAfterPresentLossSurvivesLaggingPump() {
    using FfxHooks::Maechen::ForegroundInputDecision;
    using FfxHooks::Maechen::Phase;

    Expect(FfxHooks::Maechen_TestPublishMenuOwnedForClose(),
           "the lagging-Pump fixture must begin with an existing Maechen owner");
    FfxHooks::Maechen_NotifyForegroundLost();

    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    FfxHooks::Maechen_TestPresentInput(true, false, false, false);
    FfxHooks::Maechen_PumpTick(true);
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               FfxHooks::Maechen_TestUiPhase() == Phase::Closed &&
               FfxHooks::Maechen_TestLastPumpInputDecision() ==
                   ForegroundInputDecision::Blocked,
           "a lagging Pump must consume the queued loss after Present has already observed a newer focused release");

    FfxHooks::Maechen_PumpTick(true);
    Expect(FfxHooks::Maechen_TestLastPumpInputDecision() ==
               ForegroundInputDecision::Prime,
           "the lagging Pump must still prime held text and navigation state before input resumes");

    FfxHooks::Maechen_TestPresentInput(true, true, false, false);
    Expect(FfxHooks::Maechen_MenuOwned(),
           "the first fresh F9 after the epoch-correlated release must reserve Maechen");
    FfxHooks::Maechen_TestSuppressNativeAllocation(true);
    FfxHooks::Maechen_PumpTick(true);
    FfxHooks::Maechen_TestSuppressNativeAllocation(false);
    Expect(FfxHooks::Maechen_TestUiPhase() == Phase::Editing &&
               FfxHooks::Maechen_MenuOwned(),
           "the newer focused release must survive the lagging Pump so the first legitimate F9 reaches Editing");

    FfxHooks::Maechen_TestPresentInput(false, false, false, false);
    FfxHooks::Maechen_PumpTick(false);
    Expect(!FfxHooks::Maechen_MenuOwned(),
           "the lagging-Pump regression fixture must release its Maechen-only reservation");
}

void TestMaechenWhitespaceOnlySubmissionPublishesInvalidQuestion(
    FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake),
           "the whitespace-only submission scenario must configure while idle");
    uint32_t workerCreatesBefore = 0;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        workerCreatesBefore = fake.workerCreates;
    }

    MaechenAdapterResult rejection = MaechenAdapterResult::RetryLater;
    Expect(!FfxHooks::Maechen_SubmitRequest("pt", "     ", 61, &rejection) &&
               rejection == MaechenAdapterResult::InvalidQuestion &&
               !FfxHooks::Maechen_RequestBusy(),
           "trim-empty input must return InvalidQuestion without owning the request slot");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(fake.workerCreates == workerCreatesBefore && fake.sendCalls == 0 &&
                   fake.receiveCalls == 0 && fake.transportHandles.empty() &&
                   fake.cancelEvents.empty() && fake.operations.empty(),
               "trim-empty input must not create a worker, cancel event, or network handle");
    }

    FfxHooks::Maechen_TestPrepareRequestingState(61);
    unsigned char published[64]{};
    Expect(FfxHooks::Maechen_TestApplySubmitFailureToUi(rejection) &&
               FfxHooks::Maechen_TestCopyPublishedStatus(
                   published, sizeof(published)),
           "the real Pump submit-failure path must publish the local rejection");
    const std::vector<unsigned char> expected =
        EncodeExpectedMaechenAtlas("Question rejected. Edit and retry");
    Expect(expected.size() <= sizeof(published) &&
               memcmp(published, expected.data(), expected.size()) == 0,
           "spaces-only input must render the fixed InvalidQuestion status rather than RetryLater");
}

void TestDelayedArenaModalArbitrationRetainsOneOpen() {
    using FfxHooks::F8Ui::LegacyModalOwners;
    using FfxHooks::F8Ui::LegacyModalAllocationIdle;

    const LegacyModalOwners idle{};
    Expect(LegacyModalAllocationIdle(idle),
           "an owner-free post-Maechen pump state must allow one legacy modal allocation");

    LegacyModalOwners blocked = idle;
    blocked.maechenBlocks = true;
    Expect(!LegacyModalAllocationIdle(blocked),
           "Maechen menu ownership or terminal wake must block every legacy modal allocation");
    blocked = idle;
    blocked.f7MenuOwned = true;
    Expect(!LegacyModalAllocationIdle(blocked),
           "an existing F7 menu must block delayed or direct Arena allocation");
    blocked = idle;
    blocked.composeOwned = true;
    Expect(!LegacyModalAllocationIdle(blocked),
           "an active compose picker must block delayed or direct Arena allocation");
    blocked = idle;
    blocked.heldActionOwned = true;
    Expect(!LegacyModalAllocationIdle(blocked),
           "a held native action must block delayed or direct Arena allocation");

    volatile long delayedPending = 1;
    volatile long arenaWantOpen = 0;
    volatile long ownerPublished = 0;
    int allocations = 0;

    if (LegacyModalAllocationIdle(LegacyModalOwners{true}) &&
        FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &delayedPending, 0, &ownerPublished) != 0) {
        InterlockedExchange(&arenaWantOpen, 1);
    }
    Expect(delayedPending == 1 && arenaWantOpen == 0 && ownerPublished == 0,
           "a delayed Arena request must remain pending and unconsumed while Maechen blocks");

    if (LegacyModalAllocationIdle(idle) &&
        FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &delayedPending, 0, &ownerPublished) != 0) {
        InterlockedExchange(&arenaWantOpen, 1);
    }
    Expect(delayedPending == 0 && arenaWantOpen == 1 && ownerPublished == 1,
           "after Maechen fully releases, delayed promotion must reserve ownership before publishing Arena open");

    if (LegacyModalAllocationIdle(idle) &&
        FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &arenaWantOpen, 0, &ownerPublished) != 0) {
        ++allocations;
    }
    if (LegacyModalAllocationIdle(idle) &&
        FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
            &arenaWantOpen, 0, &ownerPublished) != 0) {
        ++allocations;
    }
    Expect(allocations == 1 && arenaWantOpen == 0 && ownerPublished == 1,
           "the retained delayed Arena request must allocate exactly once after release");
}

void TestBlockedArenaConfirmCannotFallThroughToAnotherModal() {
    using FfxHooks::F8Ui::ArenaOpenResult;
    using FfxHooks::F8Ui::LegacyModalAllocationIdle;
    using FfxHooks::F8Ui::LegacyModalOwners;
    using FfxHooks::F8Ui::TryHandleArenaOpenRequest;

    enum class FallbackKind : uint8_t { Sin, F7, Native };

    volatile long arenaWantOpen = 1;
    volatile long ownerPublished = 0;
    int arenaAllocations = 0;
    int sinAllocations = 0;
    int f7Allocations = 0;
    int nativeAllocations = 0;

    auto pumpConfirmStep = [&](const LegacyModalOwners& owners,
                               FallbackKind fallback) {
        const bool requestPending =
            InterlockedCompareExchange(&arenaWantOpen, 0, 0) != 0;
        const ArenaOpenResult result = TryHandleArenaOpenRequest(
            requestPending, LegacyModalAllocationIdle(owners), [&]() {
                if (FfxHooks::NativeMenu_ReserveAndConsumeOpenRequest(
                        &arenaWantOpen, 0, &ownerPublished) == 0) {
                    return false;
                }
                ++arenaAllocations;
                return true;
            });

        if (result == ArenaOpenResult::NoRequest) {
            if (fallback == FallbackKind::Sin) ++sinAllocations;
            else if (fallback == FallbackKind::F7) ++f7Allocations;
            else ++nativeAllocations;
        }
        return result;
    };

    LegacyModalOwners blocked{};
    blocked.maechenBlocks = true;
    Expect(pumpConfirmStep(blocked, FallbackKind::Sin) ==
               ArenaOpenResult::DeferredBlocked &&
               pumpConfirmStep(blocked, FallbackKind::F7) ==
               ArenaOpenResult::DeferredBlocked &&
               pumpConfirmStep(blocked, FallbackKind::Native) ==
               ArenaOpenResult::DeferredBlocked,
           "ACT_ARENA retained under a Maechen wake must report DeferredBlocked to every fallback branch");
    Expect(arenaWantOpen == 1 && ownerPublished == 0 &&
               arenaAllocations == 0 && sinAllocations == 0 &&
               f7Allocations == 0 && nativeAllocations == 0,
           "a blocked Arena request must remain intact without Arena, Sin, F7, or native fallback allocation");

    const LegacyModalOwners released{};
    Expect(pumpConfirmStep(released, FallbackKind::Native) ==
               ArenaOpenResult::Handled &&
               arenaWantOpen == 0 && ownerPublished == 1 &&
               arenaAllocations == 1 && nativeAllocations == 0,
           "after Maechen fully releases, the retained Arena request must consume and allocate exactly once");
    Expect(pumpConfirmStep(released, FallbackKind::Native) ==
               ArenaOpenResult::NoRequest &&
               arenaAllocations == 1 && nativeAllocations == 1,
           "an actually absent Arena request must still permit the existing fallback path");
}

FfxHooks::MaechenCompletion RunFakeMaechenRequest(FakeMaechenTransport& fake,
                                                  uint32_t generation,
                                                  const char* question = "Where next?") {
    (void)fake;
    FfxHooks::MaechenCompletion completion{};
    Expect(FfxHooks::Maechen_SubmitRequest("pt", question, generation),
           "an idle configured Maechen slot must accept one nonzero generation");
    Expect(FfxHooks::Maechen_RequestBusy(),
           "the Maechen slot must become busy as soon as it owns worker/cancel handles");
    Expect(PumpMaechenUntilIdle(), "the fake Maechen worker must complete and be reaped");
    Expect(FfxHooks::Maechen_TakeCompletion(&completion),
           "a reaped fake request must publish exactly one completion snapshot");
    return completion;
}

void TestMaechenTransportExactSuccessContract(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake),
           "the test-only Maechen seam must accept a complete fake operation table while idle");
    const FfxHooks::MaechenCompletion completion =
        RunFakeMaechenRequest(fake, 11, "SECRETQUESTION");

    std::lock_guard<std::mutex> lock(fake.mutex);
    Expect(completion.generation == 11 &&
               completion.result == MaechenAdapterResult::Success &&
               completion.pages.pageCount == 1 && completion.pages.lineCount[0] == 2 &&
               strcmp(completion.pages.lines[0][0], "Hello") == 0 &&
               strcmp(completion.pages.lines[0][1], "World") == 0,
           "HTTP 200 with exact protocol metadata must publish validated paginated pages");
    Expect(fake.workerThreadId != 0 && fake.workerThreadId != fake.mainThreadId,
           "the fake HTTPS transaction must execute on the owned worker thread");
    Expect(fake.userAgent == L"FFX-Hooks-Maechen/1" &&
               fake.host == L"ffxmodstudio.com" &&
               fake.port == 443 && fake.method == L"POST" &&
               fake.path == L"/api/maechen/game/v1" &&
               fake.requestFlags == WINHTTP_FLAG_SECURE,
           "the worker must use only the exact secure Maechen v1 endpoint and POST method");
    constexpr wchar_t kExpectedHeaders[] =
        L"Content-Type: application/json; charset=utf-8\r\n"
        L"Accept: text/plain\r\n"
        L"Accept-Encoding: identity\r\n"
        L"X-Maechen-Protocol: 1\r\n";
    Expect(fake.headers == kExpectedHeaders &&
               fake.requestBody ==
                   "{\"version\":1,\"locale\":\"pt\",\"question\":\"SECRETQUESTION\"}",
           "the worker must send the exact four explicit headers plus session User-Agent and bounded JSON");
    Expect(fake.disableFeatures == (WINHTTP_DISABLE_REDIRECTS |
                                    WINHTTP_DISABLE_COOKIES |
                                    WINHTTP_DISABLE_AUTHENTICATION),
           "redirect, cookie, and automatic-authentication features must all be disabled together");
    Expect(fake.resolveTimeout == 5000 && fake.connectTimeout == 5000 &&
               fake.sendTimeout == 10000 && fake.receiveTimeout == 30000,
           "WinHTTP timeouts must remain finite at 5/5/10/30 seconds");
    Expect(fake.cancelManualReset && !fake.cancelInitiallySignaled,
           "each request must create a fresh manual-reset cancel event in the unsignaled state");
    Expect(fake.statusQueries == 1 && fake.contentTypeQueries == 1 &&
               fake.protocolQueries == 1 && fake.contentEncodingQueries == 1,
           "status and all three protocol metadata fields must be queried before body reads");
    Expect(OperationsContainInOrder(fake.operations, {
               "open-session", "set-timeouts", "open-connect", "open-request",
               "disable-features", "probe-cancel", "send", "receive", "probe-cancel",
               "query-status", "query-content-type", "query-protocol",
               "query-content-encoding", "read", "close-request", "close-connect",
               "close-session", "close-worker", "close-cancel"}),
           "transport and slot handles must close in strict reverse ownership order");
    Expect(fake.transportHandles.empty() && fake.cancelEvents.empty() &&
               fake.workerCreates == fake.workerCloses &&
               fake.liveWorkerCloseAttempts == 0,
           "the successful fake request must leave zero transport, worker, or cancel handles");
}

void TestMaechenTransportStatusTaxonomy(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;
    struct StatusCase {
        uint32_t status;
        MaechenAdapterResult expected;
        const char* message;
    };
    const StatusCase cases[] = {
        {302, MaechenAdapterResult::RetryLater,
         "redirect responses must fail closed without following or displaying a body"},
        {400, MaechenAdapterResult::InvalidQuestion,
         "HTTP 400 must map to InvalidQuestion without publishing its body"},
        {413, MaechenAdapterResult::InvalidQuestion,
         "HTTP 413 must map to InvalidQuestion without publishing its body"},
        {429, MaechenAdapterResult::RateLimited,
         "HTTP 429 must map to RateLimited without publishing its body"},
        {503, MaechenAdapterResult::RetryLater,
         "unrecognized non-200 status must map to RetryLater without publishing its body"},
    };
    uint32_t generation = 20;
    for (const StatusCase& item : cases) {
        Expect(ConfigureFakeMaechen(fake),
               "each status scenario must install only while the slot is idle");
        {
            std::lock_guard<std::mutex> lock(fake.mutex);
            fake.status = item.status;
            fake.responseBody = "DO NOT PUBLISH";
        }
        const FfxHooks::MaechenCompletion completion =
            RunFakeMaechenRequest(fake, generation++);
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(completion.result == item.expected && MaechenPagesEmpty(completion.pages) &&
                   fake.readCalls == 0,
               item.message);
        Expect(fake.statusQueries == 1 && fake.contentTypeQueries == 1 &&
                   fake.protocolQueries == 1 && fake.contentEncodingQueries == 1,
               "non-200 responses must still query bounded metadata but never read the body");
    }
}

void TestMaechenTransportMetadataAndTimeouts(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake), "wrong-content-type scenario must configure");
    fake.contentType = "application/json";
    auto completion = RunFakeMaechenRequest(fake, 30);
    Expect(completion.result == MaechenAdapterResult::RetryLater &&
               MaechenPagesEmpty(completion.pages) && fake.readCalls == 0,
           "JSON success content must fail closed before any body read or pagination");

    Expect(ConfigureFakeMaechen(fake), "missing-content-type scenario must configure");
    fake.missingContentType = true;
    completion = RunFakeMaechenRequest(fake, 31);
    Expect(completion.result == MaechenAdapterResult::RetryLater,
           "missing Content-Type on HTTP 200 must fail closed");

    Expect(ConfigureFakeMaechen(fake), "wrong-protocol scenario must configure");
    fake.protocol = "2";
    completion = RunFakeMaechenRequest(fake, 32);
    Expect(completion.result == MaechenAdapterResult::RetryLater && fake.readCalls == 0,
           "wrong protocol version on HTTP 200 must fail closed before any body read");

    Expect(ConfigureFakeMaechen(fake), "missing-protocol scenario must configure");
    fake.missingProtocol = true;
    completion = RunFakeMaechenRequest(fake, 33);
    Expect(completion.result == MaechenAdapterResult::RetryLater,
           "missing protocol header on HTTP 200 must fail closed");

    Expect(ConfigureFakeMaechen(fake), "gzip scenario must configure");
    fake.contentEncoding = "gzip";
    completion = RunFakeMaechenRequest(fake, 34);
    Expect(completion.result == MaechenAdapterResult::RetryLater && fake.readCalls == 0,
           "compressed HTTP 200 data must never enter the fixed response buffer");

    Expect(ConfigureFakeMaechen(fake), "absent-encoding scenario must configure");
    fake.missingContentEncoding = true;
    completion = RunFakeMaechenRequest(fake, 35);
    Expect(completion.result == MaechenAdapterResult::Success,
           "only an absent Content-Encoding header may be interpreted as identity");

    Expect(ConfigureFakeMaechen(fake), "timeout scenario must configure");
    fake.sendFails = true;
    fake.failureError = ERROR_WINHTTP_TIMEOUT;
    completion = RunFakeMaechenRequest(fake, 36);
    Expect(completion.result == MaechenAdapterResult::Timeout &&
               MaechenPagesEmpty(completion.pages),
           "WinHTTP timeout errors must map to Timeout with no pages");

    Expect(ConfigureFakeMaechen(fake), "generic transport-failure scenario must configure");
    fake.receiveFails = true;
    fake.failureError = ERROR_WINHTTP_CANNOT_CONNECT;
    completion = RunFakeMaechenRequest(fake, 37);
    Expect(completion.result == MaechenAdapterResult::RetryLater &&
               MaechenPagesEmpty(completion.pages),
           "non-timeout transport failures must map to RetryLater with no pages");

    Expect(ConfigureFakeMaechen(fake), "missing-status scenario must configure");
    fake.statusFails = true;
    fake.failureError = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    completion = RunFakeMaechenRequest(fake, 38);
    Expect(completion.result == MaechenAdapterResult::RetryLater &&
               MaechenPagesEmpty(completion.pages),
           "a failed numeric status query must fail closed even when other metadata is valid");
}

void TestMaechenTransportSingleShotAttemptCounts(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    struct AttemptCase {
        uint32_t generation;
        uint32_t status;
        bool sendFails;
        bool receiveFails;
        uint32_t error;
        MaechenAdapterResult expectedResult;
        uint32_t expectedReceives;
        const char* message;
    };
    const AttemptCase cases[] = {
        {39, 200, false, false, ERROR_SUCCESS, MaechenAdapterResult::Success, 1,
         "a successful POST must send and receive exactly once"},
        {40, 503, false, false, ERROR_SUCCESS, MaechenAdapterResult::RetryLater, 1,
         "a non-200 POST must not replay before returning its mapped failure"},
        {41, 200, true, false, ERROR_WINHTTP_TIMEOUT, MaechenAdapterResult::Timeout, 0,
         "a send timeout must fail after exactly one send without receiving"},
        {42, 200, false, true, ERROR_WINHTTP_CANNOT_CONNECT,
         MaechenAdapterResult::RetryLater, 1,
         "a connection failure during receive must not replay the POST"},
    };

    for (const AttemptCase& item : cases) {
        Expect(ConfigureFakeMaechen(fake),
               "each single-shot attempt scenario must configure while the slot is idle");
        {
            std::lock_guard<std::mutex> lock(fake.mutex);
            fake.status = item.status;
            fake.sendFails = item.sendFails;
            fake.receiveFails = item.receiveFails;
            fake.failureError = item.error;
        }
        const FfxHooks::MaechenCompletion completion =
            RunFakeMaechenRequest(fake, item.generation);
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(completion.result == item.expectedResult && fake.sendCalls == 1 &&
                   fake.receiveCalls == item.expectedReceives,
               item.message);
    }
}

void TestMaechenTransportCancellationAndBounds(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake), "cancel-before-send scenario must configure");
    fake.cancelAtProbe = 1;
    auto completion = RunFakeMaechenRequest(fake, 40);
    Expect(completion.result == MaechenAdapterResult::Cancelled && fake.sendCalls == 0,
           "cancellation must be checked before the request is sent");

    Expect(ConfigureFakeMaechen(fake), "cancel-after-receive scenario must configure");
    fake.cancelAfterReceive = true;
    completion = RunFakeMaechenRequest(fake, 41);
    Expect(completion.result == MaechenAdapterResult::Cancelled && fake.readCalls == 0,
           "cancellation must be checked immediately after receive and before metadata/body use");

    Expect(ConfigureFakeMaechen(fake), "cancel-between-reads scenario must configure");
    fake.responseBody = "A\nB";
    fake.readChunk = 1;
    fake.cancelAfterRead = 1;
    completion = RunFakeMaechenRequest(fake, 42);
    Expect(completion.result == MaechenAdapterResult::Cancelled && fake.readCalls == 1,
           "cancellation signaled by one read must stop before a second body read");

    std::string body4096;
    for (size_t line = 0; line < 71; ++line) {
        body4096.append(56, 'A');
        body4096.push_back('\n');
    }
    body4096.append(49, 'B');
    Expect(body4096.size() == 4096,
           "the transport fixture must independently contain exactly 4096 response bytes");

    Expect(ConfigureFakeMaechen(fake), "exact-body-cap scenario must configure");
    fake.responseBody = body4096;
    completion = RunFakeMaechenRequest(fake, 43);
    Expect(completion.result == MaechenAdapterResult::Success &&
               !fake.readCapacities.empty() && fake.readCapacities.back() == 1,
           "an exact 4096-byte body must succeed only after a one-byte EOF probe");

    Expect(ConfigureFakeMaechen(fake), "overflow-body scenario must configure");
    fake.responseBody = body4096 + "C";
    completion = RunFakeMaechenRequest(fake, 44);
    Expect(completion.result == MaechenAdapterResult::RetryLater &&
               MaechenPagesEmpty(completion.pages) && fake.responseOffset == 4097 &&
               !fake.readCapacities.empty() && fake.readCapacities.back() == 1,
           "a 4097th byte must be detected by the one-byte probe and never truncated to success");
}

void TestMaechenRequestSlotLifecycle(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake), "blocked-worker lifecycle scenario must configure");
    uint32_t workerClosesBefore = 0;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.blockReceive = true;
        workerClosesBefore = fake.workerCloses;
    }
    Expect(FfxHooks::Maechen_SubmitRequest("pt", "First", 50),
           "the lifecycle scenario must start its first request");
    Expect(WaitForFakeMaechenFlag(fake, &fake.receiveEntered),
           "the worker must reach the controlled receive barrier");
    const uintptr_t firstCancelIdentity = FfxHooks::Maechen_TestCancelIdentity();
    Expect(firstCancelIdentity != 0,
           "each active generation must expose a nonzero test-only cancel identity");
    Expect(!FfxHooks::Maechen_SubmitRequest("pt", "Second", 51),
           "a live RequestSlot must reject a second submission before reap");

    const auto pumpStart = std::chrono::steady_clock::now();
    FfxHooks::Maechen_PumpTick(true);
    const auto pumpMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pumpStart).count();
    Expect(pumpMillis < 50 && FfxHooks::Maechen_RequestBusy(),
           "pump must poll without waiting or closing a live worker handle");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(fake.workerCloses == workerClosesBefore && !fake.cancelEvents.empty(),
               "a live worker and its cancel event must remain owned after a nonblocking pump");
    }

    FfxHooks::Maechen::Pages noPages{};
    Expect(!FfxHooks::Maechen_TestTryPublishCompletion(
               51, MaechenAdapterResult::RetryLater, &noPages),
           "a completion with a mismatched generation must be rejected under the slot lock");
    Expect(FfxHooks::Maechen_TestPublishMenuOwnedForClose() &&
               FfxHooks::Maechen_MenuOwned() &&
               FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "the close lifecycle scenario must begin with explicit menu ownership that blocks a second modal");
    FfxHooks::Maechen_TestCloseMenuFromPump();
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               !FfxHooks::Maechen_PumpWakePending() &&
               FfxHooks::Maechen_RequestBusy(),
           "closing a blocked request must release menu/title/force ownership immediately");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.releaseReceive = true;
        fake.condition.notify_all();
    }
    Expect(WaitForFakeMaechenFlag(fake, &fake.transportFinished),
           "the cancelled worker must close all transport handles before returning");
    HANDLE cancelledWorker = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        cancelledWorker = fake.latestWorkerHandle;
    }
    Expect(cancelledWorker && WaitForSingleObject(cancelledWorker, 5000) == WAIT_OBJECT_0,
           "the cancelled worker must be signaled before publishing its reap wake");
    const auto wakeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!FfxHooks::Maechen_PumpWakePending() &&
           std::chrono::steady_clock::now() < wakeDeadline) {
        Sleep(1);
    }
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               FfxHooks::Maechen_PumpWakePending() &&
               FfxHooks::Maechen_RequestBusy() &&
               FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "the completed worker must publish one pump-reap wake that blocks allocation until reap");
    Expect(FfxHooks::Maechen_RequestBusy() &&
               !FfxHooks::Maechen_SubmitRequest("pt", "Still blocked", 52),
           "a completed-but-unreaped slot must remain busy and reject a second submission");
    Expect(PumpMaechenUntilIdle(), "the pump must reap the completed worker and cancel event");
    Expect(!FfxHooks::Maechen_MenuOwned() && !FfxHooks::Maechen_PumpWakePending() &&
               !FfxHooks::Maechen_BlocksNativeModalAllocation(),
           "the sole pump reaper must clear the one-shot reap wake and unblock allocation after closing both handles");
    FfxHooks::MaechenCompletion completion{};
    Expect(FfxHooks::Maechen_TakeCompletion(&completion) &&
               completion.generation == 50 &&
               completion.result == MaechenAdapterResult::Cancelled,
           "the cancelled generation must publish one matching terminal snapshot");

    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(OperationsContainInOrder(fake.operations, {
                   "close-request", "close-connect", "close-session",
                   "close-worker", "close-cancel"}) &&
                   fake.transportHandles.empty() && fake.cancelEvents.empty() &&
                   fake.workerCreates == fake.workerCloses,
               "pump reap must follow worker LIFO cleanup and leave zero handles");
    }

    Expect(ConfigureFakeMaechen(fake),
           "late-close completion-tail scenario must configure after reap");
    {
        std::lock_guard<std::mutex> lock(g_maechenLogMutex);
        g_blockMaechenCompletionLog = true;
        g_maechenCompletionLogEntered = false;
        g_releaseMaechenCompletionLog = false;
    }
    Expect(FfxHooks::Maechen_SubmitRequest("pt", "Late close", 52),
           "late-close scenario must start one worker");
    {
        std::unique_lock<std::mutex> lock(g_maechenLogMutex);
        Expect(g_maechenLogCondition.wait_for(lock, std::chrono::seconds(5), []() {
                   return g_maechenCompletionLogEntered;
               }),
               "worker must pause after completion publication but before terminal wake");
    }
    FfxHooks::Maechen_CancelRequest();
    Expect(!FfxHooks::Maechen_MenuOwned() &&
               !FfxHooks::Maechen_PumpWakePending() &&
               FfxHooks::Maechen_RequestBusy(),
           "a close racing after completion publication must not fabricate early UI or wake ownership");
    {
        std::lock_guard<std::mutex> lock(g_maechenLogMutex);
        g_releaseMaechenCompletionLog = true;
        g_maechenLogCondition.notify_all();
    }
    HANDLE lateCloseWorker = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        lateCloseWorker = fake.latestWorkerHandle;
    }
    Expect(lateCloseWorker && WaitForSingleObject(lateCloseWorker, 5000) == WAIT_OBJECT_0 &&
               FfxHooks::Maechen_PumpWakePending(),
           "unconditional worker-tail publication must preserve the reap wake across late close");
    Expect(PumpMaechenUntilIdle() && !FfxHooks::Maechen_PumpWakePending(),
           "late-close worker wake must clear only after Pump reaps both handles");
    Expect(FfxHooks::Maechen_TakeCompletion(&completion) &&
               completion.generation == 52 &&
               completion.result == MaechenAdapterResult::Success,
           "late close must retain the already published terminal completion");
    {
        std::lock_guard<std::mutex> lock(g_maechenLogMutex);
        g_blockMaechenCompletionLog = false;
        g_maechenCompletionLogEntered = false;
        g_releaseMaechenCompletionLog = false;
    }

    Expect(ConfigureFakeMaechen(fake), "fresh-cancel scenario must configure after reap");
    Expect(FfxHooks::Maechen_SubmitRequest("pt", "Fresh", 53),
           "submission must resume only after the prior generation is fully reaped");
    const uintptr_t secondCancelIdentity = FfxHooks::Maechen_TestCancelIdentity();
    Expect(secondCancelIdentity != 0 && secondCancelIdentity != firstCancelIdentity,
           "a later generation must own a fresh cancel event identity instead of resetting one");
    Expect(PumpMaechenUntilIdle(), "the fresh generation must complete and reap");
    Expect(FfxHooks::Maechen_TakeCompletion(&completion) &&
               completion.generation == 53 &&
               completion.result == MaechenAdapterResult::Success,
           "the fresh generation must publish normally after the prior reap");
}

void TestMaechenPumpOwnsCompletionObservation(FakeMaechenTransport& fake) {
    using FfxHooks::MaechenAdapterResult;

    Expect(ConfigureFakeMaechen(fake), "Present/pump race scenario must configure while idle");
    Expect(FfxHooks::Maechen_SubmitRequest("pt", "Race", 54),
           "Present/pump race scenario must start one worker");
    Expect(WaitForFakeMaechenFlag(fake, &fake.transportFinished),
           "race worker must finish transport ownership before the handle barrier");

    HANDLE worker = nullptr;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        worker = fake.latestWorkerHandle;
    }
    Expect(worker && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0,
           "race worker must be signaled but deliberately unreaped");
    FfxHooks::Maechen_CancelRequest();

    uint32_t closesBefore = 0;
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.blockFirstWorkerPoll = true;
        closesBefore = fake.workerCloses;
    }

    FfxHooks::Maechen_PresentTick(false, false, true);
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(!fake.workerPollEntered && fake.workerCloses == closesBefore,
               "Present must not observe or close worker handles");
    }
    std::atomic<bool> pumpReturned{false};
    std::thread pump([&]() {
        FfxHooks::Maechen_PumpTick(true);
        pumpReturned.store(true, std::memory_order_release);
    });
    Expect(WaitForFakeMaechenFlag(fake, &fake.workerPollEntered),
           "Pump must own the controlled zero-time worker observation");
    Sleep(50);
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(!pumpReturned.load(std::memory_order_acquire) &&
                   fake.workerCloses == closesBefore,
               "Pump must not close a worker before its completion observation returns");
        fake.releaseWorkerPoll = true;
        fake.condition.notify_all();
    }
    pump.join();

    Expect(pumpReturned.load(std::memory_order_acquire) &&
               !FfxHooks::Maechen_RequestBusy() && !FfxHooks::Maechen_IsActive(),
           "after its observation returns, Pump alone must reap and clear pending ownership");
    FfxHooks::MaechenCompletion completion{};
    Expect(FfxHooks::Maechen_TakeCompletion(&completion) &&
               completion.generation == 54 &&
               completion.result == MaechenAdapterResult::Success,
           "pump-owned observation must not alter the already published completion");
}

void TestMaechenWorkerCreationFailureRetainsCancelOwnership(FakeMaechenTransport& fake) {
    Expect(ConfigureFakeMaechen(fake), "worker-allocation failure scenario must configure");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        fake.workerCreateFails = true;
        fake.cancelCloseFailsOnce = true;
    }
    Expect(!FfxHooks::Maechen_SubmitRequest("pt", "No worker", 55),
           "a failed worker allocation must reject submission");
    Expect(FfxHooks::Maechen_RequestBusy(),
           "a failed first cancel close must keep the cancel handle owned and the slot busy");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(fake.cancelEvents.size() == 1 && fake.cancelCloseFailures == 1,
               "the failed close must retain exactly the generation's cancel event for retry");
    }
    FfxHooks::Maechen_PumpTick(true);
    Expect(!FfxHooks::Maechen_RequestBusy(),
           "Pump must retry and complete cancel-event cleanup when no worker was created");
    {
        std::lock_guard<std::mutex> lock(fake.mutex);
        Expect(fake.cancelEvents.empty() && OperationsContainInOrder(fake.operations, {
                   "create-cancel", "create-worker-failed", "close-cancel-failed",
                   "close-cancel"}),
               "worker-allocation rollback must leave zero handles after the pump retry");
    }
    FfxHooks::MaechenCompletion completion{};
    Expect(!FfxHooks::Maechen_TakeCompletion(&completion),
           "worker allocation failure must not publish a transport completion");
}

void TestMaechenTransportSourceContracts() {
    std::string source;
    std::string header;
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), source) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.h"), header) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "Maechen adapter, public header, and DllMain must be readable for source contracts");
    const std::string code = SourceCodeOnly(source);
    const size_t testSeam = header.find("#if defined(FFXHOOKS_TESTING)");
    const std::string productionHeader = header.substr(0, testSeam);

    Expect(testSeam != std::string::npos &&
               productionHeader.find("MaechenTest") == std::string::npos &&
               productionHeader.find("uintptr_t") == std::string::npos &&
               productionHeader.find("ffx-mod-website") == std::string::npos &&
               productionHeader.find("headerOverride") == std::string::npos &&
               productionHeader.find("urlOverride") == std::string::npos,
           "opaque handles and fake transport controls must not exist in the production interface");
    ExpectSourceIncludes(source, "L\"ffxmodstudio.com\"",
                         "production transport must hardcode the exact Maechen host");
    ExpectSourceIncludes(source, "L\"/api/maechen/game/v1\"",
                         "production transport must hardcode the exact Maechen path");
    ExpectSourceIncludes(source, "WINHTTP_FLAG_SECURE",
                         "production transport must require the secure WinHTTP request flag");
    ExpectSourceIncludes(source, "WINHTTP_DISABLE_REDIRECTS |",
                         "production transport must combine redirect disabling with other gates");
    ExpectSourceIncludes(source, "WINHTTP_DISABLE_COOKIES |",
                         "production transport must combine cookie disabling with other gates");
    ExpectSourceIncludes(source, "WINHTTP_DISABLE_AUTHENTICATION",
                         "production transport must disable automatic authentication");

    for (const char* forbidden : {
             "http://", "Authorization:", "Cookie:", "api_key", "apiKey",
             "CreateFile", "ReadFile", "fopen(", "ifstream", "ResetEvent(",
             "TerminateThread("}) {
        ExpectSourceExcludes(code, forbidden,
                             "Maechen transport must exclude URL downgrade, credentials, local files, event reuse, and forced termination");
    }
    const SourceBlock present = SourceFunctionBody(
        source, "void Maechen_PresentTick(bool gameMenuOpen, bool otherCustomMenuOpen, bool ffxForeground)");
    const SourceBlock pump = SourceFunctionBody(source, "void Maechen_PumpTick(bool ffxForeground)");
    Expect(present.Valid() && pump.Valid() &&
               present.body.find("RunTransport") == std::string::npos &&
               present.body.find("WinHttp") == std::string::npos &&
               pump.body.find("RunTransport") == std::string::npos &&
               pump.body.find("WinHttp") == std::string::npos &&
               pump.body.find("WaitForSingleObject") == std::string::npos,
           "Present and pump entrypoints must never perform synchronous transport or waits");
    Expect(pump.Valid() && SourceTokensInOrder(pump.body, {
               "if (worker && !PlatformIsWorkerComplete(worker)) {",
               "PlatformCloseWorker(worker)"}),
           "pump must prove zero-time worker completion before closing the worker handle");
    ExpectSourceIncludes(source, "if (total > Maechen::kMaxResponseBytes)",
                         "the adapter must reject the one-byte overflow probe before validation");
    const SourceBlock dllmainBody = SourceFunctionBody(
        dllmain, "BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)");
    Expect(dllmainBody.Valid() && dllmainBody.body.find("WinHttp") == std::string::npos &&
               dllmainBody.body.find("Maechen_SubmitRequest") == std::string::npos,
           "DllMain must not submit or execute Maechen network work");
    Expect(code.find("NextGeneration") == std::string::npos,
           "the adapter must consume the core generation instead of owning a second counter");
    const SourceBlock worker = SourceFunctionBody(
        source, "DWORD WINAPI MaechenWorkerMain(void* parameter)");
    Expect(worker.Valid() && SourceTokensInOrder(worker.body, {
               "const bool published = PublishCompletion(",
               "if (published) LogResult(result)"}),
           "a stale rejected completion must not emit a misleading terminal-result log");

    std::lock_guard<std::mutex> lock(g_maechenLogMutex);
    bool logsAreMetadataOnly = true;
    for (const std::string& line : g_maechenLogs) {
        if (line.size() > 96 || line.find("SECRETQUESTION") != std::string::npos ||
            line.find("ffxmodstudio") != std::string::npos ||
            line.find("/api/maechen") != std::string::npos ||
            line.find("Authorization") != std::string::npos ||
            line.find("Cookie") != std::string::npos) {
            logsAreMetadataOnly = false;
        }
    }
    Expect(logsAreMetadataOnly,
           "Maechen logs must be bounded English metadata without question/body/header/endpoint data");
}

std::string IniSectionBody(const std::string& text, const char* section) {
    if (!section) return {};
    const std::string marker = std::string("[") + section + "]";
    const size_t beginMarker = text.find(marker);
    if (beginMarker == std::string::npos) return {};
    size_t begin = text.find('\n', beginMarker + marker.size());
    if (begin == std::string::npos) return {};
    ++begin;
    const size_t next = text.find("\n[", begin);
    return text.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

void TestMaechenDefaultOffAndLocaleContracts() {
    std::string config;
    std::string ini;
    std::string dllmain;
    std::string adapter;
    Expect(ReadWholeFile(RuntimeSourcePath("shared\\Config.cpp"), config) &&
               ReadWholeFile(RuntimeSourcePath("ffx-hooks.ini"), ini) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), adapter),
           "Maechen configuration and startup sources must be readable");

    const size_t defaultBegin = config.find("\"[maechen]\\n\"");
    const size_t defaultEnd = defaultBegin == std::string::npos
                                  ? std::string::npos
                                  : config.find(';', defaultBegin);
    const std::string defaultSection =
        defaultBegin == std::string::npos || defaultEnd == std::string::npos
            ? std::string{}
            : config.substr(defaultBegin, defaultEnd - defaultBegin);
    Expect(defaultSection.find("\"enabled = 0\\n\"") != std::string::npos &&
               defaultSection.find("\"locale = pt\\n\"") != std::string::npos &&
               CountSourceToken(defaultSection, " = ") == 2,
           "built-in Maechen defaults must contain only enabled=0 and locale=pt");
    const std::string iniSection = IniSectionBody(ini, "maechen");
    Expect(iniSection == "enabled = 0\nlocale = pt\n" ||
               iniSection == "enabled = 0\r\nlocale = pt\r\n",
           "ffx-hooks.ini Maechen template must contain only enabled=0 and locale=pt");

    const SourceBlock localeAllowed = SourceFunctionBody(
        adapter, "bool IsMaechenLocaleAllowed(const char* locale) noexcept");
    const SourceBlock localeFromConfig = SourceFunctionBody(
        adapter, "const char* MaechenLocaleFromConfig() noexcept");
    Expect(localeAllowed.Valid() && CountSourceToken(localeAllowed.body, "strcmp(locale,") == 6 &&
               SourceTokensInOrder(localeAllowed.body, {
                   "\"pt\"", "\"en\"", "\"es\"", "\"fr\"", "\"it\"", "\"de\""}),
           "runtime locale validation must allow exactly pt/en/es/fr/it/de");
    Expect(localeFromConfig.Valid() &&
               localeFromConfig.body.find("Config::GetString(\"maechen.locale\", \"pt\")") != std::string::npos &&
               localeFromConfig.body.find("IsMaechenLocaleAllowed") != std::string::npos &&
               localeFromConfig.body.find("return \"pt\"") != std::string::npos,
           "invalid or missing Maechen locale must fall back deterministically to pt");

    const SourceBlock armed = SourceFunctionBody(dllmain, "static bool NativeMenuArmedFromConfig()");
    const SourceBlock developerSources = SourceBlockAfterToken(
        dllmain, "static bool AuroraDeveloperUiEnabledFromExplicitSources(");
    Expect(armed.Valid() && armed.body.find("Config::GetBool(\"maechen.enabled\", false)") != std::string::npos,
           "Maechen enabled must arm the shared Present/native-pump producer");
    Expect(developerSources.Valid() && developerSources.body.find("maechen") == std::string::npos,
           "Maechen enablement must not enter the Aurora developer-UI source set");
}

void TestMaechenDeferredInstallRequiresPhysicalPresentReady() {
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "DllMain must be readable for deferred Maechen installation contracts");
    const SourceBlock tryInstall = SourceFunctionBody(
        dllmain, "static void TryInstallMaechenWhenReady()");
    const SourceBlock publishPresent = SourceBlockAfterToken(
        dllmain, "static void PublishAuroraD3DPresentResult(");
    const SourceBlock installHooks = SourceFunctionBody(dllmain, "static void InstallHooks()");

    Expect(tryInstall.Valid() &&
               tryInstall.body.find("AuroraD3DPresentReady()") != std::string::npos &&
               tryInstall.body.find("g_maechenConfigEnabledPublished") != std::string::npos &&
               tryInstall.body.find("g_maechenNativePumpReadyPublished") != std::string::npos &&
               tryInstall.body.find("InterlockedCompareExchange(&g_maechenInstallState, 1, 0)") != std::string::npos,
           "Maechen installation must require physical Present Ready plus published config/native-pump readiness and a once-only CAS");
    Expect(publishPresent.Valid() && SourceTokensInOrder(publishPresent.body, {
               "PresentHookResult::Ready", "NotifyUnXBoosterPresentProducer(true, false)",
               "TryInstallMaechenWhenReady()"}),
           "the actual Present-Ready publication must trigger the deferred Maechen install attempt");
    Expect(installHooks.Valid() &&
               installHooks.body.find("InterlockedExchange(&g_maechenConfigEnabledPublished") != std::string::npos &&
               installHooks.body.find("InterlockedExchange(&g_maechenNativePumpReadyPublished") != std::string::npos &&
               installHooks.body.find("TryInstallMaechenWhenReady()") != std::string::npos &&
               installHooks.body.find("FfxHooks::Maechen_Install(LogLine)") == std::string::npos &&
               CountSourceToken(dllmain, "FfxHooks::Maechen_Install(LogLine)") == 1,
           "InstallHooks must publish prerequisites and defer the sole Maechen install call to the ready gate");
}

void TestMaechenPlainF9ArbitrationContracts() {
    std::string adapter;
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), adapter) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "Maechen adapter and native producer must be readable for F9 arbitration");
    const std::string adapterCode = SourceCodeOnly(adapter);
    const SourceBlock sample = SourceFunctionBody(adapter, "bool SamplePlainF9() noexcept");
    const SourceBlock present = SourceFunctionBody(
        adapter, "void Maechen_PresentTick(bool gameMenuOpen, bool otherCustomMenuOpen, bool ffxForeground)");
    const SourceBlock nativePresent = SourceFunctionBody(dllmain, "static void NativeMenu_PresentTick()");
    const SourceBlock nativeStart = SourceFunctionBody(dllmain, "static bool StartNativeMenuIfEnabled()");

    Expect(sample.Valid() && CountSourceToken(sample.body, "GetAsyncKeyState(VK_F9)") == 1 &&
               sample.body.find("VK_CONTROL") != std::string::npos &&
               sample.body.find("VK_MENU") != std::string::npos &&
               sample.body.find("VK_SHIFT") != std::string::npos &&
               sample.body.find("g_f9ChordSuppressed") != std::string::npos &&
               SourceTokensInOrder(sample.body, {
                   "if (!f9Down)", "g_f9ChordSuppressed = false",
                   "if (modifiersDown)", "g_f9ChordSuppressed = true"}),
           "plain F9 must suppress the whole modifier chord until physical F9 release");
    Expect(present.Valid() && CountSourceToken(present.body, "SamplePlainF9()") == 1 &&
               CountSourceToken(adapterCode, "GetAsyncKeyState(VK_F9)") == 1,
           "Maechen Present must be the sole physical plain-F9 sampler");
    Expect(present.Valid() &&
               SourceTokensInOrder(present.body, {
                   "ForegroundAfterQueuedLoss(",
                   "ProcessPlainF9Input(inputForeground",
                   "inputForeground ? SamplePlainF9() : false"}),
           "Maechen must prove FFX foreground before sampling the sole physical F9 owner");
    Expect(nativePresent.Valid() && SourceTokensInOrder(nativePresent.body, {
               "Dash_F8Pressed()", "GetAsyncKeyState(g_nativeMenuHotkey)",
               "Maechen_PresentTick("}),
           "Maechen F9 sampling must run only after F8/F7 arbitration");
    Expect(nativePresent.Valid() && SourceTokensInOrder(nativePresent.body, {
               "if (g_f8MenuOpen.Exchange(false))",
               "else if (FfxHooks::Maechen_BlocksNativeModalAllocation())",
               "g_f8MenuOpen.Store(true)"}),
           "F8 must always close its own menu but reject a new open while Maechen owns or has a terminal reap wake");
    const size_t f8OpenState = nativePresent.body.find("g_f8MenuOpen.Store(true)");
    const size_t f8OpenLog = nativePresent.body.find(
        "Log(\"[ffx-hooks] F8 -> opening catalog FLAGS submenu", f8OpenState);
    const std::string f8OpenPublication =
        f8OpenState != std::string::npos && f8OpenLog != std::string::npos
            ? nativePresent.body.substr(f8OpenState, f8OpenLog - f8OpenState)
            : std::string{};
    Expect(nativePresent.Valid() &&
               CountSourceToken(f8OpenPublication,
                   "InterlockedExchange(&g_f7WantOpenKind, F7_MENU_FLAGS)") == 1 &&
               CountSourceToken(f8OpenPublication,
                   "InterlockedExchange(&g_nativeWantSpawn, 1)") == 1 &&
               SourceTokensInOrder(f8OpenPublication, {
                   "InterlockedExchange(&g_f7WantOpenKind, F7_MENU_FLAGS)",
                   "InterlockedExchange(&g_nativeWantSpawn, 1)"}),
           "direct F8 must publish its FLAGS kind before the spawn-ready request");
    Expect(nativePresent.Valid() &&
               nativePresent.body.find("g_f8MenuOpen.Load() ||") != std::string::npos &&
               nativePresent.body.find("g_nativeWantSpawn") != std::string::npos,
           "same-frame F8/F7 requests must be reported as another owner before Maechen F9");
    Expect(nativePresent.Valid() && SourceTokensInOrder(nativePresent.body, {
               "if (edge)", "if (FfxHooks::Maechen_MenuOwned())",
               "InterlockedCompareExchange(&g_forceSubsystem, 0, 0) == 0"}),
           "F7 must not close or steal the shared force gate while Maechen is active");
    Expect(nativeStart.Valid() &&
               nativeStart.body.find("g_nativeMenuHotkey == VK_F9 ? VK_F7 : g_nativeMenuHotkey") != std::string::npos,
           "a dynamic native-menu VK_F9 collision must deterministically yield F9 to Maechen");

    const SourceBlock developerHotkey = SourceFunctionBody(
        dllmain, "static bool AuroraDeveloperHotkeyPressed(int virtualKey)");
    Expect(developerHotkey.Valid() && developerHotkey.body.find("controlDown && altDown && !shiftDown") != std::string::npos &&
               CountSourceToken(dllmain, "AuroraDeveloperHotkeyPressed(VK_F9)") == 2 &&
               CountSourceToken(dllmain, "AuroraDeveloperHotkeyPressed(VK_F10)") == 2,
           "Aurora must retain only explicit Ctrl+Alt+F9/F10 developer chords");
}

void TestMaechenPresentPumpOwnershipContracts() {
    std::string adapter;
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), adapter) &&
               ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "Maechen lifecycle sources must be readable");
    const SourceBlock present = SourceFunctionBody(
        adapter, "void Maechen_PresentTick(bool gameMenuOpen, bool otherCustomMenuOpen, bool ffxForeground)");
    const SourceBlock pump = SourceFunctionBody(adapter, "void Maechen_PumpTick(bool ffxForeground)");
    const SourceBlock closeFromPump = SourceFunctionBody(
        adapter, "void CloseMaechenMenuFromPump(Maechen::EventKind closeKind = Maechen::EventKind::Close) noexcept");
    const SourceBlock draw = SourceFunctionBody(
        adapter, "int __cdecl MaechenPumpDraw(int obj)");
    const SourceBlock nativePump = SourceFunctionBody(
        dllmain, "static int __cdecl NativeMenu_PumpHook(unsigned int a1)");
    const SourceBlock nativePresent = SourceFunctionBody(
        dllmain, "static void NativeMenu_PresentTick()");
    const SourceBlock titleGuard = SourceFunctionBody(
        dllmain, "static int __cdecl NativeTextOutline_MenuGuard(\n    void* renderState, void* glyphMetrics, float scale)");
    const SourceBlock worker = SourceFunctionBody(
        adapter, "DWORD WINAPI MaechenWorkerMain(void* parameter)");
    const SourceBlock publishSnapshot = SourceFunctionBody(
        adapter, "void PublishRenderSnapshot() noexcept");
    const SourceBlock copySnapshot = SourceFunctionBody(
        adapter, "bool CopyStableRenderSnapshot(MaechenRenderSnapshot* output) noexcept");
    const SourceBlock reserveOpenRequest = SourceBlockAfterToken(
        adapter, "long NativeMenu_ReserveAndConsumeOpenRequest(");
    const SourceBlock publishNativeOwner = SourceFunctionBody(
        dllmain, "static void NativeMenuPublishOwnerReservationFromPumpState()");
    const SourceBlock applyCompletion = SourceFunctionBody(
        adapter, "bool ApplyCompletionFromPump(const MaechenCompletion& completion) noexcept");
    const SourceBlock applySubmitFailure = SourceFunctionBody(
        adapter, "bool ApplySubmitFailureFromPump(MaechenAdapterResult result) noexcept");
    const SourceBlock legacyModalIdle = SourceFunctionBody(
        dllmain, "static bool NativeMenuLegacyModalAllocationIdle()");
    const SourceBlock delayedArenaPending = SourceFunctionBody(
        dllmain, "static bool ArenaPlusNpcDelayedOpenPending()");
    const SourceBlock promoteDelayedArena = SourceFunctionBody(
        dllmain, "static void NativeMenuPromoteDelayedArenaRequestIfReady()");
    const SourceBlock tryOpenArena = SourceFunctionBody(
        dllmain,
        "static FfxHooks::F8Ui::ArenaOpenResult NativeMenuTryOpenArenaRequest()");
    const SourceBlock pumpOnlyOwners = SourceFunctionBody(
        dllmain, "static bool NativeMenuOtherOwnerOrRequestActivePumpOnly()");
    const SourceBlock publishedOwners = SourceFunctionBody(
        dllmain, "static bool NativeMenuOtherOwnerOrRequestPublished()");
    const SourceBlock ffxForeground = SourceFunctionBody(
        dllmain, "static bool F7IsForegroundWindow()");
    const SourceBlock processF9 = SourceFunctionBody(
        adapter, "void ProcessPlainF9Input(bool ffxForeground, bool plainF9Down, bool gameMenuOpen, bool otherCustomMenuOpen) noexcept");
    const SourceBlock pumpKey = SourceFunctionBody(
        adapter, "bool PumpKeyPressed(int virtualKey) noexcept");
    const SourceBlock dllEntry = SourceFunctionBody(
        dllmain, "BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)");
    const SourceBlock queuedFocus = SourceBlockAfterToken(
        adapter, "bool ForegroundAfterQueuedLoss(");
    const SourceBlock focusNotify = SourceFunctionBody(
        adapter, "void Maechen_NotifyForegroundLost() noexcept");
    const SourceBlock menuWndProc = SourceFunctionBody(
        dllmain, "static LRESULT CALLBACK InGameMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)");

    Expect(present.Valid() && processF9.Valid() &&
               processF9.body.find("InterlockedExchange(&g_openRequested, 1)") != std::string::npos &&
               processF9.body.find("InterlockedExchange(&g_closeRequested, 1)") != std::string::npos &&
               present.body.find("NativeMenu::Alloc") == std::string::npos &&
               present.body.find("Maechen_SubmitRequest") == std::string::npos &&
               present.body.find("LogFixed") == std::string::npos &&
               present.body.find("WaitForSingleObject") == std::string::npos &&
               present.body.find("WinHttp") == std::string::npos,
           "Present may only sample/publish Maechen open-close requests");
    Expect(ffxForeground.Valid() &&
               ffxForeground.body.find("GetForegroundWindow()") != std::string::npos &&
               ffxForeground.body.find("g_ingameMenuInputHwnd") != std::string::npos &&
               ffxForeground.body.find("GetWindowThreadProcessId") != std::string::npos &&
               ffxForeground.body.find("GetCurrentProcessId()") != std::string::npos &&
               SourceCodeOnly(ffxForeground.body).find("title") == std::string::npos,
           "Maechen must reuse F7's HWND/PID foreground proof without a title dependency");
    Expect(processF9.Valid() &&
               processF9.body.find("ConsumeFocusedRisingEdge") != std::string::npos &&
               processF9.body.find("InterlockedExchange(&g_closeRequested, 1)") != std::string::npos &&
               processF9.body.find("g_forceSubsystem") == std::string::npos,
           "background Present must reset the F9 edge and request only Maechen cleanup");
    Expect(processF9.Valid() && SourceTokensInOrder(processF9.body, {
               "InterlockedCompareExchange(&g_focusLossEpoch, 0, 0)",
               "InterlockedExchange(&g_f9ReleaseFocusEpoch, focusEpoch)",
               "InterlockedExchange(&g_f9ReleaseObserved, 1)"}),
           "a focused physical F9 release must publish its focus epoch before its ready bit");
    Expect(pump.Valid() && SourceTokensInOrder(pump.body, {
               "ForegroundAfterQueuedLoss(",
               "ObserveForegroundInput(&g_pumpInputGate, inputForeground)",
               "ForegroundInputDecision::Blocked",
               "CloseMaechenMenuFromPump(Maechen::EventKind::FocusLost)",
               "ForegroundInputDecision::Prime", "PrimePumpKeys()",
               "if (!g_menuObj ||",
               "inputDecision != Maechen::ForegroundInputDecision::Sample",
               "return", "PumpKeyPressed(VK_ESCAPE)",
               "GetAsyncKeyState(VK_SHIFT)"}) &&
               pumpKey.Valid() &&
               pumpKey.body.find("GetAsyncKeyState(virtualKey)") != std::string::npos,
           "background Pump must close/cancel Maechen and prime held keys before any text or navigation sampling");
    Expect(queuedFocus.Valid() && focusNotify.Valid() && menuWndProc.Valid() &&
               queuedFocus.body.find("g_focusLossEpoch") != std::string::npos &&
               queuedFocus.body.find("InterlockedExchange(observedEpoch, published)") != std::string::npos &&
               focusNotify.body.find("InterlockedIncrement(&g_focusLossEpoch)") != std::string::npos &&
               CountSourceToken(menuWndProc.body,
                   "FfxHooks::Maechen_NotifyForegroundLost();") == 2 &&
               adapter.find("g_presentFocusLossEpoch") != std::string::npos &&
               adapter.find("g_pumpFocusLossEpoch") != std::string::npos,
           "WndProc focus loss must publish one Maechen epoch observed independently by Present and Pump");
    Expect(dllEntry.Valid() &&
               dllEntry.body.find("GetForegroundWindow") == std::string::npos &&
               dllEntry.body.find("GetAsyncKeyState") == std::string::npos &&
               dllEntry.body.find("Maechen_PresentTick") == std::string::npos &&
               dllEntry.body.find("Maechen_PumpTick") == std::string::npos,
           "DllMain must perform no Maechen focus check, key sampling, or pump work");
    Expect(pump.Valid() && SourceTokensInOrder(pump.body, {
               "InterlockedExchange(&g_f9ReleaseObserved, 0)",
               "EventKind::F9Sample, false",
               "InterlockedExchange(&g_openRequested, 0)",
               "const Maechen::Actions openActions", "EventKind::F9Sample, true",
               "if (!openActions.requestOpen)", "else", "NativeMenu::Alloc()",
               "NativeMenu::ClaimModal", "NativeMenu::Register"}) &&
               CountSourceToken(pump.body, "EventKind::F9Sample, false") == 1 &&
               CountSourceToken(pump.body,
                   "releaseFocusEpoch == pumpFocusEpoch") == 2 &&
               pump.body.find("ffxForeground && releaseObserved != 0") !=
                   std::string::npos &&
               closeFromPump.Valid() &&
               closeFromPump.body.find("NativeMenu::WrB(g_menuObj, 65, 1)") != std::string::npos &&
               closeFromPump.body.find("NativeMenu::ReleaseModalIfOwned") != std::string::npos &&
               closeFromPump.body.find("InterlockedExchange(&g_menuVisiblePublished, 0)") != std::string::npos &&
               closeFromPump.body.find("InterlockedExchange(&g_menuActiveOrPending, 0)") != std::string::npos &&
               pump.body.find("CloseMaechenMenuFromPump") != std::string::npos &&
               pump.body.find("Maechen_SubmitRequest") != std::string::npos &&
               pump.body.find("PlatformCloseWorker") != std::string::npos,
           "native pump close must immediately release visible/modal/menu ownership while retaining request-slot reap ownership");
    Expect(pump.Valid() &&
               pump.body.find("EventKind::AskAgain") != std::string::npos &&
               pump.body.find("Phase::Answer") != std::string::npos &&
               pump.body.find("editingPhase") != std::string::npos &&
               pump.body.find("const bool enterPressed = PumpKeyPressed(VK_RETURN)") !=
                   std::string::npos &&
               CountSourceToken(pump.body, "PumpKeyPressed(VK_RETURN)") == 1 &&
               SourceTokensInOrder(pump.body, {
                   "const bool enterPressed", "Phase::Answer",
                   "EventKind::AskAgain", "PublishRenderSnapshot",
                   "enterPressed && g_questionLength != 0"}),
           "Pump must sample Enter exactly once, then route that edge to AskAgain or Submit by phase");
    Expect(nativePump.Valid() &&
               nativePump.body.find("FfxHooks::Maechen_PumpTick(F7IsForegroundWindow())") != std::string::npos &&
               nativePump.body.find("g_nativeMenuInHook") != std::string::npos,
           "Maechen pump work must execute only inside the existing reentrancy guard");
    Expect(nativePresent.Valid() &&
               nativePresent.body.find("gameMenuOpen, otherCustomMenuOpen, f7Foreground") != std::string::npos,
           "Maechen Present must consume the same foreground decision already used by F7");
    Expect(draw.Valid() && draw.body.find("GetAsyncKeyState") == std::string::npos &&
               draw.body.find("Maechen_SubmitRequest") == std::string::npos &&
               draw.body.find("WinHttp") == std::string::npos &&
               draw.body.find("WaitFor") == std::string::npos,
           "Present draw must be input/network/wait free");
    Expect(titleGuard.Valid() &&
               titleGuard.body.find("FfxHooks::Maechen_MenuOwned()") != std::string::npos &&
               titleGuard.body.find("NativeMenuHubCloseDrainPending()") != std::string::npos &&
               titleGuard.body.find("g_nativeMenuTitleGuardRequired") == std::string::npos &&
               titleGuard.body.find("Maechen_PumpWakePending") == std::string::npos,
           "text-outline guard must follow concrete menu or close-drain ownership, never a process-lifetime latch or background reap wake");

    Expect(worker.Valid() && SourceTokensInOrder(worker.body, {
               "PublishCompletion(", "InterlockedExchange(&g_pumpReapWakePending, 1)"}) &&
               pump.Valid() && SourceTokensInOrder(pump.body, {
                   "PlatformCloseWorker(worker)",
                   "InterlockedExchange(&g_pumpReapWakePending, 0)"}),
           "the worker must publish one atomic reap wake after completion and Pump must clear it only after reap");
    Expect(applyCompletion.Valid() &&
               applyCompletion.body.find("FailureFromAdapterResult(completion.result)") != std::string::npos &&
               applyCompletion.body.find("PublishRenderSnapshot()") != std::string::npos &&
               pump.Valid() && pump.body.find("ApplyCompletionFromPump(completion)") != std::string::npos,
           "Pump must preserve each adapter failure family through the shared generation-safe UI publication path");
    Expect(applySubmitFailure.Valid() &&
               applySubmitFailure.body.find("FailureFromAdapterResult(result)") != std::string::npos &&
               applySubmitFailure.body.find("PublishRenderSnapshot()") != std::string::npos &&
               pump.Valid() && SourceTokensInOrder(pump.body, {
                   "MaechenAdapterResult rejectionResult = MaechenAdapterResult::RetryLater",
                   "Maechen_SubmitRequest(g_locale, g_question, g_uiState.generation,",
                   "&rejectionResult", "ApplySubmitFailureFromPump(rejectionResult)"}),
           "Pump must preserve the local submit rejection family instead of collapsing it to RetryLater");
    Expect(adapter.find("SRWLOCK g_renderSnapshotLock = SRWLOCK_INIT") != std::string::npos &&
               publishSnapshot.Valid() &&
               publishSnapshot.body.find("AcquireSRWLockExclusive(&g_renderSnapshotLock)") != std::string::npos &&
               publishSnapshot.body.find("ReleaseSRWLockExclusive(&g_renderSnapshotLock)") != std::string::npos &&
               copySnapshot.Valid() &&
               copySnapshot.body.find("TryAcquireSRWLockShared(&g_renderSnapshotLock)") != std::string::npos &&
               copySnapshot.body.find("ReleaseSRWLockShared(&g_renderSnapshotLock)") != std::string::npos,
           "render snapshot publication must use an exclusive SRW writer and a nonblocking shared Present reader");
    Expect(reserveOpenRequest.Valid() && SourceTokensInOrder(reserveOpenRequest.body, {
               "InterlockedExchange(ownerPublished, 1)",
               "InterlockedExchange(request, emptyValue)",
               "PauseAfterNativeRequestConsumedForTest()"}),
           "native open requests must publish an owner reservation before consumption and expose the deterministic post-consume test seam");
    Expect(draw.Valid() && draw.body.find("g_menuObj") == std::string::npos &&
               draw.body.find("g_menuVisiblePublished") != std::string::npos,
           "Present draw visibility must be atomic and must never read the pump-owned menu object");

    Expect(nativePresent.Valid() &&
               nativePresent.body.find("FfxHooks::Maechen_MenuOwned()") != std::string::npos &&
               nativePresent.body.find("FfxHooks::Maechen_PumpWakePending()") != std::string::npos &&
               nativePresent.body.find("if (maechenMenuOwned || maechenPumpWake)") != std::string::npos &&
               nativePresent.body.find("FfxHooks::Maechen_PresentDraw()") == std::string::npos,
           "menu ownership or a one-shot reap wake may drive the force gate, but Present must never emit Maechen 2D");
    Expect(nativePump.Valid() &&
               nativePump.body.find("if (!FfxHooks::Maechen_MenuOwned() &&") != std::string::npos &&
               nativePump.body.find("!FfxHooks::Maechen_PumpWakePending()") != std::string::npos &&
               nativePump.body.find("NativeMenuOtherOwnerOrRequestActivePumpOnly()") != std::string::npos,
           "Maechen close may clear the force gate only when no other owner or request remains");
    Expect(legacyModalIdle.Valid() &&
               legacyModalIdle.body.find("F8Ui::LegacyModalAllocationIdle") != std::string::npos &&
               legacyModalIdle.body.find("Maechen_BlocksNativeModalAllocation") != std::string::npos &&
               legacyModalIdle.body.find("g_nativeMenu.obj") != std::string::npos &&
               legacyModalIdle.body.find("g_arenaPlusMenu.obj") != std::string::npos &&
               legacyModalIdle.body.find("g_sinMenu.obj") != std::string::npos &&
               legacyModalIdle.body.find("g_f7Menu.obj") != std::string::npos &&
               legacyModalIdle.body.find("ArenaPlusComposePick_IsActive") != std::string::npos &&
               legacyModalIdle.body.find("g_nativeHeldAction") != std::string::npos,
           "one post-Maechen predicate must reject every legacy modal, compose, and held owner");
    Expect(nativePump.Valid() && SourceTokensInOrder(nativePump.body, {
               "FfxHooks::Maechen_PumpTick(F7IsForegroundWindow())",
               "NativeMenuLegacyModalAllocationIdle()",
               "F7Sub_SpawnMenu"}) &&
               promoteDelayedArena.Valid() &&
               promoteDelayedArena.body.find("NativeMenuLegacyModalAllocationIdle()") != std::string::npos &&
               tryOpenArena.Valid() &&
               tryOpenArena.body.find("NativeMenuLegacyModalAllocationIdle()") != std::string::npos,
           "direct F7/FLAGS plus delayed and direct Arena allocation must share the post-Maechen predicate");
    Expect(delayedArenaPending.Valid() &&
               delayedArenaPending.body.find("g_arenaNpcPendingOpen") != std::string::npos &&
               delayedArenaPending.body.find("g_arenaNpcOpenDelay") != std::string::npos &&
               pumpOnlyOwners.Valid() &&
               pumpOnlyOwners.body.find("ArenaPlusNpcDelayedOpenPending()") != std::string::npos &&
               publishedOwners.Valid() &&
               publishedOwners.body.find("ArenaPlusNpcDelayedOpenPending()") != std::string::npos,
           "delayed Arena pending ownership must remain visible to both Pump and Present arbitration");
    Expect(promoteDelayedArena.Valid() && SourceTokensInOrder(promoteDelayedArena.body, {
               "NativeMenuLegacyModalAllocationIdle()",
               "NativeMenu_ReserveAndConsumeOpenRequest(",
               "&g_arenaNpcPendingOpen", "ArenaPlus_RequestOpen"}) &&
               tryOpenArena.Valid() && SourceTokensInOrder(tryOpenArena.body, {
                   "TryHandleArenaOpenRequest(",
                   "NativeMenuLegacyModalAllocationIdle()",
                   "NativeMenu_ReserveAndConsumeOpenRequest(",
                   "&g_arenaPlusWantOpen", "ArenaPlus_OpenMenuFromRequest"}),
           "Arena requests must stay queued while blocked and reserve ownership before either delayed promotion or direct allocation consumes them");
    Expect(nativePump.Valid() && SourceTokensInOrder(nativePump.body, {
               "const FfxHooks::F8Ui::ArenaOpenResult arenaResult =",
               "NativeMenuTryOpenArenaRequest()",
               "arenaResult == FfxHooks::F8Ui::ArenaOpenResult::NoRequest",
               "&g_sinWantOpen", "&g_f7WantOpenKind", "SpawnHydratedNativeMenu()"}),
           "only NoRequest may enter Sin/F7/native fallback; DeferredBlocked must retain exclusive Arena ownership");
    Expect(pumpOnlyOwners.Valid() &&
               pumpOnlyOwners.body.find("g_nativeMenu.obj") != std::string::npos &&
               pumpOnlyOwners.body.find("g_nativeHeldAction") != std::string::npos &&
               nativePresent.Valid() &&
               nativePresent.body.find("NativeMenuOtherOwnerOrRequestActivePumpOnly") == std::string::npos &&
               nativePresent.body.find("NativeMenuOtherOwnerOrRequestPublished") != std::string::npos,
           "only Pump may inspect raw native menu objects/held action; Present must consume atomic ownership publication");
    Expect(nativePump.Valid() &&
               CountSourceToken(nativePump.body, "NativeMenu_ReserveAndConsumeOpenRequest(") == 5 &&
               CountSourceToken(tryOpenArena.body,
                                "NativeMenu_ReserveAndConsumeOpenRequest(") == 1 &&
               CountSourceToken(promoteDelayedArena.body,
                                "NativeMenu_ReserveAndConsumeOpenRequest(") == 1 &&
               CountSourceToken(nativePump.body,
                    "&g_nativeWantSpawn, 0, &g_nativeOtherOwnerPublished") == 1 &&
               CountSourceToken(nativePump.body,
                    "&g_arenaPlusWantOpen, 0, &g_nativeOtherOwnerPublished") == 0 &&
               CountSourceToken(tryOpenArena.body,
                    "&g_arenaPlusWantOpen, 0, &g_nativeOtherOwnerPublished") == 1 &&
               CountSourceToken(nativePump.body,
                   "&g_sinWantOpen, 0, &g_nativeOtherOwnerPublished") == 1 &&
               CountSourceToken(nativePump.body,
                   "&g_f7WantOpenKind, -1, &g_nativeOtherOwnerPublished") == 2 &&
               CountSourceToken(nativePump.body,"&EquipmentMenu::wantOpen, 0, &g_nativeOtherOwnerPublished") == 1,
           "every native open-request consumer must reserve published ownership before clearing its request");
    Expect(publishNativeOwner.Valid() &&
               publishNativeOwner.body.find("NativeMenuOtherOwnerOrRequestActivePumpOnly()") != std::string::npos &&
               publishNativeOwner.body.find("&g_nativeOtherOwnerPublished") != std::string::npos &&
               nativePump.Valid() && SourceTokensInOrder(nativePump.body, {
                   "NativeMenu_ReserveAndConsumeOpenRequest(", "&g_nativeWantSpawn",
                   "NativeMenuPublishOwnerReservationFromPumpState()"}),
           "Pump may clear the native reservation only after confirming no raw owner or open request remains");
}

void TestMaechenBoundedUiAndRawIsolationContracts() {
    std::string adapter;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), adapter),
           "Maechen UI source must be readable");
    const SourceBlock pump = SourceFunctionBody(adapter, "void Maechen_PumpTick(bool ffxForeground)");
    const SourceBlock draw = SourceFunctionBody(adapter, "int __cdecl MaechenPumpDraw(int obj)");
    const SourceBlock append = SourceFunctionBody(
        adapter, "bool AppendMaechenInputCharacter(char value) noexcept");
    const std::string drawCode = SourceCodeOnly(draw.body);

    Expect(adapter.find("constexpr size_t kMaechenInputCapacity = Maechen::kMaxQuestionBytes + 1u") != std::string::npos &&
               append.Valid() && append.body.find("value == '\\0'") != std::string::npos &&
               pump.Valid() && pump.body.find("AppendMaechenInputCharacter") != std::string::npos &&
               pump.body.find("VK_BACK") != std::string::npos &&
               pump.body.find("VK_RETURN") != std::string::npos &&
               pump.body.find("VK_ESCAPE") != std::string::npos &&
               pump.body.find("VK_LEFT") != std::string::npos &&
               pump.body.find("VK_RIGHT") != std::string::npos,
           "pump input must be bounded and own edit/submit/close/page keys");
    Expect(adapter.find("constexpr int kMaechenMaxTextDraws = 12") != std::string::npos &&
               draw.Valid() && draw.body.find("draws < kMaechenMaxTextDraws") != std::string::npos &&
               draw.body.find("Maechen::kLinesPerPage") != std::string::npos,
           "Maechen rendering must cap text calls at 12 and page at six lines");
    Expect(draw.Valid() &&
               drawCode.find("response") == std::string::npos &&
               drawCode.find("provider") == std::string::npos &&
               drawCode.find("path") == std::string::npos &&
               drawCode.find("stats") == std::string::npos &&
               drawCode.find("reason") == std::string::npos &&
               drawCode.find("markdown") == std::string::npos &&
               drawCode.find("question") == std::string::npos &&
               drawCode.find("g_answerPages") == std::string::npos &&
               draw.body.find("CopyStableRenderSnapshot") != std::string::npos,
           "pump draw must consume only a stable pre-encoded snapshot, never raw/private text");
    Expect(adapter.find("Raw responses remain worker-owned") != std::string::npos,
           "the adapter must document WHY raw response bytes never reach the draw path");
}

void TestMaechenBuildWiringParityContracts() {
    std::string script;
    std::string project;
    Expect(ReadWholeFile(RuntimeSourcePath("build_hooks.ps1"), script) &&
               ReadWholeFile(RuntimeSourcePath("FfxHooksDll.vcxproj"), project),
           "both native build entrypoints must be readable");
    for (const char* leaf : {"MaechenCore.cpp", "MaechenHook.cpp"}) {
        Expect(CountSourceToken(script, leaf) == 1 && CountSourceToken(project, leaf) == 1,
               "CLI and vcxproj must compile each Maechen source exactly once");
    }
    for (const char* leaf : {"MaechenCore.h", "MaechenHook.h"}) {
        Expect(CountSourceToken(script, leaf) == 1 && CountSourceToken(project, leaf) == 1,
               "CLI and vcxproj must inventory each Maechen public header exactly once");
    }
    Expect(CountSourceToken(script, "winhttp.lib") == 1 &&
               CountSourceToken(project, "winhttp.lib") == 4,
           "CLI plus every vcxproj link configuration must include winhttp.lib");
}

void TestMaechenHttpsAdapterContract() {
    FakeMaechenTransport fake{};
    {
        std::lock_guard<std::mutex> lock(g_maechenLogMutex);
        g_maechenLogs.clear();
    }
    Expect(FfxHooks::Maechen_Install(&FakeMaechenLog),
           "Maechen synchronization must initialize once outside DllMain");
    TestMaechenForegroundAdapterLifecycle();
    TestMaechenQueuedFocusLossAndCoreOpenTransition();
    TestMaechenReleaseAfterPresentLossSurvivesLaggingPump();
    Expect(!FfxHooks::Maechen_SubmitRequest("pt", "Zero", 0),
           "generation zero must never own a RequestSlot");
    Expect(!FfxHooks::Maechen_SubmitRequest("pt", "No transport", 1),
           "the RT0 build must fail closed until a complete fake transport is installed");
    TestMaechenPublishedErrorTaxonomy();
    TestMaechenModalAllocationArbitration();
    TestMaechenWhitespaceOnlySubmissionPublishesInvalidQuestion(fake);
    TestDelayedArenaModalArbitrationRetainsOneOpen();
    TestBlockedArenaConfirmCannotFallThroughToAnotherModal();
    TestMaechenTransportExactSuccessContract(fake);
    TestMaechenTransportStatusTaxonomy(fake);
    TestMaechenTransportMetadataAndTimeouts(fake);
    TestMaechenTransportSingleShotAttemptCounts(fake);
    TestMaechenTransportCancellationAndBounds(fake);
    TestNativeOpenReservationHasNoFalseIdleWindow();
    TestMaechenRequestSlotLifecycle(fake);
    TestMaechenPumpOwnsCompletionObservation(fake);
    TestMaechenWorkerCreationFailureRetainsCancelOwnership(fake);
    TestMaechenTransportSourceContracts();
}

void TestF7UnsafePrototypePolicyAndSourceContainment() {
    using FfxHooks::F7UnsafePrototype;
    using FfxHooks::ResolveF7UnsafePrototype;

    const auto ultra = ResolveF7UnsafePrototype(
        F7UnsafePrototype::CustomMixUltra, false, false);
    Expect(!ultra.available && !ultra.requested && ultra.reason != nullptr &&
               strcmp(ultra.reason,
                      "Unavailable: isolated transactional output and encounter carrier are not validated.") == 0,
           "CustomMix Ultra must stay unavailable with the exact validation boundary");

    for (bool staleFlag : {false, true}) {
        for (bool staleEnvironment : {false, true}) {
            const auto legacySin = ResolveF7UnsafePrototype(
                F7UnsafePrototype::LegacySinWriter, staleFlag, staleEnvironment);
            Expect(!legacySin.available && !legacySin.requested &&
                       legacySin.reason != nullptr &&
                       strcmp(legacySin.reason,
                              "Unavailable: legacy disk writer is quarantined; no S.I.N. areas are supported.") == 0,
                   "legacy S.I.N. sources must stay ignored and cannot create requested or effective state");
        }
    }

    std::string dllmain;
    std::string sinHook;
    std::string nativeMenu;
    std::string arenaPick;
    std::string flagCatalog;
    std::string customMixRuntime;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\SinCurseHook.cpp"), sinHook) &&
               ReadWholeFile(RuntimeSourcePath("..\\NativeMenuShell\\NativeMenuShell.h"), nativeMenu) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\ArenaPlusComposePick.cpp"), arenaPick) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\F8FlagCatalog.cpp"), flagCatalog) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\CustomMixRuntime.cpp"), customMixRuntime),
           "F7 containment source surfaces must be readable");

    const char* const removedUltraTokens[] = {
        "ArenaMultiBossLab.exe",
        "ultra_manifest.json",
        "F7_Ultra.bin",
    };
    for (const char* token : removedUltraTokens) {
        char message[224] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "CustomMix Ultra containment must remove legacy unsafe token %s", token);
        ExpectSourceExcludes(dllmain, token, message);
    }
    for (const char* token : {
             "MH_CreateHook", "MH_EnableHook", "CreateProcess", "CreateFile",
             "WriteFile", "ArenaMultiBossLab", "0x1158"}) {
        char message[224] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "CustomMix RAM runtime must exclude unsafe token %s", token);
        ExpectSourceExcludes(customMixRuntime, token, message);
    }

    const SourceBlock arenaConfirm = SourceFunctionBody(
        dllmain, "static void ArenaPlus_HandleMenuConfirm(int row)");
    const SourceBlock ultraConfirm = SourceBlockAfterToken(
        arenaConfirm.body, "if (row == ARENA_PLUS_HUB_ROW_ULTRA)");
    Expect(arenaConfirm.Valid() && ultraConfirm.Valid() &&
               ultraConfirm.body.find("ArenaPlus_MixEnabled()") != std::string::npos &&
               ultraConfirm.body.find("CancelReason::NewGeneration") != std::string::npos &&
               ultraConfirm.body.find("ArenaPlus_SpawnHubMenu()") != std::string::npos &&
               ultraConfirm.body.find("ArenaPlus_SpawnUltraMenu()") != std::string::npos,
           "the Ultra hub row must open only when the reviewed F7 runtime is operational");
    Expect(dllmain.find("CustomMix Ultra [Unavailable]") == std::string::npos &&
               dllmain.find("Restart with Arena+ Master ON") == std::string::npos &&
               dllmain.find("Turn Arena+ Master ON and restart") != std::string::npos &&
               dllmain.find("Arena+ setup unavailable - check hooks log") != std::string::npos &&
               dllmain.find("Restore failed - restart required") != std::string::npos,
           "the Arena+ hub must distinguish Master OFF, setup failure and restore conflicts");

    const char* const legacySinDenylist[] = {
        "CreateProcessA",
        "WaitForSingleObject",
        "SinScaleInject.exe",
        "GraphicFieldMapLoad_SinCurseHook",
        "GetEnvironmentVariableA",
        "FFXHOOKS_ENABLE_SIN_CURSE",
        "sin_curse.flag",
        "sin_f7_intensity.flag",
        ".bin",
    };
    for (const char* token : legacySinDenylist) {
        char message[224] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "legacy S.I.N. hook must not retain source-reader/writer token %s", token);
        ExpectSourceExcludes(sinHook, token, message);
    }

    const char* const sinUiWriterDenylist[] = {
        "SinCurse_ToggleOnOff",
        "SinCurse_WriteIntensity",
        "SinCurse_CycleIntensity",
        "MoveFileA(onPath",
        "CreateFileA(onPath",
        "config\\\\sin_curse.flag",
    };
    for (const char* token : sinUiWriterDenylist) {
        char message[224] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "S.I.N. menu must not retain live flag writer token %s", token);
        ExpectSourceExcludes(dllmain, token, message);
    }

    const SourceBlock sinInstall = SourceFunctionBody(
        sinHook, "SinCurseInstallResult InstallSinCurseHook(uintptr_t moduleBase, void* logFn)");
    Expect(sinInstall.Valid() &&
               sinInstall.body.find("F7UnsafePrototype::LegacySinWriter") != std::string::npos &&
               sinInstall.body.find("result.ok = true") == std::string::npos &&
               sinInstall.body.find("InstallDetour") == std::string::npos,
           "legacy S.I.N. installation must fail closed before any detour or process setup");
    Expect(nativeMenu.find("S.I.N. Curses") != std::string::npos &&
               nativeMenu.find("S.I.N. - Unavailable") == std::string::npos &&
               dllmain.find("SIN_RAM_ROW_COUNT         8") != std::string::npos &&
               dllmain.find("Seeded encounters in Macalania") !=
                   std::string::npos,
            "the F7 S.I.N. row and submenu must publish the seeded eight-row prototype scope");
    const SourceBlock sinInput = SourceFunctionBody(
        dllmain, "static int __cdecl SinCurse_InputCb(int obj)");
    const SourceBlock sinLabels = SourceFunctionBody(
        dllmain, "static void SinCurse_BuildLabels()");
    const SourceBlock sinConfirm = SourceFunctionBody(
        dllmain, "static void SinCurse_HandleConfirm(int row)");
    Expect(sinInput.Valid() &&
               sinInput.body.find("else if (sel != SIN_RAM_ROW_SAVE)") != std::string::npos &&
               sinInput.body.find(
                   "PlaySfx(sel == SIN_RAM_ROW_BACK ? 4 : 1)") == std::string::npos,
           "S.I.N. Save confirm must not play generic success-like feedback before persistence");
    Expect(sinConfirm.Valid() && SourceTokensInOrder(sinConfirm.body, {
               "SIN_RAM_ROW_SAVE",
               "F7_SetSinRamConfig(g_sinDraft)",
               "F7_SaveConfig()",
               "g_sinSaveFeedback = saved ?",
               "NativeMenu::PlaySfx(saved ? 4 : 3)",
           }),
           "S.I.N. Save must map true to success SFX 4 and false to failure SFX 3");
    Expect(sinLabels.Valid() &&
               sinLabels.body.find("Saved for next encounter") != std::string::npos &&
               sinLabels.body.find("Save failed - memory only") != std::string::npos &&
               sinLabels.body.find(
                   "set(SIN_RAM_ROW_SAVE") != std::string::npos,
           "S.I.N. Save must expose bounded persistent-in-menu success/failure truth");
    Expect(dllmain.find("SinRam_ClearSaveFeedback();") != std::string::npos &&
               dllmain.find("g_sinDraftActive = false;\n        SinRam_ClearSaveFeedback();") !=
                   std::string::npos,
           "S.I.N. Save feedback must reset with draft edits and modal cleanup");

    Expect(dllmain.find("static const int ARENA_PLUS_PRESET_COMBO_COUNT = 5;") != std::string::npos &&
               dllmain.find("static const int ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT = 3;") != std::string::npos &&
               dllmain.find("g_arenaPlusMixRequiredSlots = static_cast<uint8_t>(row + 3)") != std::string::npos &&
               dllmain.find("ArenaPlusComposePick_Open(combo)") == std::string::npos &&
               dllmain.find("ArenaPlus_LaunchComboBattleFromPump(combo)") != std::string::npos,
           "fixed Mix uses the RAM editor; preset gauntlets retain their separate launch path");

    const size_t composeOpen = arenaPick.find("bool ArenaPlusComposePick_Open(int combo)");
    const size_t composeOverride =
        arenaPick.find("bool ArenaPlusComposePick_ApplyLaunchRouteOverride");
    const size_t composeOpenGate =
        arenaPick.find("ArenaPlusComposePick_IsAvailable()", composeOpen);
    const size_t composeOverrideGate =
        arenaPick.find("ArenaPlusComposePick_IsAvailable()", composeOverride);
    Expect(composeOpen != std::string::npos && composeOverride != std::string::npos &&
               composeOpenGate > composeOpen && composeOpenGate < composeOverride &&
               composeOverrideGate > composeOverride,
           "production quarantine must block both picker entry and cached launch-route reads");
    const SourceBlock composeLaunch = SourceFunctionBody(
        dllmain, "static bool ArenaPlus_LaunchComboBattleFromPump(int combo)");
    Expect(composeLaunch.Valid() && SourceTokensInOrder(composeLaunch.body, {
               "ArenaPlusComposePick_IsCustomMixCombo(combo)",
               "!ArenaPlusComposePick_IsAvailable()",
               "return false;",
               "ArenaPlus_ComboRouteMapped(combo)",
           }),
           "the final combo launcher must reject quarantined Custom Mix before any fallback route");
    Expect(flagCatalog.find(
               "LIVE - F7 Custom Mix editor; requires Arena+ Master.") !=
               std::string::npos,
           "the F8 catalog describes the RAM editor and its Arena+ prerequisite");
    Expect(dllmain.find(
               "\"arena_plus.compose_f7\", FfxHooks::F8RuntimeAvailability::ProducerUnavailable, false") !=
               std::string::npos,
           "startup must initially fail closed until the RAM editor producer is ready");
}

struct ResolverOwnerInstallSpy {
    int calls = 0;
    bool succeeds = true;
};

bool ResolverOwnerInstallAttempt(void* context) noexcept {
    ResolverOwnerInstallSpy& spy = *static_cast<ResolverOwnerInstallSpy*>(context);
    ++spy.calls;
    return spy.succeeds;
}

void TestResolverOwnerStartupPolicyAndCompositionContracts() {
    using FfxHooks::ResolverOwner::ExecuteSharedResolverStartup;
    using FfxHooks::ResolverOwner::PlanSharedResolverStartup;
    using FfxHooks::ResolverOwner::SharedResolverStartupOwner;
    using FfxHooks::ResolverOwner::SharedResolverStartupReason;
    using FfxHooks::ResolverOwner::SharedResolverStartupReasonName;

    struct GateCase {
        bool resolverRequested;
        bool f7Requested;
        SharedResolverStartupOwner owner;
        SharedResolverStartupReason reason;
        int expectedInstallCalls;
    };
    const GateCase cases[] = {
        {false, false, SharedResolverStartupOwner::None,
         SharedResolverStartupReason::NotRequested, 0},
        {true, false, SharedResolverStartupOwner::ResolverLog,
         SharedResolverStartupReason::ResolverLogSelected, 1},
        {false, true, SharedResolverStartupOwner::F7Difficulty,
         SharedResolverStartupReason::F7Reserved, 0},
        {true, true, SharedResolverStartupOwner::F7Difficulty,
         SharedResolverStartupReason::ConflictF7ResolverOwner, 0},
    };

    for (const GateCase& gate : cases) {
        const auto plan = PlanSharedResolverStartup(
            gate.resolverRequested, gate.f7Requested);
        ResolverOwnerInstallSpy spy{};
        const auto execution = ExecuteSharedResolverStartup(
            plan, &spy, &ResolverOwnerInstallAttempt);
        Expect(plan.owner == gate.owner && plan.reason == gate.reason,
               "all four startup gate combinations must select one deterministic resolver owner");
        Expect(spy.calls == gate.expectedInstallCalls,
               "only the ResolverLog-only gate combination may invoke its install callback");
        Expect(execution.installAttempted == (gate.expectedInstallCalls == 1),
               "startup execution must report whether it actually invoked the resolver installer");
        Expect(execution.installed == (gate.expectedInstallCalls == 1),
               "startup execution must never report installation for a skipped resolver owner");
    }

    const auto conflict = PlanSharedResolverStartup(true, true);
    Expect(std::strcmp(SharedResolverStartupReasonName(conflict.reason),
                       "CONFLICT_F7_RESOLVER_OWNER") == 0,
           "the both-ON conflict must expose one stable English reason for logs and UI status");
    const FfxHooks::F8FlagSpec* resolverFlag =
        FfxHooks::FindF8Flag("arena_plus.resolver_log");
    Expect(resolverFlag != nullptr,
           "the ResolverLog startup owner must map to one catalog row");
    if (resolverFlag) {
        Expect(FfxHooks::PublishF8RuntimeStatus(
                   resolverFlag->gate.canonicalKey,
                   F8RuntimeAvailability::Conflict, false, false) &&
                   FfxHooks::GetF8RuntimeStatus(*resolverFlag).availability ==
                       F8RuntimeAvailability::Conflict,
               "a restart-required ResolverLog row must retain its published startup conflict");
        Expect(FfxHooks::PublishF8RuntimeStatus(
                   resolverFlag->gate.canonicalKey,
                   F8RuntimeAvailability::NotApplicable, false, false),
               "the focused ResolverLog status probe must restore the catalog default");
    }
    ResolverOwnerInstallSpy failedInstall{};
    failedInstall.succeeds = false;
    const auto failedExecution = ExecuteSharedResolverStartup(
        PlanSharedResolverStartup(true, false), &failedInstall,
        &ResolverOwnerInstallAttempt);
    Expect(failedInstall.calls == 1 && failedExecution.installAttempted &&
               !failedExecution.installed,
           "an attempted ResolverLog install must preserve its real failure result");

    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "dllmain must be readable for shared resolver ownership source contracts");
    const SourceBlock startup = SourceBlockAfterToken(
        dllmain, "static void InstallArenaResolverLogFromStartupPlan(");
    const SourceBlock attempt = SourceBlockAfterToken(
        dllmain, "static bool AttemptResolverLogInstall(void* context)");
    const SourceBlock installHooks = SourceFunctionBody(dllmain, "static void InstallHooks()");
    const std::string dllmainCode = SourceCodeOnly(dllmain);
    Expect(startup.Valid() && attempt.Valid() && installHooks.Valid() &&
               CountSourceToken(dllmainCode, "InstallResolverLogHook(") == 1 &&
               attempt.body.find("InstallResolverLogHook(") != std::string::npos &&
               startup.body.find("ExecuteSharedResolverStartup(") != std::string::npos &&
               startup.body.find("F8RuntimeAvailability::Conflict") != std::string::npos &&
               startup.body.find("SharedResolverStartupReasonName") != std::string::npos,
           "ResolverLog installation must have one callback path guarded by the shared-owner policy and truthful conflict publication");
    const size_t installStart = dllmainCode.find("static void InstallHooks()");
    const std::string installTail = installStart == std::string::npos
        ? std::string{}
        : dllmainCode.substr(installStart);
    const SourceBlock worker = SourceFunctionBody(dllmain, "static DWORD WINAPI HooksWorkerThread(LPVOID)");
    const size_t captureGates = worker.body.find("CaptureF8StartupGates()");
    const bool captureBeforeInstall = SourceTokensInOrder(worker.body, {"Config::Load()", "CaptureF8StartupGates()", "InstallHooks()"}) &&
        installHooks.body.find("CaptureF8StartupGates()") == std::string::npos;
    const size_t resolverGate = installTail.find("ArenaPlus_ResolverLogEnabled()");
    const size_t f7Gate = installTail.find("Config::CheckEnabled(", resolverGate);
    const size_t sharedRequest = installTail.find(
        "SharedBattleRuntime::AnyConsumerRequiresRuntime(");
    const size_t seymourLiveProducer = installTail.find(
        "SharedBattleRuntime::kSeymourLiveProducerRequiresInfrastructure");
    const size_t resolverPlan = installTail.find("PlanSharedResolverStartup(");
    const size_t resolverInstall = installTail.find(
        "InstallArenaResolverLogFromStartupPlan(");
    const size_t battleInstallGate = installTail.find(
        "F7Difficulty::ShouldInstallAtStartup(minHookReady, validateOnly)");
    const size_t f7Install = installTail.find("F7_InstallHooks(");
    const size_t resolverSharedArgument = installTail.find(
        "sharedBattleRuntimeRequested", resolverPlan);
    const size_t battleSharedArgument = installTail.find(
        "sharedBattleRuntimeRequested", battleInstallGate);
    const bool startupTokensFound = captureGates != std::string::npos &&
               resolverGate != std::string::npos && f7Gate != std::string::npos &&
               sharedRequest != std::string::npos && seymourLiveProducer != std::string::npos &&
               resolverPlan != std::string::npos && resolverInstall != std::string::npos &&
               battleInstallGate != std::string::npos && f7Install != std::string::npos &&
               resolverSharedArgument != std::string::npos &&
               battleSharedArgument != std::string::npos;
    const bool startupOrderValid = startupTokensFound &&
               captureBeforeInstall && resolverGate < f7Gate &&
               f7Gate < sharedRequest && sharedRequest < seymourLiveProducer &&
               seymourLiveProducer < resolverPlan &&
               resolverPlan < resolverInstall && resolverInstall < battleInstallGate &&
               battleInstallGate < f7Install &&
               resolverSharedArgument < resolverInstall &&
               battleSharedArgument < f7Install;
    if (!startupOrderValid) {
        std::fprintf(stderr,
                     "startup positions capture=%zu resolver=%zu f7=%zu shared=%zu seymour=%zu plan=%zu "
                     "planArg=%zu resolverInstall=%zu battleGate=%zu battleArg=%zu f7Install=%zu\n",
                     captureGates, resolverGate, f7Gate, sharedRequest, seymourLiveProducer, resolverPlan,
                     resolverSharedArgument, resolverInstall, battleInstallGate,
                     battleSharedArgument, f7Install);
    }
    Expect(startupOrderValid,
           "startup must reserve the resolver for any shared battle consumer and reuse that immutable request for admission");

    const SourceBlock technicalStatus = SourceFunctionBody(
        dllmain, "static void F8BuildSelectedStatus(int sel, char* out, size_t outSize)");
    Expect(technicalStatus.Valid() &&
               technicalStatus.body.find("F8RuntimeAvailability::NotApplicable") !=
                   std::string::npos &&
               technicalStatus.body.find("STARTUP %s") != std::string::npos,
           "restart-required rows with a published startup result must show conflict/failure instead of generic restart text");
    Expect(technicalStatus.body.find("\"EXT %s: %s\"") != std::string::npos &&
               technicalStatus.body.find("REQ %s / EXT") == std::string::npos,
           "an external override must surface its artifact inside the status budget — "
           "a blown 95-char line collapses to STATUS UNAVAILABLE and hides the blocker");
    // R7-UX-B2: after Enable/Disable Supported the per-row truth lives in
    // g_f8BulkStatus — the selected bulk row must render it on the technical line
    // instead of leaving the user staring at a blank status.
    Expect(technicalStatus.body.find("F7RT_BULK") != std::string::npos &&
               technicalStatus.body.find("g_f8BulkStatus[0]") != std::string::npos &&
               technicalStatus.body.find("g_f8BulkStatus") != std::string::npos,
           "a selected bulk row must surface the persisted bulk summary on the status line");

    // WHY: Difficulty owns three shared machine entries. Future Seymour, S.I.N., CustomMix, or
    // other adapters must compose through that owner instead of quietly creating a second detour.
    const std::array<const char*, 6> ownerFiles = {
        "F7DifficultyCore.h", "F7DifficultyCore.cpp", "F7InLive.h", "F7InLive.cpp",
        "ResolverLogHook.h", "ResolverLogHook.cpp",
    };
    const std::array<const char*, 21> sharedTargetTokens = {
        "kresolveencounterrva", "kinitsystemscenerva",
        "kactorinitializerrva",
        "rva_ffx_field_resolve_encounter_token",
        "0x003828b0", "0x3828b0", "0x00383ed0", "0x383ed0",
        "0x0039c130", "0x39c130",
        "0x007828b0", "0x7828b0", "0x00783ed0", "0x783ed0",
        "0x0079c130", "0x79c130",
        "kactorpopulaterva", "0x00384010", "0x384010", "0x00784010", "0x784010",
    };
    bool scannedAll = true;
    bool targetsDisjoint = true;
    std::string offender;
    const std::string hooksRoot = RuntimeSourcePath("hooks");
    const auto scanPattern = [&](const char* extension) {
        WIN32_FIND_DATAA entry{};
        const std::string pattern = hooksRoot + "\\*" + extension;
        HANDLE find = FindFirstFileA(pattern.c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) {
            scannedAll = false;
            return;
        }
        do {
            const std::string name = entry.cFileName;
            bool ownerFile = false;
            for (const char* allowed : ownerFiles) ownerFile |= name == allowed;
            if (ownerFile) continue;
            std::string source;
            if (!ReadWholeFile(hooksRoot + "\\" + name, source)) {
                scannedAll = false;
                offender = name;
                continue;
            }
            std::string code = SourceCodeOnly(source);
            std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            for (const char* token : sharedTargetTokens) {
                if (code.find(token) != std::string::npos) {
                    targetsDisjoint = false;
                    offender = name;
                }
            }
        } while (FindNextFileA(find, &entry) != FALSE);
        FindClose(find);
    };
    scanPattern(".h");
    scanPattern(".cpp");
    if (!scannedAll || !targetsDisjoint) {
        std::fprintf(stderr, "shared resolver target source offender: %s\n", offender.c_str());
    }
    Expect(scannedAll && targetsDisjoint,
           "non-owner hook adapters must not claim Difficulty's three shared hook targets");

    std::string sharedBattleHeader;
    std::string sharedBattleSource;
    std::string seymourHookSource;
    std::string f7Source;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\SharedBattleRuntime.h"), sharedBattleHeader) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\SharedBattleRuntime.cpp"), sharedBattleSource) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\SeymourBattleHook.cpp"), seymourHookSource) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\F7InLive.cpp"), f7Source),
           "shared battle owner and Seymour consumer sources must be readable");
    const std::string sharedBattleCode = SourceCodeOnly(
        sharedBattleHeader + "\n" + sharedBattleSource);
    const std::string seymourHookCode = SourceCodeOnly(seymourHookSource);
    const std::string f7Code = SourceCodeOnly(f7Source);
    Expect(f7Code.find("{g_base + kResolveEncounterRva") != std::string::npos &&
               f7Code.find("{g_base + kInitSystemSceneRva") != std::string::npos &&
               f7Code.find("{g_base + kActorPopulateRva") != std::string::npos &&
               f7Code.find(
                   "if (!flagGate && !difficultyRequested && !sinRequested && !sharedBattleRuntimeRequested && !arenaMixRequested)") !=
                   std::string::npos &&
               f7Code.find("InstallDifficultyDetours(") != std::string::npos,
           "a Seymour-only LIVE producer must reserve ResolverLog because F7 installs one "
           "exact retained three-target ownership batch; saved Difficulty and S.I.N. join "
           "the requester set without changing single-owner structure");
    Expect(f7Code.find("F7_RegisterInitSceneComposer(") != std::string::npos &&
               f7Code.find("F7_UnregisterInitSceneComposer(") != std::string::npos &&
               f7Code.find("SharedBattleRuntime::RunInitScene(") != std::string::npos &&
               sharedBattleCode.find("kBattleStateInitSceneReturnRva") != std::string::npos,
           "F7 must remain the single InitScene owner and expose only the reviewed composer seam");
    Expect(seymourHookCode.find("F7_RegisterInitSceneComposer(&g_entryComposer)") !=
                   std::string::npos &&
               seymourHookCode.find("kExitTargetRva") != std::string::npos &&
               seymourHookCode.find("kInitSystemSceneRva") == std::string::npos &&
               seymourHookCode.find("0x00383ed0") == std::string::npos &&
               seymourHookCode.find("0x383ed0") == std::string::npos,
           "Seymour must consume F7's InitScene seam and own only its unique exit target");
    for (const char* forbidden : {
             "VirtualProtect(", "WriteProcessMemory(", "EncodeRel32Call(",
             "InstallTwoCallsitePatch(", "MH_Uninitialize(", "MH_RemoveHook("}) {
        char message[256] = {};
        std::snprintf(message, sizeof(message),
                      "Seymour production source must exclude raw patch/lifecycle token %s",
                      forbidden);
        ExpectSourceExcludes(seymourHookCode, forbidden, message);
    }
    Expect(seymourHookSource.find(
               "[f8-runtime] key=%s effective=%d source=%s state=applied readback=01") !=
                   std::string::npos &&
               seymourHookSource.find(
               "[f8-runtime] key=%s effective=%d source=%s state=restored readback=00") !=
                   std::string::npos &&
               seymourHookSource.find("[f8-runtime] key=%s failure=%s") !=
                   std::string::npos,
           "Seymour must emit the exact RuntimeAcknowledged apply/restore anchors and structured failure record consumed by RT2");
    Expect(seymourHookCode.find(
               "case State::PendingBattle: return F8RuntimeAvailability::Pending;") !=
                   std::string::npos &&
               dllmain.find("boosters.playable_seymour") != std::string::npos &&
               dllmain.find("ARMED BATTLE") != std::string::npos,
           "Seymour must expose truthful AVAILABLE, ARMED BATTLE, APPLIED, and RESTORE PENDING UI states");
    const SourceBlock removeSeymour = SourceFunctionBody(
        seymourHookSource, "bool RemoveSeymourBattleHook()");
    const SourceBlock startSeymour = SourceBlockAfterToken(
        seymourHookSource, "bool StartSeymourBattleHook(");
    const SourceBlock requestSeymourStop = SourceFunctionBody(
        seymourHookSource, "void RequestSeymourBattleStop()");
    const SourceBlock notifySeymourProducer = SourceFunctionBody(
        seymourHookSource,
        "void NotifySeymourBattlePresentProducer(bool ready, bool terminalFailure)");
    const SourceBlock prepareSeymourTeardown = SourceFunctionBody(
        seymourHookSource, "static TeardownPreparation PrepareTeardownAdmission()");
    const SourceBlock removeHooks = SourceFunctionBody(
        dllmain, "static void RemoveHooks()");
    Expect(removeSeymour.Valid() &&
               removeSeymour.body.find("AdapterPublication::RestorePending") ==
                   std::string::npos,
           "a roster-owned teardown retry must remain Installed and may not report success before the consumer is retired");
    Expect(startSeymour.Valid() &&
               startSeymour.body.find("ArmTeardown(") != std::string::npos &&
               startSeymour.body.find("AdvanceTeardown(") != std::string::npos &&
               startSeymour.body.find("(void)RetireExitDetour(") == std::string::npos,
           "ComposerConflict rollback must publish through the retryable teardown machine and may not discard retirement failure");
    Expect(startSeymour.Valid() &&
               startSeymour.body.find("ClassifyProducerAtInstall(") !=
                   std::string::npos &&
               startSeymour.body.find(
                   "ProducerStartupDisposition::ProducerUnavailable") !=
                   std::string::npos &&
               startSeymour.body.find("F8RuntimeAvailability::ProducerUnavailable") !=
                   std::string::npos &&
               startSeymour.body.find("producerReady ?") == std::string::npos,
           "a Terminal producer published before Start must remain PRODUCER UNAVAILABLE instead of being downgraded to PENDING");
    Expect(notifySeymourProducer.Valid() &&
               notifySeymourProducer.body.find("PublishProducerState(") !=
                   std::string::npos &&
               notifySeymourProducer.body.find("g_producer.store(") ==
                   std::string::npos,
           "Present producer publication must use the absorbing atomic state transition");
    Expect(removeSeymour.Valid() &&
               SourceTokensInOrder(removeSeymour.body, {
                   "RequestStop(&g_commands)",
                   "PrepareTeardownAdmission()",
                   "AdvanceTeardown("
               }) &&
               removeSeymour.body.find("F7_UnregisterInitSceneComposer(") ==
                   std::string::npos &&
               removeSeymour.body.find("RetireExitDetour(") == std::string::npos,
           "normal Seymour teardown must close/drain once and delegate exact retry phases to the machine");
    Expect(prepareSeymourTeardown.Valid() &&
               prepareSeymourTeardown.body.find("CloseAdmission(&g_admission)") !=
                   std::string::npos &&
               prepareSeymourTeardown.body.find("DrainCallbacks(") !=
                   std::string::npos &&
               prepareSeymourTeardown.body.find("RosterOwnershipClear()") !=
                   std::string::npos,
           "normal-context teardown preparation must prove exclusive roster ownership and a closed drained admission");
    Expect(requestSeymourStop.Valid() &&
               requestSeymourStop.body.find("RequestStop(&g_commands)") !=
                   std::string::npos &&
               requestSeymourStop.body.find("CloseAdmission(&g_admission)") !=
                   std::string::npos &&
               requestSeymourStop.body.find("DrainCallbacks(") == std::string::npos &&
               requestSeymourStop.body.find("Sleep(") == std::string::npos &&
               requestSeymourStop.body.find("AdvanceTeardown(") == std::string::npos,
           "DllMain Seymour stop must remain nonblocking and must not claim lifecycle ownership cleanup");
    Expect(removeHooks.Valid() &&
               SourceTokensInOrder(removeHooks.body, {
                   "RemoveSeymourBattleHook()",
                   "if (seymourRetired)",
                   "F7_RemoveHooks()"
               }),
           "global teardown must not retire F7's shared battle batch until Seymour retirement succeeds");
    const std::string minHookFailure = BoundedSourceSection(
        dllmain, "FfxHooks::F7AiSwap_ReportSetupFailure(LogLine);",
        "ArenaPlus_RestorePendingComposeOnBoot(validateOnly);");
    Expect(!minHookFailure.empty() &&
               minHookFailure.find("PublishResolvedF8Status(") != std::string::npos &&
               minHookFailure.find("\"boosters.playable_seymour\"") !=
                   std::string::npos &&
               minHookFailure.find("F8RuntimeAvailability::ProducerUnavailable") !=
                   std::string::npos,
           "process-global MinHook failure must truthfully publish Seymour PRODUCER UNAVAILABLE");
}

void TestDashboardFocusedHotkeyAdmission() {
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(nullptr, true, true, false),
           "dashboard hotkey must reject a null state");

    FfxHooks::DashHotkeyState state{};
    Expect(FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "a foreground plain press must produce one rising edge");
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "a held key must not produce a second edge");
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, false, false),
           "a key release must not produce an edge");
    Expect(FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "a new foreground press after release must produce a fresh edge");

    state = {};
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, false, true, false),
           "a background press must not produce an edge");
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "focus returning while the key is still held must not produce an edge");

    state = {};
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, true),
           "a modifier chord must suppress the hotkey edge");
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "releasing the modifier while the key stays held must remain suppressed");
    Expect(!FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, false, false),
           "physical key release must only re-arm the admission state");
    Expect(FfxHooks::DashConsumeFocusedHotkeyEdge(&state, true, true, false),
           "a plain press after full release must produce an edge");
}

void TestNativeMenuFunctionKeySafetyContracts() {
    std::string dllmain;
    std::string dashboard;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain) &&
               ReadWholeFile(RuntimeSourcePath("hooks\\InGameMenuDashboard.cpp"), dashboard),
           "native producer and dashboard adapter sources must be readable");
    const SourceBlock presentTick = SourceFunctionBody(
        dllmain, "static void NativeMenu_PresentTick()");
    const SourceBlock nativeStart = SourceFunctionBody(
        dllmain, "static bool StartNativeMenuIfEnabled()");
    const SourceBlock nativeStop = SourceFunctionBody(
        dllmain, "static void StopNativeMenu()");
    const SourceBlock titleGuard = SourceFunctionBody(
        dllmain, "static int __cdecl NativeTextOutline_MenuGuard(\n    void* renderState, void* glyphMetrics, float scale)");
    const SourceBlock titleGuardStart = SourceFunctionBody(
        dllmain, "static bool StartNativeTextOutlineGuard()");
    const SourceBlock installHooks = SourceFunctionBody(
        dllmain, "static void InstallHooks()");
    const SourceBlock dashTick = SourceFunctionBody(
        dashboard, "void Dash_Tick(bool foreground)");
    const SourceBlock nativePump = SourceFunctionBody(
        dllmain, "static int __cdecl NativeMenu_PumpHook(unsigned int a1)");
    const SourceBlock closeTransition = SourceFunctionBody(
        dllmain, "FfxHooks::F7Ui::CloseDestination destination)");
    const SourceBlock drainPending = SourceFunctionBody(
        dllmain, "static bool NativeMenuHubCloseDrainPending()");
    const SourceBlock stillOwned = SourceFunctionBody(
        dllmain, "static bool NativeMenuHubObjectStillOwned(int obj)");
    const SourceBlock queueDrain = SourceFunctionBody(
        dllmain, "static void NativeMenuQueueHubCloseDrain(int closingObject, bool pumpAvailable)");
    const SourceBlock pollDrain = SourceFunctionBody(
        dllmain, "static void NativeMenuPollHubCloseDrainAfterPump()");
    const SourceBlock abortDrain = SourceFunctionBody(
        dllmain, "static void NativeMenuAbortHubCloseDrainForStop()");
    const SourceBlock pumpOnlyOwners = SourceFunctionBody(
        dllmain, "static bool NativeMenuOtherOwnerOrRequestActivePumpOnly()");
    const SourceBlock publishedOwners = SourceFunctionBody(
        dllmain, "static bool NativeMenuOtherOwnerOrRequestPublished()");

    Expect(presentTick.Valid() && SourceTokensInOrder(presentTick.body, {
               "ProductionTick(GetTickCount64())",
               "InterlockedCompareExchange(&g_nativeMenuProducerReady, 0, 0) == 0) return",
               "const bool f7Foreground = F7IsForegroundWindow()",
               "FfxHooks::Dash_Tick(f7Foreground)",
               "GetAsyncKeyState(g_nativeMenuHotkey)"}),
           "Present must gate every function-key path on the native pump producer before any sampling");
    Expect(presentTick.Valid() &&
               presentTick.body.find("s_hkChordSuppressed") != std::string::npos &&
               presentTick.body.find("GetAsyncKeyState(VK_CONTROL)") != std::string::npos &&
               presentTick.body.find("GetAsyncKeyState(VK_MENU)") != std::string::npos &&
               presentTick.body.find("GetAsyncKeyState(VK_SHIFT)") != std::string::npos &&
               presentTick.body.find("!s_hkChordSuppressed") != std::string::npos,
           "the configured F7 hotkey must suppress the whole modifier chord until physical release");
    Expect(presentTick.Valid() &&
               presentTick.body.find("NativeMenu::OurDraw(g_nativeMenu.obj)") == std::string::npos &&
               presentTick.body.find("ArenaPlus_Draw(g_arenaPlusMenu.obj)") == std::string::npos,
           "Present must never enqueue F7 or Arena game-native 2D batches outside the original pump");
    Expect(nativeStart.Valid() && SourceTokensInOrder(nativeStart.body, {
               "InterlockedExchange(&g_nativeMenuProducerReady, 0)",
               "g_nativeMenuPumpDetour->hook()",
               "InterlockedExchange(&g_nativeMenuProducerReady, 1)"}) &&
               nativeStop.Valid() && SourceTokensInOrder(nativeStop.body, {
               "InterlockedExchange(&g_nativeMenuProducerReady, 0)",
               "F7CloseTransition("}),
           "the Present producer must publish only after pump-hook success and revoke before stop work");
    Expect(titleGuardStart.Valid() &&
               titleGuardStart.body.find("g_base + 0x4FAE40u") != std::string::npos &&
               titleGuardStart.body.find("(0x4FAE40u - 0x400000u)") == std::string::npos &&
               titleGuardStart.body.find("catch (const std::exception& ex)") != std::string::npos &&
               titleGuardStart.body.find("catch (...)") != std::string::npos &&
               titleGuardStart.body.find("if (!ok)") != std::string::npos &&
               titleGuardStart.body.find("return false") != std::string::npos,
           "the native text-outline guard must install at the RVA-fixed address and fail closed");
    Expect(installHooks.Valid() && SourceTokensInOrder(installHooks.body, {
               "NativeMenuArmedFromConfig()",
               "StartNativeTextOutlineGuard()",
               "StartNativeMenuIfEnabled()"}) &&
               installHooks.body.find(
                   "WARN NativeMenu disabled: native text-outline guard unavailable") !=
                   std::string::npos,
           "the native menu must fail closed when its required text-outline guard is absent");
    Expect(titleGuard.Valid() &&
               titleGuard.body.find("FfxHooks::Maechen_MenuOwned()") != std::string::npos &&
               titleGuard.body.find("NativeMenuHubCloseDrainPending()") != std::string::npos &&
               titleGuard.body.find("g_nativeMenuTitleGuardRequired") == std::string::npos &&
               dllmain.find("static volatile LONG     g_nativeMenuTitleGuardRequired") == std::string::npos,
           "the text-outline guard must stop with concrete custom-menu ownership and restore vanilla reward text");
    Expect(dashTick.Valid() &&
               dashTick.body.find(
                   "DashConsumeFocusedHotkeyEdge(\n        &g_hotkeyState, foreground,") !=
                   std::string::npos &&
               SourceTokensInOrder(dashTick.body, {
                   "if (!foreground)",
                   "InterlockedExchange(&g_pendingF8, 0)",
                   "return"}),
           "the dashboard must route focus through the pure helper and drop pending input while backgrounded");

    Expect(closeTransition.Valid() && SourceTokensInOrder(closeTransition.body, {
               "NativeMenu::CloseMenu(g_nativeMenu)",
               "NativeMenu::ReleaseModalIfOwned(closingObject)",
               "NativeMenuQueueHubCloseDrain("}),
           "F7 hub close must queue the close drain after CloseMenu and modal release");
    Expect(nativePump.Valid() && SourceTokensInOrder(nativePump.body, {
               "const int closingHubObject = g_nativeMenu.obj",
               "NativeMenu::CloseMenu(g_nativeMenu)",
               "NativeMenu::ReleaseModalIfOwned(closingHubObject)",
               "NativeMenuQueueHubCloseDrain(closingHubObject, true)",
               "NativeMenu::DispatchConfirm(p.row)"}),
           "hub confirmation must preserve, release, and drain the closing object before dispatch can clear ownership");
    Expect(closeTransition.Valid() && SourceTokensInOrder(closeTransition.body, {
               "const bool keepCleanupPump = NativeMenuHubCloseDrainPending()",
               "InterlockedExchange(&g_forceSubsystem, keepCleanupPump ? 1 : 0)",
               "if (!keepCleanupPump)",
               "NativeMenuForceGateClear()"}),
           "the terminal force/gate and keep-alive clear must be skipped while the hub close drain is pending");
    Expect(nativePump.Valid() && SourceTokensInOrder(nativePump.body, {
               "(g_nativeMenuPumpTramp)(a1)",
               "NativeMenuPollHubCloseDrainAfterPump()",
               "!NativeMenuOtherOwnerOrRequestActivePumpOnly()"}),
           "the hub close drain must be polled after the trampoline and before the no-owner force release");
    Expect(pumpOnlyOwners.Valid() && publishedOwners.Valid() &&
               pumpOnlyOwners.body.find("NativeMenuHubCloseDrainPending()") != std::string::npos &&
               publishedOwners.body.find("NativeMenuHubCloseDrainPending()") != std::string::npos,
           "both ownership predicates must treat a pending hub close drain as owned");
    Expect(stillOwned.Valid() &&
               stillOwned.body.find("NativeMenu::POOL_VA") != std::string::npos &&
               stillOwned.body.find("NativeMenu::POOL_STRIDE") != std::string::npos &&
               stillOwned.body.find("NativeMenu::O_ACTIVE") != std::string::npos &&
               stillOwned.body.find("NativeMenu::O_UPDATE") != std::string::npos &&
               stillOwned.body.find("NativeMenu::O_DRAW") != std::string::npos &&
               stillOwned.body.find("&NativeMenu::OurListInputCb") != std::string::npos &&
               stillOwned.body.find("&NativeMenu::OurDraw") != std::string::npos,
           "drain ownership must prove pool alignment plus active/update/draw identity");
    Expect(queueDrain.Valid() &&
               queueDrain.body.find("InterlockedExchange(&g_forceSubsystem, 1)") != std::string::npos &&
               queueDrain.body.find("if (!pumpAvailable)") != std::string::npos &&
               queueDrain.body.find("NativeMenu::Reset(closingObject)") != std::string::npos &&
               queueDrain.body.find("NativeMenuHubObjectStillOwned(closingObject)") != std::string::npos,
           "the queue must retain force, ownership-check, and reset synchronously when no pump remains");
    Expect(pollDrain.Valid() && SourceTokensInOrder(pollDrain.body, {
               "kNativeMenuHubCloseDrainMaxPumpPasses",
               "NativeMenuHubObjectStillOwned(obj)",
               "NativeMenu::Reset(obj)"}) &&
               CountSourceToken(pollDrain.body,
                   "InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, pending)") == 2 &&
               drainPending.Valid() &&
               drainPending.body.find(
                   "InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, 0)") !=
                   std::string::npos,
           "the poll must stay bounded, reset only a still-owned object, and clear by exact-object CAS");
    Expect(presentTick.Valid() && SourceTokensInOrder(presentTick.body, {
               "if (NativeMenuHubCloseDrainPending() || EquipmentMenu::NeedsPump())",
               "InterlockedExchange(&g_forceSubsystem, 1)",
               "const bool forcedByCustomMenu"}),
           "Present must republish force before the snapshot while hub or Workshop work is pending");
    Expect(abortDrain.Valid() && SourceTokensInOrder(abortDrain.body, {
               "NativeMenuHubObjectStillOwned(obj)",
               "NativeMenu::Reset(obj)",
               "InterlockedCompareExchange(&g_nativeMenuHubCloseDrainObj, 0, pending)",
               "InterlockedExchange(&g_forceSubsystem, 0)",
               "NativeMenuForceGateClear()"}),
           "stop must ownership-check, reset, clear the exact record, then release force through the shared gate-and-keep-alive clear");
    Expect(nativeStop.Valid() && SourceTokensInOrder(nativeStop.body, {
               "InterlockedExchange(&g_nativeMenuProducerReady, 0)",
               "F7CloseTransition(",
               "WARN NativeMenu stop exception",
               "NativeMenuAbortHubCloseDrainForStop()",
               "g_nativeMenuPumpDetour->unHook()"}),
           "Stop must abort any pending hub close drain after the close calls and before unhook");
}

void TestNativeMenuKeepAliveContract() {
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "dllmain source must be readable for the keep-alive contract");

    const SourceBlock publish = SourceFunctionBody(
        dllmain, "static void NativeMenuForceGatePublish()");
    const SourceBlock clear = SourceFunctionBody(
        dllmain, "static void NativeMenuForceGateClear()");
    const SourceBlock presentTick = SourceFunctionBody(
        dllmain, "static void NativeMenu_PresentTick()");
    const SourceBlock nativePump = SourceFunctionBody(
        dllmain, "static int __cdecl NativeMenu_PumpHook(unsigned int a1)");
    const SourceBlock closeTransition = SourceFunctionBody(
        dllmain, "FfxHooks::F7Ui::CloseDestination destination)");
    const SourceBlock abortDrain = SourceFunctionBody(
        dllmain, "static void NativeMenuAbortHubCloseDrainForStop()");
    const SourceBlock returnFlags = SourceFunctionBody(
        dllmain, "static void F8ReturnFlagsToGame()");
    const SourceBlock ultraClose = SourceFunctionBody(
        dllmain, "static void ArenaPlus_Ultra_CloseAfterLaunch()");

    Expect(dllmain.find("kNativeMenuRequestRva = 0x18408ACu") != std::string::npos &&
               dllmain.find("kNativeMenuKeepAliveBit = 0x80000000u") != std::string::npos &&
               dllmain.find("kNativeMenuGateRva = 0x13407E4u") != std::string::npos,
           "the keep-alive contract must name the vanilla request bitmask, system bit, and gate RVAs");
    Expect(dllmain.find("static void NativeMenuForceGatePublish()") <
               dllmain.find("static void NativeMenuAbortHubCloseDrainForStop()"),
           "the force-gate helpers must be declared before their first teardown caller");
    Expect(publish.Valid() && SourceTokensInOrder(publish.body, {
               "kNativeMenuGateRva - 0x400000u)) = 1",
               "kNativeMenuRequestRva - 0x400000u)) |= kNativeMenuKeepAliveBit"}) &&
               publish.body.find("InterlockedExchange(&g_nativeMenuKeepAlivePublished, 1)") !=
                   std::string::npos,
           "publish must set the gate and the keep-alive system bit together under SEH");
    Expect(clear.Valid() && SourceTokensInOrder(clear.body, {
               "InterlockedExchange(&g_nativeMenuKeepAlivePublished, 0) == 0",
               "return",
               "kNativeMenuRequestRva - 0x400000u)) &= ~kNativeMenuKeepAliveBit",
               "kNativeMenuGateRva - 0x400000u)) = 0"}),
           "clear must be a no-op unless a forced publish owns the gate: an unconditional "
           "gate=0 on vanilla pump calls forces dispatcher 0x8AAFE0 onto its init branch, "
           "whose 0x8AA520 wipe erases the reward layers every other frame (regression cb4b239)");
    Expect(presentTick.Valid() &&
               presentTick.body.find("NativeMenuForceGatePublish()") != std::string::npos &&
               presentTick.body.find("0x13407E4u - 0x400000u)) = ") == std::string::npos,
           "Present must publish force through the keep-alive helper without a direct gate write");
    Expect(nativePump.Valid() &&
               nativePump.body.find("NativeMenuForceGatePublish()") != std::string::npos &&
               nativePump.body.find("NativeMenuForceGateClear()") != std::string::npos &&
               nativePump.body.find("0x13407E4u") == std::string::npos,
           "the pump hook must re-publish while forced and clear through the shared helper");
    Expect(closeTransition.Valid() &&
               closeTransition.body.find("NativeMenuForceGateClear()") != std::string::npos &&
               closeTransition.body.find("0x13407E4u") == std::string::npos,
           "the F7 close transition must clear force through the shared helper");
    Expect(abortDrain.Valid() && abortDrain.body.find("NativeMenuForceGateClear()") != std::string::npos,
           "stop-time drain abort must clear the keep-alive state");
    Expect(returnFlags.Valid() && returnFlags.body.find("NativeMenuForceGateClear()") != std::string::npos,
           "F8 flags hand-back must clear the keep-alive state");
    Expect(ultraClose.Valid() && ultraClose.body.find("NativeMenuForceGateClear()") != std::string::npos,
           "the Arena+ ultra close-after-launch path must clear the keep-alive state");
}

void TestNativeMenuBoundaryTraceContract() {
    std::string dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "dllmain source must be readable for the boundary-trace contract");

    const SourceBlock enabled = SourceFunctionBody(
        dllmain, "static bool NativeMenuBoundaryTraceEnabled()");
    const SourceBlock tick = SourceFunctionBody(
        dllmain, "static void NativeMenuBoundaryTraceTick()");
    const SourceBlock presentTick = SourceFunctionBody(
        dllmain, "static void NativeMenu_PresentTick()");

    /* The tracer exists to locate the post-battle reward black-screen flag.
     * It must stay OFF by default: the SettingInt fallback is 0 and the armed
     * state latches only on the positive edge so a flag dropped after boot is
     * still picked up. */
    Expect(enabled.Valid() &&
               enabled.body.find("\"native_menu.boundary_trace\", 0") != std::string::npos &&
               enabled.body.find("native_menu_trace.flag") != std::string::npos &&
               enabled.body.find("g_nativeMenuTraceArmed = 1") != std::string::npos,
           "the boundary tracer must default OFF and latch only the armed edge");
    /* The sampled tuple must cover the whole vanilla bootstrap boundary:
     * dispatcher gate/transition, request path, mode machine, kill switches,
     * and the UI manager inner state that gates the reward spawn. */
    Expect(dllmain.find("{0x13407E4u") != std::string::npos &&
               dllmain.find("{0x13407E8u") != std::string::npos &&
               dllmain.find("{0x1340804u") != std::string::npos &&
               dllmain.find("{0x1340810u") != std::string::npos &&
               dllmain.find("{0x18408ACu") != std::string::npos &&
               dllmain.find("{0x12FBBF0u") != std::string::npos &&
               dllmain.find("{0x1840834u") != std::string::npos &&
               dllmain.find("{0x12FB790u") != std::string::npos &&
               dllmain.find("{0x12FB798u") != std::string::npos &&
               dllmain.find("{0xCCB994u") != std::string::npos &&
               dllmain.find("{0xCCB998u") != std::string::npos &&
               dllmain.find("{0x12FB878u") != std::string::npos &&
               dllmain.find("{0x1597F34u") != std::string::npos &&
               dllmain.find("{0x1841C28u") != std::string::npos &&
               dllmain.find("{0xCE81E4u") != std::string::npos,
           "the trace field table must cover gate, transition, request, mode, kill, and uiMgr fields");
    /* Sampling is read-only on the Present thread: SEH-wrapped, reentrancy
     * guarded, and it logs only on change plus a one-time baseline. */
    Expect(tick.Valid() && SourceTokensInOrder(tick.body, {
               "NativeMenuBoundaryTraceEnabled()",
               "InterlockedCompareExchange(&g_nativeMenuTraceBusy, 1, 0)",
               "__try",
               "g_nativeMenuTracePrimed = true",
               "__except (EXCEPTION_EXECUTE_HANDLER)",
               "InterlockedExchange(&g_nativeMenuTraceBusy, 0)"}) &&
               tick.body.find("->") != std::string::npos,
           "the sampler must be SEH-wrapped, reentrancy-guarded, and on-change only");
    /* The Present tick is the per-frame producer; the sampler must run before
     * the producerReady early-out so post-battle black frames still trace. */
    Expect(presentTick.Valid() && SourceTokensInOrder(presentTick.body, {
               "if (!g_base) return;",
               "NativeMenuBoundaryTraceTick();",
               "g_nativeMenuProducerReady"}),
           "Present must run the boundary sampler before the producer early-out");
}

void TestMaechenPumpDrawContract() {
    std::string maechen, dllmain;
    Expect(ReadWholeFile(RuntimeSourcePath("hooks\\MaechenHook.cpp"), maechen),
           "MaechenHook source must be readable for the pump-draw contract");
    Expect(ReadWholeFile(RuntimeSourcePath("dllmain.cpp"), dllmain),
           "dllmain source must be readable for the pump-draw contract");

    /* RT2 (2026-09-16): F9 engaged force/gate/pump and owned the modal (F7/F8
     * correctly blocked), yet the screen stayed black — field 3D suppressed by
     * the forced gate while Maechen's window emitted native 2D from Present,
     * outside the pump's batch enqueue/flush phase. The durable fix mirrors
     * F7's OurDraw: a real O_DRAW callback invoked inside the pump. */
    Expect(maechen.find("NativeMenu::O_DRAW, nullptr") == std::string::npos &&
               maechen.find("O_DRAW") != std::string::npos &&
               maechen.find("MaechenPumpDraw") != std::string::npos,
           "the Maechen object must install a real O_DRAW callback instead of nullptr");
    const SourceBlock draw = SourceFunctionBody(
        maechen, "int __cdecl MaechenPumpDraw(int obj)");
    Expect(draw.Valid() &&
               draw.body.find("CopyStableRenderSnapshot") != std::string::npos &&
               draw.body.find("NativeMenu::DrawStringSub") != std::string::npos,
           "the pump draw callback must emit the window and snapshot text inside the pump");
    /* UI refresh (2026-09-16): the bare DrawWindow rectangle looked unfinished,
     * then the user asked for translucency so the field stays visible. Maechen
     * composes the shell vocabulary — worldmap scrim + glass panels + animated
     * accent — with a glass container instead of the opaque vanilla window. */
    Expect(draw.Valid() &&
               draw.body.find("NativeMenu::DrawMenuBackdrop") == std::string::npos &&
               draw.body.find("NativeMenu::DrawWindow") == std::string::npos &&
               draw.body.find("NativeMenu::DrawMenuGlassPanel") != std::string::npos &&
               draw.body.find("NativeMenu::DrawTexByAtlasId") != std::string::npos &&
               draw.body.find("NativeMenu::DrawPlasma") != std::string::npos,
           "the Maechen window must stay translucent: light scrim + worldmap + glass, no opaque vanilla window");
    const SourceBlock presentTick = SourceFunctionBody(
        dllmain, "static void NativeMenu_PresentTick()");
    Expect(presentTick.Valid() &&
               presentTick.body.find("Maechen_PresentDraw") == std::string::npos,
           "Present must never emit Maechen native 2D (post-pump batches corrupt the next menu)");
}

} // namespace

int main(int argc, char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--arena-music-io")==0) {
        TestArenaMusicMarkerIo();
        std::printf("Arena Music file IO: %d checks, %d failures\n",g_checks,g_failures);
        return g_failures?1:0;
    }
    TestResolverOwnerStartupPolicyAndCompositionContracts();
    TestF7UnsafePrototypePolicyAndSourceContainment();
    TestMaechenStateMachineAndGeneration();
    TestMaechenForegroundInputGate();
    TestMaechenStoppingIsAbsorbing();
    TestMaechenSerializationContract();
    TestMaechenResponseValidationContract();
    TestMaechenReleasedProtocolRemoteLimits();
    TestMaechenPaginationContract();
    TestMaechenLocalDefensivePageCapacity();
    TestMaechenOfficialHostNoFallbackAndIdentityHeaders();
    TestSourceContractReaderTreatsLfAndCrlfAlike();
    TestMaechenDefaultOffAndLocaleContracts();
    TestMaechenDeferredInstallRequiresPhysicalPresentReady();
    TestMaechenPlainF9ArbitrationContracts();
    TestMaechenPresentPumpOwnershipContracts();
    TestDashboardFocusedHotkeyAdmission();
    TestNativeMenuFunctionKeySafetyContracts();
    TestNativeMenuKeepAliveContract();
    TestNativeMenuBoundaryTraceContract();
    TestMaechenPumpDrawContract();
    TestMaechenBoundedUiAndRawIsolationContracts();
    TestMaechenBuildWiringParityContracts();
    TestMaechenHttpsAdapterContract();
    TestRuntimeCorePeProfileAndRanges();
    TestRewardMultiplierEvidenceAndEncoding();
    TestRewardHookInstallAndRemovalTransaction();
    TestRewardHookRetainedStubCleanupAndBattleResample();
    TestRewardHookFailClosedTransactions();
    TestRewardHookSiteFaultRecoveryAndSharedPageIsolation();
    TestRewardHookCacheProvenanceRecovery();
    TestRewardScalarBeforeGateTransaction();
    TestRewardActiveGateFaultsDisarmFailClosed();
    TestRewardUnownedGateRaceIsConflict();
    TestRuntimeCoreOwnedByteFaultMatrix();
    TestRuntimeCoreOwnedByteChangedDesiredAndStickyConflict();
    TestSeymourAddressLedgerAndRoundTrip();
    TestSeymourPreflightAndInvalidInputs();
    TestSeymourProductionInertSourceContracts();
    TestSeymourApplyFaultMatrix();
    TestSeymourRollbackFaultMatrix();
    TestSeymourOwnershipAndConflict();
    TestSeymourExactDerivedLayout();
    TestSeymourConditionalWriteRaces();
    TestSeymourActiveMaskAndRestoreAba();
    TestSeymourRestoreProvenance();
    TestSeymourRestorePhaseIsObligation();
    TestSeymourPerResourceConflictWitness();
    TestSeymourProtectionSnapshotValidation();
    TestSeymourPendingPrecedenceAndApplyCleanup();
    TestRuntimeCoreCadenceAndLifecycle();
    TestRuntimeCorePresentHookArbiter();
    TestRuntimeCoreApTransform();
    TestTask6AddressLedger();
    TestTask6BoosterSourceContracts();
    TestRewardProductionAdapterSourceContracts();
    TestTask6DllmainIntegrationContracts();
    TestAuroraDeveloperHotkeyContracts();
    TestTask6F7ArbitrationContracts();
    TestTask6IdleApGateRemainsEditable();
    TestTask6UnsupportedAdapterStatusRemainsSticky();
    TestTask6SourceValidatorMutationPressure();
    TestF8FlagsCloseLatchAndGeometry();
    TestF8FinalPolishPortableContracts();
    TestF8WakePendingRejectsAndRollsBackDirectOpen();
    TestF8RejectedOpenAtomicPublicationAcrossThreads();
    TestF8FlagsCloseAndModalSourceContracts();
    TestF8FlagsReadableLayoutSourceContracts();
    TestMultiplierEditorAndUiContracts();
    TestF7DifficultyTruthfulUiContracts();
    TestInputHintGlyphContracts();
    TestBackspacePromptSemantics();
    TestF8BulkUiSourceContracts();
    TestF8CatalogHelpIsPlayerFacing();
    TestActiveF8ConsumersUseTheCatalog();
    TestDirectF8FlagsLifecycleSourceContracts();
    TestF8FlagsCursorAndBoundsSourceContracts();
    TestSourceCheckerMutationPressure();
    TestExactLookupHasNoAliasesOrSuffixes();
    TestResolutionPrecedence();
    TestArenaMusicMarkerIo();
    TestArenaMusicExplicitOnRetiresOnlyOwnOff();
    TestCompatibilityNegativeNames();
    TestEveryLegacyFlagLocationIsObservable();
    TestUnmarkedCanonicalAndInvalidInputs();
    TestSourceNamesAreStable();
    TestSpeedHackControl();
    TestSpeedHackStickyForegroundLoss();
    TestSpeedHackNativeArbitrationAndTelemetry();
    TestSpeedHackBackendRouting();
    TestSpeedHackPackedRouteAndNestedTransitionSafety();
    TestSpeedHackIndicatorContract();
    TestSpeedHackRelocatedTargetValidation();
    TestSpeedHackGlobalTargetValidation();
    TestDialogSkipPackedPublication();
    TestDialogSkipCorrectedTargetAndOwnership();
    TestCatalogMetadataAndInvariants();
    TestFastloadDevelopmentGate();
    TestLabCatalogAndRestartControls();
    TestEditableCatalogKeysExistInBothDefaults();
    TestCatalogEditGuardsAndReadback();
    TestF8BulkTabTransactions();
    TestF8BulkExternalOverrideTruth();
    TestF8FocusLossDrainContract();
    TestUnavailableLiveRowsAllowFailSafeDisarm();
    TestSameRowFailurePublicationLinearizesAfterConfigPolledEdit();
    TestSameRowReadbackLinearizesAfterRuntimeAcknowledgedEdit();
    TestUnavailablePublicationBeforeEditPreventsPersistence();
    TestRuntimeStatusIsRowSpecificAndMetadataIsImmutable();
    TestCatalogNamesAreStable();
    TestStrictIntegerConfiguration();
    TestArenaMixProgressionFlag();
    TestMultiplierCatalogAndTransactions();
    TestFailedPersistenceNeverPublishes();
    TestPublicationOccursAfterPersistence();
    TestAuthoritativeWriteIsOneCompleteGeneration();
    TestMissingIniStartsFromSafeDefaults();
    TestRealFileDefaultsOnlyForConfirmedAbsence();
    TestConcurrentReadersSeeWholeValues();
    TestConcurrentWritersCannotLoseUpdates();
    ResetForTests();

    if (g_failures != 0) {
        fprintf(stderr, "F8RuntimeRt0: FAIL (%d/%d checks failed)\n", g_failures, g_checks);
        return 1;
    }
    printf("F8RuntimeRt0: PASS (%d checks)\n", g_checks);
    return 0;
}
