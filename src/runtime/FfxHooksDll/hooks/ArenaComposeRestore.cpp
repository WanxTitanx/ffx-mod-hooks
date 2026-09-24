#define WIN32_LEAN_AND_MEAN
#include "ArenaComposeRestore.h"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace FfxHooks::ArenaComposeRestore {
namespace {

constexpr size_t kMaximumBattleIdLength = 31;
constexpr DWORD kMaximumManifestBytes = 64u * 1024u;
constexpr DWORD kMaximumMarkerBytes = 4096u;
constexpr char kMarkerFormatV1[] = "ffx-hooks-arena-compose-restore/v1";
constexpr char kMarkerFormatV2[] = "ffx-hooks-arena-compose-restore/v2";
constexpr char kReadyFormatV2[] = "ffx-hooks-arena-compose-ready/v2";
constexpr char kFinalFormatV2[] = "ffx-hooks-arena-compose-final/v2";
constexpr wchar_t kStagingDirectoryName[] = L"arena_plus_compose_staging";
// WHY: explicit-path v2 stays testable, but production disk mutation remains quarantined until
// FileId+security-descriptor identity and child Job containment have dedicated adversarial gates.
constexpr bool kProductionDiskTransactionsQuarantined = true;

SRWLOCK g_transactionLock = SRWLOCK_INIT;

struct MarkerRecord {
    unsigned version = 0;
    std::string battleId;
    std::string attemptId;
    std::string deployRelative;
    std::string backupRelative;
    std::string stageRelative;
    std::string sourceSha256;
    std::string deployedSha256;
    std::string destinationBeforeSha256;
    std::string manifestBeforeSha256;
    bool destinationExisted = false;
    bool manifestExisted = false;
};

struct ReadyRecord {
    std::string attemptId;
    std::string deployedSha256;
};

struct DerivedPaths {
    std::wstring lexicalRoot;
    std::wstring finalRoot;
    std::wstring battleDirectory;
    std::wstring deploy;
    std::wstring backup;
    std::string deployRelative;
    std::string backupRelative;
};

bool IsRegularFileAttributes(DWORD attributes) {
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool PathExists(const wchar_t* path) {
    return path && path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

class ScopedTransactionLock {
public:
    explicit ScopedTransactionLock(const wchar_t* markerPath) {
        if (!markerPath || !markerPath[0]) return;
        path_ = markerPath;
        path_ += L".lock";
        file_ = CreateFileW(
            path_.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return;
        const DWORD attributes = GetFileAttributesW(path_.c_str());
        if (!IsRegularFileAttributes(attributes) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            return;
        }
        OVERLAPPED overlap = {};
        if (!LockFileEx(
                file_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap)) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            return;
        }
        locked_ = true;
    }

    ~ScopedTransactionLock() {
        if (file_ == INVALID_HANDLE_VALUE) return;
        if (locked_) {
            OVERLAPPED overlap = {};
            UnlockFileEx(file_, 0, 1, 0, &overlap);
        }
        CloseHandle(file_);
    }

    bool locked() const { return locked_; }

private:
    std::wstring path_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool locked_ = false;
};

class ScopedSrwExclusive {
public:
    ScopedSrwExclusive() { AcquireSRWLockExclusive(&g_transactionLock); }
    ~ScopedSrwExclusive() { ReleaseSRWLockExclusive(&g_transactionLock); }
};

bool ReadBoundedFile(const wchar_t* path, DWORD maximumBytes, std::string* out) {
    if (!path || !path[0] || !out) return false;
    out->clear();

    const DWORD attributes = GetFileAttributesW(path);
    if (!IsRegularFileAttributes(attributes) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return false;

    HANDLE file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    const bool sizeOk = GetFileSizeEx(file, &size) != FALSE &&
        size.QuadPart >= 0 && size.QuadPart <= maximumBytes;
    if (!sizeOk) {
        CloseHandle(file);
        return false;
    }

    out->resize(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < out->size()) {
        DWORD read = 0;
        const DWORD remaining = static_cast<DWORD>(out->size() - total);
        if (!ReadFile(file, out->data() + total, remaining, &read, nullptr) || read == 0) {
            CloseHandle(file);
            out->clear();
            return false;
        }
        total += read;
    }
    CloseHandle(file);
    return true;
}

bool WriteAll(HANDLE file, const char* bytes, size_t length) {
    size_t total = 0;
    while (total < length) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(length - total, static_cast<size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(file, bytes + total, chunk, &written, nullptr) || written == 0)
            return false;
        total += written;
    }
    return true;
}

bool FullPath(const wchar_t* input, std::wstring* out) {
    if (!input || !input[0] || !out) return false;
    const DWORD needed = GetFullPathNameW(input, 0, nullptr, nullptr);
    if (needed == 0) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1u, L'\0');
    const DWORD length = GetFullPathNameW(input, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return false;
    out->assign(buffer.data(), length);
    while (out->size() > 3 && (out->back() == L'\\' || out->back() == L'/')) out->pop_back();
    return true;
}

bool FinalPathFromHandle(HANDLE handle, std::wstring* out) {
    if (handle == INVALID_HANDLE_VALUE || !out) return false;
    const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD needed = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (needed == 0) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1u, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (length == 0 || length >= buffer.size()) return false;
    out->assign(buffer.data(), length);
    while (out->size() > 7 && (out->back() == L'\\' || out->back() == L'/')) out->pop_back();
    return true;
}

bool IsContainedFinalPath(const std::wstring& root, const std::wstring& child) {
    if (root.empty() || child.size() <= root.size()) return false;
    if (_wcsnicmp(root.c_str(), child.c_str(), root.size()) != 0) return false;
    return child[root.size()] == L'\\' || child[root.size()] == L'/';
}

bool OpenRootFinalPath(const std::wstring& root, std::wstring* finalRoot) {
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    HANDLE directory = CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return false;
    const bool ok = FinalPathFromHandle(directory, finalRoot);
    CloseHandle(directory);
    return ok;
}

bool BuildDerivedPaths(const wchar_t* modBtlRoot, const std::string& battleId, DerivedPaths* out) {
    if (!modBtlRoot || !out || !IsStrictBattleId(battleId.c_str())) return false;

    DerivedPaths result;
    if (!FullPath(modBtlRoot, &result.lexicalRoot) ||
        !OpenRootFinalPath(result.lexicalRoot, &result.finalRoot)) {
        return false;
    }

    std::wstring battleWide(battleId.begin(), battleId.end());
    result.battleDirectory = result.lexicalRoot + L"\\" + battleWide;
    const DWORD battleAttributes = GetFileAttributesW(result.battleDirectory.c_str());
    if (battleAttributes == INVALID_FILE_ATTRIBUTES ||
        (battleAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (battleAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }

    const std::wstring fileName = battleWide + L".bin";
    result.deploy = result.battleDirectory + L"\\" + fileName;
    result.backup = result.deploy + L".spiraforge.bak";
    result.deployRelative = battleId + "\\" + battleId + ".bin";
    result.backupRelative = result.deployRelative + ".spiraforge.bak";

    std::wstring deployFull;
    std::wstring backupFull;
    if (!FullPath(result.deploy.c_str(), &deployFull) ||
        !FullPath(result.backup.c_str(), &backupFull) ||
        _wcsicmp(deployFull.c_str(), result.deploy.c_str()) != 0 ||
        _wcsicmp(backupFull.c_str(), result.backup.c_str()) != 0) {
        return false;
    }

    *out = std::move(result);
    return true;
}

bool Utf8ToWide(const std::string& value, std::wstring* out) {
    if (!out || value.empty()) return false;
    const int needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return false;
    out->resize(static_cast<size_t>(needed));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        out->data(), needed) == needed;
}

bool AnsiPathToWide(const char* value, std::wstring* out) {
    if (!value || !value[0] || !out) return false;
    const int needed = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (needed <= 1) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value, -1, buffer.data(), needed) != needed)
        return false;
    out->assign(buffer.data());
    return true;
}

bool WidePathToAnsi(const wchar_t* value, char* out, size_t outCount) {
    if (!value || !value[0] || !out || outCount == 0) return false;
    const int needed = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value, -1, nullptr, 0,
                                            nullptr, nullptr);
    if (needed <= 1 || static_cast<size_t>(needed) > outCount) return false;
    BOOL usedDefault = FALSE;
    return WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value, -1, out, needed,
                               nullptr, &usedDefault) == needed && !usedDefault;
}

bool ParentDirectory(const wchar_t* path, std::wstring* out) {
    if (!path || !path[0] || !out) return false;
    std::wstring full;
    if (!FullPath(path, &full)) return false;
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < 3) return false;
    out->assign(full, 0, slash);
    return true;
}

int HexNibble(char ch);

bool IsAttemptId(const std::string& value) {
    if (value.size() != 32) return false;
    for (const char ch : value) if (HexNibble(ch) < 0) return false;
    return true;
}

bool GenerateAttemptId(std::string* out) {
    if (!out) return false;
    std::array<unsigned char, 16> random = {};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return false;
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    out->clear();
    out->reserve(32);
    for (const unsigned char byte : random) {
        out->push_back(kHex[byte >> 4]);
        out->push_back(kHex[byte & 0x0F]);
    }
    return true;
}

bool CopyWide(wchar_t* destination, size_t destinationCount, const std::wstring& source) {
    if (!destination || destinationCount == 0 || source.size() + 1 > destinationCount) return false;
    std::wmemcpy(destination, source.c_str(), source.size() + 1);
    return true;
}

bool EnsurePlainDirectory(const std::wstring& path, bool create) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && create) {
        if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        attributes = GetFileAttributesW(path.c_str());
    }
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

std::wstring ReadyPath(const wchar_t* markerPath, const std::string& attemptId) {
    return std::wstring(markerPath) + L".ready-" +
        std::wstring(attemptId.begin(), attemptId.end());
}

std::wstring FinalPath(const wchar_t* markerPath, const std::string& attemptId) {
    return std::wstring(markerPath) + L".final-" +
        std::wstring(attemptId.begin(), attemptId.end());
}

std::string StageRelativeForAttempt(const std::string& attemptId) {
    return std::string("arena_plus_compose_staging\\") + attemptId + "\\mod-btl";
}

bool ResolveStageRootLexical(
    const wchar_t* markerPath,
    const std::string& attemptId,
    const std::string& stageRelative,
    std::wstring* outStageRoot) {
    if (!markerPath || !IsAttemptId(attemptId) ||
        stageRelative != StageRelativeForAttempt(attemptId) || !outStageRoot) {
        return false;
    }
    std::wstring markerDirectory;
    if (!ParentDirectory(markerPath, &markerDirectory) || !EnsurePlainDirectory(markerDirectory, false))
        return false;
    const std::wstring expected = markerDirectory + L"\\" + kStagingDirectoryName + L"\\" +
        std::wstring(attemptId.begin(), attemptId.end()) + L"\\mod-btl";
    std::wstring full;
    if (!FullPath(expected.c_str(), &full) || _wcsicmp(full.c_str(), expected.c_str()) != 0)
        return false;
    *outStageRoot = std::move(full);
    return true;
}

bool CreateStageTree(
    const wchar_t* markerPath,
    const std::string& attemptId,
    const std::string& battleId,
    std::wstring* outStageRoot) {
    std::wstring markerDirectory;
    if (!ParentDirectory(markerPath, &markerDirectory) || !EnsurePlainDirectory(markerDirectory, false))
        return false;
    const std::wstring stageBase = markerDirectory + L"\\" + kStagingDirectoryName;
    if (!EnsurePlainDirectory(stageBase, true)) return false;

    const std::wstring attemptDirectory = stageBase + L"\\" +
        std::wstring(attemptId.begin(), attemptId.end());
    if (!CreateDirectoryW(attemptDirectory.c_str(), nullptr)) return false;
    const std::wstring stageRoot = attemptDirectory + L"\\mod-btl";
    if (!CreateDirectoryW(stageRoot.c_str(), nullptr)) return false;
    const std::wstring battleDirectory = stageRoot + L"\\" +
        std::wstring(battleId.begin(), battleId.end());
    if (!CreateDirectoryW(battleDirectory.c_str(), nullptr)) return false;

    std::wstring parentFinal;
    std::wstring stageFinal;
    if (!OpenRootFinalPath(markerDirectory, &parentFinal) ||
        !OpenRootFinalPath(stageRoot, &stageFinal) || !IsContainedFinalPath(parentFinal, stageFinal)) {
        return false;
    }
    *outStageRoot = stageRoot;
    return true;
}

bool ResolveExistingStagePaths(
    const wchar_t* markerPath,
    const MarkerRecord& marker,
    DerivedPaths* outPaths,
    std::wstring* outStageRoot) {
    std::wstring stageRoot;
    if (!ResolveStageRootLexical(
            markerPath, marker.attemptId, marker.stageRelative, &stageRoot) ||
        !BuildDerivedPaths(stageRoot.c_str(), marker.battleId, outPaths)) {
        return false;
    }
    if (outStageRoot) *outStageRoot = stageRoot;
    return true;
}

bool AppendUtf8CodePoint(unsigned codePoint, std::string* out) {
    if (!out || codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
        return false;
    if (codePoint <= 0x7Fu) {
        out->push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFu) {
        out->push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint <= 0xFFFFu) {
        out->push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out->push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
    return true;
}

int HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool DecodeJsonString(const std::string& json, size_t quote, std::string* out, size_t* endQuote) {
    if (!out || quote >= json.size() || json[quote] != '"') return false;
    out->clear();
    for (size_t i = quote + 1; i < json.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(json[i]);
        if (ch == '"') {
            if (endQuote) *endQuote = i;
            return true;
        }
        if (ch < 0x20u) return false;
        if (ch != '\\') {
            out->push_back(static_cast<char>(ch));
            continue;
        }
        if (++i >= json.size()) return false;
        switch (json[i]) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                if (i + 4 >= json.size()) return false;
                unsigned codePoint = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const int nibble = HexNibble(json[++i]);
                    if (nibble < 0) return false;
                    codePoint = (codePoint << 4) | static_cast<unsigned>(nibble);
                }
                if (!AppendUtf8CodePoint(codePoint, out)) return false;
                break;
            }
            default: return false;
        }
    }
    return false;
}

bool FindUniqueJsonValueStart(const std::string& json, const char* key, size_t* valueStart) {
    if (!key || !valueStart) return false;
    const std::string token = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(token);
    if (keyPos == std::string::npos || json.find(token, keyPos + token.size()) != std::string::npos)
        return false;
    size_t cursor = keyPos + token.size();
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size() || json[cursor++] != ':') return false;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) ++cursor;
    if (cursor >= json.size()) return false;
    *valueStart = cursor;
    return true;
}

bool ReadUniqueJsonString(const std::string& json, const char* key, std::string* out) {
    size_t valueStart = 0;
    return FindUniqueJsonValueStart(json, key, &valueStart) &&
        DecodeJsonString(json, valueStart, out, nullptr);
}

bool ReadUniqueJsonFalse(const std::string& json, const char* key) {
    size_t valueStart = 0;
    if (!FindUniqueJsonValueStart(json, key, &valueStart) ||
        json.compare(valueStart, 5, "false") != 0) {
        return false;
    }
    const size_t after = valueStart + 5;
    return after >= json.size() || json[after] == ',' || json[after] == '}' ||
        std::isspace(static_cast<unsigned char>(json[after])) != 0;
}

bool Sha256FromOpenFile(HANDLE file, std::string* outHex) {
    if (file == INVALID_HANDLE_VALUE || !outHex) return false;
    LARGE_INTEGER zero = {};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD copied = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        goto cleanup;
    if (BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength), &copied, 0) < 0 || objectLength == 0)
        goto cleanup;
    if (BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
            sizeof(hashLength), &copied, 0) < 0 || hashLength != 32)
        goto cleanup;
    object.resize(objectLength);
    digest.resize(hashLength);
    if (BCryptCreateHash(
            algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0) < 0)
        goto cleanup;

    {
        std::array<unsigned char, 64u * 1024u> buffer = {};
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
                goto cleanup;
            if (read == 0) break;
            if (BCryptHashData(hash, buffer.data(), read, 0) < 0) goto cleanup;
        }
    }
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        goto cleanup;

    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        outHex->clear();
        outHex->reserve(digest.size() * 2u);
        for (unsigned char byte : digest) {
            outHex->push_back(kHex[byte >> 4]);
            outHex->push_back(kHex[byte & 0x0Fu]);
        }
    }
    ok = true;

cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool Sha256FromBytes(const std::string& bytes, std::string* outHex) {
    if (!outHex) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD copied = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        goto cleanup;
    if (BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength), &copied, 0) < 0 || objectLength == 0)
        goto cleanup;
    if (BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
            sizeof(hashLength), &copied, 0) < 0 || hashLength != 32)
        goto cleanup;
    object.resize(objectLength);
    digest.resize(hashLength);
    if (BCryptCreateHash(
            algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0) < 0)
        goto cleanup;
    if (!bytes.empty() && BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
            static_cast<ULONG>(bytes.size()),
            0) < 0)
        goto cleanup;
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        goto cleanup;

    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        outHex->clear();
        outHex->reserve(digest.size() * 2u);
        for (unsigned char byte : digest) {
            outHex->push_back(kHex[byte >> 4]);
            outHex->push_back(kHex[byte & 0x0Fu]);
        }
    }
    ok = true;

cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

Result HashContainedFile(
    const std::wstring& path,
    const std::wstring& finalRoot,
    Result missingResult,
    std::string* outHash) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return missingResult;
    if (!IsRegularFileAttributes(attributes) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return Result::PathRejected;

    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Result::IoFailure;

    BY_HANDLE_FILE_INFORMATION information = {};
    std::wstring finalPath;
    const bool safe = GetFileInformationByHandle(file, &information) != FALSE &&
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
        FinalPathFromHandle(file, &finalPath) && IsContainedFinalPath(finalRoot, finalPath);
    if (!safe) {
        CloseHandle(file);
        return Result::PathRejected;
    }

    const bool hashed = Sha256FromOpenFile(file, outHash);
    CloseHandle(file);
    return hashed ? Result::Created : Result::IoFailure;
}

bool IsSha256(const std::string& value) {
    if (value.size() != 64) return false;
    for (char ch : value) if (HexNibble(ch) < 0) return false;
    return true;
}

bool CopyAscii(char* destination, size_t destinationSize, const std::string& source) {
    if (!destination || destinationSize == 0 || source.size() + 1 > destinationSize) return false;
    std::memcpy(destination, source.c_str(), source.size() + 1);
    return true;
}

bool CaptureSmallFileIdentity(
    const wchar_t* path,
    DWORD maximumBytes,
    bool* existed,
    std::string* sha256) {
    if (!path || !path[0] || !existed || !sha256) return false;
    *existed = PathExists(path);
    sha256->clear();
    if (!*existed) return true;
    std::string bytes;
    return ReadBoundedFile(path, maximumBytes, &bytes) && Sha256FromBytes(bytes, sha256);
}

bool ValidateAttemptShape(const ComposeAttempt& attempt) {
    if (!IsStrictBattleId(attempt.battleId) || !IsAttemptId(attempt.attemptId) ||
        !IsSha256(attempt.expectedSourceSha256) ||
        wcsnlen_s(attempt.stagingModRoot, std::size(attempt.stagingModRoot)) == 0 ||
        wcsnlen_s(attempt.stagingModRoot, std::size(attempt.stagingModRoot)) >=
            std::size(attempt.stagingModRoot)) {
        return false;
    }
    if (attempt.destinationExisted && !IsSha256(attempt.destinationBeforeSha256)) return false;
    if (!attempt.destinationExisted && attempt.destinationBeforeSha256[0] != '\0') return false;
    if (attempt.manifestExisted && !IsSha256(attempt.manifestBeforeSha256)) return false;
    if (!attempt.manifestExisted && attempt.manifestBeforeSha256[0] != '\0') return false;
    return true;
}

Result ReadAndValidateComposeManifest(
    const wchar_t* composeManifestPath,
    const DerivedPaths& paths,
    const char* expectedBattleId,
    std::string* outManifestBytes) {
    if (!composeManifestPath || !expectedBattleId || !outManifestBytes ||
        !ReadBoundedFile(composeManifestPath, kMaximumManifestBytes, outManifestBytes)) {
        return Result::InvalidManifest;
    }
    std::string schema;
    std::string battleId;
    std::string deployUtf8;
    if (!ReadUniqueJsonString(*outManifestBytes, "schema", &schema) ||
        schema != "arena-compose-v1" ||
        !ReadUniqueJsonString(*outManifestBytes, "battle_id", &battleId) ||
        !IsStrictBattleId(battleId.c_str()) || _stricmp(battleId.c_str(), expectedBattleId) != 0 ||
        !ReadUniqueJsonString(*outManifestBytes, "deploy_path", &deployUtf8) || deployUtf8.empty() ||
        !ReadUniqueJsonFalse(*outManifestBytes, "dry_run")) {
        return Result::InvalidManifest;
    }

    std::wstring declaredDeploy;
    std::wstring declaredFull;
    std::wstring expectedFull;
    if (!Utf8ToWide(deployUtf8, &declaredDeploy) || !FullPath(declaredDeploy.c_str(), &declaredFull) ||
        !FullPath(paths.deploy.c_str(), &expectedFull) ||
        _wcsicmp(declaredFull.c_str(), expectedFull.c_str()) != 0) {
        return Result::ManifestMismatch;
    }
    return Result::Created;
}

std::string SerializeMarker(const MarkerRecord& marker) {
    return std::string("format=") + kMarkerFormatV2 + "\n" +
        "state=PREPARED\n" +
        "battle_id=" + marker.battleId + "\n" +
        "attempt_id=" + marker.attemptId + "\n" +
        "deploy_relative=" + marker.deployRelative + "\n" +
        "backup_relative=" + marker.backupRelative + "\n" +
        "stage_relative=" + marker.stageRelative + "\n" +
        "source_sha256=" + marker.sourceSha256 + "\n" +
        "destination_before_exists=" + (marker.destinationExisted ? "1\n" : "0\n") +
        "destination_before_sha256=" +
            (marker.destinationExisted ? marker.destinationBeforeSha256 : "-") + "\n" +
        "manifest_before_exists=" + (marker.manifestExisted ? "1\n" : "0\n") +
        "manifest_before_sha256=" +
            (marker.manifestExisted ? marker.manifestBeforeSha256 : "-") + "\n";
}

bool ParseMarker(const std::string& bytes, MarkerRecord* out) {
    if (!out || bytes.empty()) return false;
    MarkerRecord marker;
    bool haveFormat = false;
    bool haveState = false;
    bool haveBattle = false;
    bool haveAttempt = false;
    bool haveDeploy = false;
    bool haveBackup = false;
    bool haveStage = false;
    bool haveSource = false;
    bool haveDeployed = false;
    bool haveDestinationExists = false;
    bool haveDestinationBefore = false;
    bool haveManifestExists = false;
    bool haveManifestBefore = false;
    size_t cursor = 0;
    while (cursor < bytes.size()) {
        const size_t newline = bytes.find('\n', cursor);
        const size_t end = newline == std::string::npos ? bytes.size() : newline;
        std::string line = bytes.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        cursor = newline == std::string::npos ? bytes.size() : newline + 1;
        if (line.empty()) continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) return false;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "format" && !haveFormat) {
            if (value == kMarkerFormatV1) marker.version = 1;
            else if (value == kMarkerFormatV2) marker.version = 2;
            else return false;
            haveFormat = true;
        } else if (key == "state" && !haveState) {
            if (value != "PREPARED") return false;
            haveState = true;
        } else if (key == "battle_id" && !haveBattle) {
            marker.battleId = value;
            haveBattle = true;
        } else if (key == "attempt_id" && !haveAttempt) {
            marker.attemptId = value;
            haveAttempt = true;
        } else if (key == "deploy_relative" && !haveDeploy) {
            marker.deployRelative = value;
            haveDeploy = true;
        } else if (key == "backup_relative" && !haveBackup) {
            marker.backupRelative = value;
            haveBackup = true;
        } else if (key == "stage_relative" && !haveStage) {
            marker.stageRelative = value;
            haveStage = true;
        } else if (key == "source_sha256" && !haveSource) {
            marker.sourceSha256 = value;
            haveSource = true;
        } else if (key == "deployed_sha256" && !haveDeployed) {
            marker.deployedSha256 = value;
            haveDeployed = true;
        } else if (key == "destination_before_exists" && !haveDestinationExists) {
            if (value != "0" && value != "1") return false;
            marker.destinationExisted = value == "1";
            haveDestinationExists = true;
        } else if (key == "destination_before_sha256" && !haveDestinationBefore) {
            marker.destinationBeforeSha256 = value;
            haveDestinationBefore = true;
        } else if (key == "manifest_before_exists" && !haveManifestExists) {
            if (value != "0" && value != "1") return false;
            marker.manifestExisted = value == "1";
            haveManifestExists = true;
        } else if (key == "manifest_before_sha256" && !haveManifestBefore) {
            marker.manifestBeforeSha256 = value;
            haveManifestBefore = true;
        } else {
            return false;
        }
    }
    if (!haveFormat || !haveBattle || !haveDeploy || !haveBackup || !haveSource ||
        !IsStrictBattleId(marker.battleId.c_str()) || !IsSha256(marker.sourceSha256)) {
        return false;
    }
    const std::string expectedDeploy = marker.battleId + "\\" + marker.battleId + ".bin";
    if (marker.deployRelative != expectedDeploy ||
        marker.backupRelative != expectedDeploy + ".spiraforge.bak") {
        return false;
    }
    if (marker.version == 1) {
        if (haveState || haveAttempt || haveStage || haveDestinationExists ||
            haveDestinationBefore || haveManifestExists || haveManifestBefore || !haveDeployed ||
            !IsSha256(marker.deployedSha256) ||
            _stricmp(marker.sourceSha256.c_str(), marker.deployedSha256.c_str()) == 0) {
            return false;
        }
    } else {
        if (!haveState || !haveAttempt || !haveStage || !haveDestinationExists ||
            !haveDestinationBefore || !haveManifestExists || !haveManifestBefore || haveDeployed ||
            !IsAttemptId(marker.attemptId) ||
            marker.stageRelative != StageRelativeForAttempt(marker.attemptId) ||
            (marker.destinationExisted
                ? !IsSha256(marker.destinationBeforeSha256)
                : marker.destinationBeforeSha256 != "-") ||
            (marker.manifestExisted
                ? !IsSha256(marker.manifestBeforeSha256)
                : marker.manifestBeforeSha256 != "-")) {
            return false;
        }
        if (!marker.destinationExisted) marker.destinationBeforeSha256.clear();
        if (!marker.manifestExisted) marker.manifestBeforeSha256.clear();
    }
    *out = std::move(marker);
    return true;
}

std::string SerializeReady(const ReadyRecord& ready) {
    return std::string("format=") + kReadyFormatV2 + "\n" +
        "attempt_id=" + ready.attemptId + "\n" +
        "deployed_sha256=" + ready.deployedSha256 + "\n";
}

bool ParseReady(const std::string& bytes, ReadyRecord* out) {
    if (!out) return false;
    ReadyRecord ready;
    bool haveFormat = false;
    bool haveAttempt = false;
    bool haveDeployed = false;
    size_t cursor = 0;
    while (cursor < bytes.size()) {
        const size_t newline = bytes.find('\n', cursor);
        const size_t end = newline == std::string::npos ? bytes.size() : newline;
        std::string line = bytes.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        cursor = newline == std::string::npos ? bytes.size() : newline + 1;
        if (line.empty()) continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) return false;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "format" && !haveFormat) {
            if (value != kReadyFormatV2) return false;
            haveFormat = true;
        } else if (key == "attempt_id" && !haveAttempt) {
            ready.attemptId = value;
            haveAttempt = true;
        } else if (key == "deployed_sha256" && !haveDeployed) {
            ready.deployedSha256 = value;
            haveDeployed = true;
        } else {
            return false;
        }
    }
    if (!haveFormat || !haveAttempt || !haveDeployed || !IsAttemptId(ready.attemptId) ||
        !IsSha256(ready.deployedSha256)) {
        return false;
    }
    *out = std::move(ready);
    return true;
}

std::string SerializeFinal(const ReadyRecord& ready) {
    return std::string("format=") + kFinalFormatV2 + "\n" +
        "attempt_id=" + ready.attemptId + "\n" +
        "deployed_sha256=" + ready.deployedSha256 + "\n";
}

bool WriteMarkerAtomically(
    const wchar_t* markerPath,
    const std::string& bytes,
    const std::string* expectedExistingBytes) {
    if (!markerPath || !markerPath[0]) return false;
    if (!expectedExistingBytes && PathExists(markerPath)) return false;
    if (expectedExistingBytes) {
        std::string current;
        if (!ReadBoundedFile(markerPath, kMaximumMarkerBytes, &current) ||
            current != *expectedExistingBytes) {
            return false;
        }
    }
    std::wstring tempPath;
    HANDLE temp = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 8 && temp == INVALID_HANDLE_VALUE; ++attempt) {
        tempPath = markerPath;
        tempPath += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        temp = CreateFileW(
            tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY, nullptr);
    }
    if (temp == INVALID_HANDLE_VALUE) return false;

    const bool written = WriteAll(temp, bytes.data(), bytes.size()) && FlushFileBuffers(temp) != FALSE;
    CloseHandle(temp);
    if (!written) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    if (expectedExistingBytes) {
        std::string current;
        if (!ReadBoundedFile(markerPath, kMaximumMarkerBytes, &current) ||
            current != *expectedExistingBytes) {
            DeleteFileW(tempPath.c_str());
            return false;
        }
    }
    const DWORD moveFlags = MOVEFILE_WRITE_THROUGH |
        (expectedExistingBytes ? MOVEFILE_REPLACE_EXISTING : 0u);
    if (!MoveFileExW(tempPath.c_str(), markerPath, moveFlags)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    std::string readback;
    return ReadBoundedFile(markerPath, kMaximumMarkerBytes, &readback) && readback == bytes;
}

bool WriteNewAuthorityFile(const wchar_t* path, const std::string& bytes) {
    return WriteMarkerAtomically(path, bytes, nullptr);
}

bool ReadBoundedOpenFile(HANDLE file, DWORD maximumBytes, std::string* out) {
    if (file == INVALID_HANDLE_VALUE || !out) return false;
    LARGE_INTEGER size = {};
    LARGE_INTEGER zero = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > maximumBytes ||
        !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) {
        return false;
    }
    out->assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD total = 0;
    while (total < out->size()) {
        DWORD read = 0;
        if (!ReadFile(file, out->data() + total,
                      static_cast<DWORD>(out->size() - total), &read, nullptr) || read == 0) {
            out->clear();
            return false;
        }
        total += read;
    }
    return true;
}

std::wstring MakeRestoreTempPath(const std::wstring& deploy, unsigned attempt) {
    return deploy + L".ffxhooks-restore-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetCurrentThreadId()) + L"-" + std::to_wstring(GetTickCount64()) +
        L"-" + std::to_wstring(attempt) + L".tmp";
}

Result CreateVerifiedBackupFromSource(
    const std::wstring& source,
    const std::wstring& sourceFinalRoot,
    const DerivedPaths& targetPaths,
    std::string* outSourceHash) {
    Result result = HashContainedFile(
        source, sourceFinalRoot, Result::SourceMissing, outSourceHash);
    if (result != Result::Created) return result;

    // WHY: the external process must never be the only owner of the recovery baseline. Publish an
    // exact same-directory backup before launch, without replacing any backup another owner created.
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const std::wstring temp = MakeRestoreTempPath(targetPaths.backup, attempt);
        if (!CopyFileW(source.c_str(), temp.c_str(), TRUE)) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            return Result::IoFailure;
        }

        std::string tempHash;
        result = HashContainedFile(
            temp, targetPaths.finalRoot, Result::SourceMissing, &tempHash);
        if (result != Result::Created || _stricmp(tempHash.c_str(), outSourceHash->c_str()) != 0) {
            DeleteFileW(temp.c_str());
            return result == Result::Created ? Result::SourceHashMismatch : result;
        }

        std::string sourceRecheck;
        result = HashContainedFile(
            source, sourceFinalRoot, Result::SourceMissing, &sourceRecheck);
        if (result != Result::Created ||
            _stricmp(sourceRecheck.c_str(), outSourceHash->c_str()) != 0) {
            DeleteFileW(temp.c_str());
            return result == Result::Created ? Result::SourceHashMismatch : result;
        }

        if (!MoveFileExW(temp.c_str(), targetPaths.backup.c_str(), MOVEFILE_WRITE_THROUGH)) {
            const DWORD error = GetLastError();
            DeleteFileW(temp.c_str());
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                return Result::IoFailure;

            // Another actor won the no-replace race. Accept only byte-identical canonical data.
            std::string racedBackupHash;
            result = HashContainedFile(
                targetPaths.backup,
                targetPaths.finalRoot,
                Result::SourceMissing,
                &racedBackupHash);
            if (result != Result::Created) return result;
            return _stricmp(racedBackupHash.c_str(), outSourceHash->c_str()) == 0
                ? Result::Created
                : Result::SourceHashMismatch;
        }

        std::string backupHash;
        result = HashContainedFile(
            targetPaths.backup, targetPaths.finalRoot, Result::SourceMissing, &backupHash);
        if (result != Result::Created) return result;
        return _stricmp(backupHash.c_str(), outSourceHash->c_str()) == 0
            ? Result::Created
            : Result::SourceHashMismatch;
    }
    return Result::IoFailure;
}

bool FlushPath(const std::wstring& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return flushed;
}

bool AttemptMatchesMarker(const ComposeAttempt& attempt, const MarkerRecord& marker) {
    return marker.version == 2 && ValidateAttemptShape(attempt) &&
        _stricmp(attempt.battleId, marker.battleId.c_str()) == 0 &&
        _stricmp(attempt.attemptId, marker.attemptId.c_str()) == 0 &&
        _stricmp(attempt.expectedSourceSha256, marker.sourceSha256.c_str()) == 0 &&
        attempt.destinationExisted == marker.destinationExisted &&
        (!attempt.destinationExisted ||
            _stricmp(attempt.destinationBeforeSha256, marker.destinationBeforeSha256.c_str()) == 0) &&
        attempt.manifestExisted == marker.manifestExisted &&
        (!attempt.manifestExisted ||
            _stricmp(attempt.manifestBeforeSha256, marker.manifestBeforeSha256.c_str()) == 0);
}

Result ReadPreparedAuthority(
    const wchar_t* markerPath,
    std::string* outBytes,
    MarkerRecord* outMarker) {
    if (!ReadBoundedFile(markerPath, kMaximumMarkerBytes, outBytes) ||
        !ParseMarker(*outBytes, outMarker)) {
        return Result::InvalidMarker;
    }
    return Result::Created;
}

Result ReadReadyAuthority(
    const wchar_t* markerPath,
    const MarkerRecord& marker,
    std::string* outBytes,
    ReadyRecord* outReady) {
    const std::wstring readyPath = ReadyPath(markerPath, marker.attemptId);
    if (!PathExists(readyPath.c_str())) return Result::NoMarker;
    if (!ReadBoundedFile(readyPath.c_str(), kMaximumMarkerBytes, outBytes) ||
        !ParseReady(*outBytes, outReady) ||
        _stricmp(outReady->attemptId.c_str(), marker.attemptId.c_str()) != 0 ||
        _stricmp(outReady->deployedSha256.c_str(), marker.sourceSha256.c_str()) == 0) {
        return Result::InvalidMarker;
    }
    return Result::StagedReady;
}

bool DestinationMatchesBefore(
    const DerivedPaths& paths,
    const MarkerRecord& marker,
    Result* outFailure) {
    if (!marker.destinationExisted) {
        const DWORD attributes = GetFileAttributesW(paths.deploy.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
            return true;
        if (outFailure) *outFailure = Result::DestinationHashMismatch;
        return false;
    }
    std::string hash;
    const Result result = HashContainedFile(
        paths.deploy, paths.finalRoot, Result::DestinationMissing, &hash);
    if (result != Result::Created) {
        if (outFailure) *outFailure = result;
        return false;
    }
    if (_stricmp(hash.c_str(), marker.destinationBeforeSha256.c_str()) != 0) {
        if (outFailure) *outFailure = Result::DestinationHashMismatch;
        return false;
    }
    return true;
}

std::wstring PublishRollbackPath(const DerivedPaths& paths, const std::string& attemptId) {
    return paths.deploy + L".ffxhooks-publish-backup-" +
        std::wstring(attemptId.begin(), attemptId.end()) + L".tmp";
}

Result CopySourceToLiveTemp(
    const std::wstring& source,
    const std::wstring& sourceFinalRoot,
    const DerivedPaths& livePaths,
    const std::string& expectedHash,
    std::wstring* outTemp) {
    std::string sourceHash;
    Result result = HashContainedFile(source, sourceFinalRoot, Result::SourceMissing, &sourceHash);
    if (result != Result::Created) return result;
    if (_stricmp(sourceHash.c_str(), expectedHash.c_str()) != 0)
        return Result::SourceHashMismatch;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const std::wstring temp = MakeRestoreTempPath(livePaths.deploy, attempt);
        if (!CopyFileW(source.c_str(), temp.c_str(), TRUE)) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            return Result::IoFailure;
        }
        std::string tempHash;
        result = HashContainedFile(temp, livePaths.finalRoot, Result::DestinationMissing, &tempHash);
        if (result != Result::Created || _stricmp(tempHash.c_str(), expectedHash.c_str()) != 0) {
            DeleteFileW(temp.c_str());
            return result == Result::Created ? Result::PostRestoreMismatch : result;
        }
        *outTemp = temp;
        return Result::Created;
    }
    return Result::IoFailure;
}

Result ReplaceLivePreservingMetadata(
    const std::wstring& source,
    const std::wstring& sourceFinalRoot,
    const DerivedPaths& livePaths,
    const MarkerRecord& marker,
    const std::string& expectedCurrentHash,
    const std::string& replacementHash) {
    std::wstring temp;
    Result result = CopySourceToLiveTemp(
        source, sourceFinalRoot, livePaths, replacementHash, &temp);
    if (result != Result::Created) return result;

    std::string tempRecheck;
    result = HashContainedFile(temp, livePaths.finalRoot, Result::DestinationMissing, &tempRecheck);
    if (result != Result::Created || _stricmp(tempRecheck.c_str(), replacementHash.c_str()) != 0) {
        DeleteFileW(temp.c_str());
        return result == Result::Created ? Result::PostRestoreMismatch : result;
    }

    HANDLE battleDirectory = CreateFileW(
        livePaths.battleDirectory.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (battleDirectory == INVALID_HANDLE_VALUE) {
        DeleteFileW(temp.c_str());
        return Result::PathRejected;
    }
    std::wstring heldFinal;
    const bool heldSafe = FinalPathFromHandle(battleDirectory, &heldFinal) &&
        IsContainedFinalPath(livePaths.finalRoot, heldFinal);
    if (!heldSafe) {
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return Result::PathRejected;
    }

    std::string currentHash;
    result = HashContainedFile(
        livePaths.deploy, livePaths.finalRoot, Result::DestinationMissing, &currentHash);
    if (result != Result::Created || _stricmp(currentHash.c_str(), expectedCurrentHash.c_str()) != 0) {
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return result == Result::Created ? Result::DestinationHashMismatch : result;
    }

    const DWORD originalAttributes = GetFileAttributesW(livePaths.deploy.c_str());
    if (originalAttributes == INVALID_FILE_ATTRIBUTES) {
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return Result::DestinationMissing;
    }
    if ((originalAttributes & FILE_ATTRIBUTE_READONLY) != 0 &&
        !SetFileAttributesW(
            livePaths.deploy.c_str(), originalAttributes & ~FILE_ATTRIBUTE_READONLY)) {
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return Result::IoFailure;
    }

    const std::wstring rollback = PublishRollbackPath(livePaths, marker.attemptId);
    if (PathExists(rollback.c_str())) {
        if ((originalAttributes & FILE_ATTRIBUTE_READONLY) != 0)
            SetFileAttributesW(livePaths.deploy.c_str(), originalAttributes);
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return Result::PendingExists;
    }

    // WHY: ReplaceFileW preserves the replaced carrier's creation time, DACL/security metadata,
    // attributes, encryption/compression and named streams. Its WRITE_THROUGH flag is unsupported,
    // so durability is established by an explicit FlushFileBuffers readback below.
    const bool replaced = ReplaceFileW(
        livePaths.deploy.c_str(), temp.c_str(), rollback.c_str(), 0, nullptr, nullptr) != FALSE;
    if (!replaced) {
        if ((originalAttributes & FILE_ATTRIBUTE_READONLY) != 0)
            SetFileAttributesW(livePaths.deploy.c_str(), originalAttributes);
        CloseHandle(battleDirectory);
        DeleteFileW(temp.c_str());
        return Result::IoFailure;
    }
    SetFileAttributesW(livePaths.deploy.c_str(), originalAttributes);
    const bool flushed = FlushPath(livePaths.deploy);

    std::string replacedOriginalHash;
    result = HashContainedFile(
        rollback, livePaths.finalRoot, Result::DestinationMissing, &replacedOriginalHash);
    if (result != Result::Created ||
        _stricmp(replacedOriginalHash.c_str(), expectedCurrentHash.c_str()) != 0) {
        // A writer won after our last pre-commit check. ReplaceFile captured its exact bytes instead
        // of destroying them; restore that capture and retain READY authority as a conflict record.
        const std::wstring ownedOutput = rollback + L".owned";
        const bool restoredConflict = result == Result::Created &&
            ReplaceFileW(
                livePaths.deploy.c_str(), rollback.c_str(), ownedOutput.c_str(), 0,
                nullptr, nullptr) != FALSE;
        if (restoredConflict) {
            SetFileAttributesW(livePaths.deploy.c_str(), originalAttributes);
            FlushPath(livePaths.deploy);
            DeleteFileW(ownedOutput.c_str());
        }
        CloseHandle(battleDirectory);
        return result == Result::Created ? Result::DestinationHashMismatch : result;
    }
    DeleteFileW(rollback.c_str());
    CloseHandle(battleDirectory);
    if (!flushed) return Result::IoFailure;

    std::string readback;
    result = HashContainedFile(
        livePaths.deploy, livePaths.finalRoot, Result::DestinationMissing, &readback);
    return result == Result::Created && _stricmp(readback.c_str(), replacementHash.c_str()) == 0
        ? Result::Published
        : (result == Result::Created ? Result::PostRestoreMismatch : result);
}

Result PublishIntoAbsentLive(
    const std::wstring& source,
    const std::wstring& sourceFinalRoot,
    const DerivedPaths& livePaths,
    const std::string& replacementHash) {
    std::wstring temp;
    Result result = CopySourceToLiveTemp(
        source, sourceFinalRoot, livePaths, replacementHash, &temp);
    if (result != Result::Created) return result;
    if (PathExists(livePaths.deploy.c_str())) {
        DeleteFileW(temp.c_str());
        return Result::DestinationHashMismatch;
    }
    if (!MoveFileExW(temp.c_str(), livePaths.deploy.c_str(), MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return PathExists(livePaths.deploy.c_str())
            ? Result::DestinationHashMismatch
            : Result::IoFailure;
    }
    std::string readback;
    result = HashContainedFile(
        livePaths.deploy, livePaths.finalRoot, Result::DestinationMissing, &readback);
    return result == Result::Created && _stricmp(readback.c_str(), replacementHash.c_str()) == 0
        ? Result::Published
        : (result == Result::Created ? Result::PostRestoreMismatch : result);
}

Result DeleteOwnedLiveFile(
    const DerivedPaths& paths,
    const std::string& expectedHash) {
    HANDLE file = CreateFileW(
        paths.deploy.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Result::IoFailure;
    std::wstring finalPath;
    std::string hash;
    const bool safe = FinalPathFromHandle(file, &finalPath) &&
        IsContainedFinalPath(paths.finalRoot, finalPath) && Sha256FromOpenFile(file, &hash);
    if (!safe || _stricmp(hash.c_str(), expectedHash.c_str()) != 0) {
        CloseHandle(file);
        return safe ? Result::DestinationHashMismatch : Result::PathRejected;
    }
    FILE_DISPOSITION_INFO disposition = {};
    disposition.DeleteFile = TRUE;
    const bool deleted = SetFileInformationByHandle(
        file, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
    CloseHandle(file);
    return deleted ? Result::Restored : Result::IoFailure;
}

bool CleanupOwnedStage(const wchar_t* markerPath, const MarkerRecord& marker) {
    std::wstring stageRoot;
    if (!ResolveStageRootLexical(
            markerPath, marker.attemptId, marker.stageRelative, &stageRoot)) {
        return false;
    }
    if (!PathExists(stageRoot.c_str())) return true;
    DerivedPaths paths;
    if (!BuildDerivedPaths(stageRoot.c_str(), marker.battleId, &paths)) return false;
    if (PathExists(paths.deploy.c_str()) && !DeleteFileW(paths.deploy.c_str())) return false;
    if (PathExists(paths.backup.c_str()) && !DeleteFileW(paths.backup.c_str())) return false;
    if (!RemoveDirectoryW(paths.battleDirectory.c_str())) return false;
    if (!RemoveDirectoryW(stageRoot.c_str())) return false;
    std::wstring attemptDirectory = stageRoot.substr(0, stageRoot.find_last_of(L"\\/"));
    if (!RemoveDirectoryW(attemptDirectory.c_str())) return false;
    return true;
}

bool ArchivePreparedMarker(
    const wchar_t* markerPath,
    const std::string& expectedBytes,
    const std::string& suffix) {
    HANDLE marker = CreateFileW(
        markerPath, GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker == INVALID_HANDLE_VALUE) return false;
    std::string current;
    if (!ReadBoundedOpenFile(marker, kMaximumMarkerBytes, &current) || current != expectedBytes) {
        CloseHandle(marker);
        return false;
    }
    const std::wstring archive = std::wstring(markerPath) + L".archive-" +
        std::wstring(suffix.begin(), suffix.end()) + L".prepared";
    const DWORD nameBytes = static_cast<DWORD>(archive.size() * sizeof(wchar_t));
    std::vector<unsigned char> renameBytes(sizeof(FILE_RENAME_INFO) + nameBytes);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(renameBytes.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = nameBytes;
    std::memcpy(rename->FileName, archive.data(), nameBytes);
    const bool renamed = SetFileInformationByHandle(
        marker, FileRenameInfo, rename, static_cast<DWORD>(renameBytes.size())) != FALSE;
    CloseHandle(marker);
    return renamed;
}

bool FinalizeConsumption(
    const wchar_t* markerPath,
    const MarkerRecord& marker,
    const std::string& markerBytes,
    const ReadyRecord* ready) {
    ReadyRecord finalRecord;
    finalRecord.attemptId = marker.version == 2
        ? marker.attemptId
        : marker.deployedSha256.substr(0, 32);
    finalRecord.deployedSha256 = ready
        ? ready->deployedSha256
        : (marker.version == 2 ? marker.sourceSha256 : marker.deployedSha256);
    const std::wstring finalPath = FinalPath(markerPath, finalRecord.attemptId);
    const std::string finalBytes = SerializeFinal(finalRecord);
    if (PathExists(finalPath.c_str())) {
        std::string existing;
        if (!ReadBoundedFile(finalPath.c_str(), kMaximumMarkerBytes, &existing) || existing != finalBytes)
            return false;
    } else if (!WriteNewAuthorityFile(finalPath.c_str(), finalBytes)) {
        return false;
    }
    if (marker.version == 2 && !CleanupOwnedStage(markerPath, marker)) return false;
    return ArchivePreparedMarker(
        markerPath,
        markerBytes,
        marker.version == 2 ? marker.attemptId : marker.deployedSha256.substr(0, 16));
}

}  // namespace

bool IsStrictBattleId(const char* battleId) {
    if (!battleId) return false;
    const size_t length = std::strlen(battleId);
    if (length == 0 || length > kMaximumBattleIdLength) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(battleId[i]);
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) {
            return false;
        }
    }
    return true;
}

bool MarkerExists(const wchar_t* markerPath) {
    if (!markerPath || !markerPath[0]) return false;
    return IsRegularFileAttributes(GetFileAttributesW(markerPath));
}

bool PendingMarkerMatchesBattleId(const wchar_t* markerPath, const char* battleId) {
    if (!IsStrictBattleId(battleId)) return false;
    std::string bytes;
    MarkerRecord marker;
    return ReadBoundedFile(markerPath, kMaximumMarkerBytes, &bytes) &&
        ParseMarker(bytes, &marker) && _stricmp(marker.battleId.c_str(), battleId) == 0;
}

bool ResolveModuleMarkerPath(HMODULE module, wchar_t* outPath, size_t outPathCount) {
    if (!module || !outPath || outPathCount == 0) return false;
    std::vector<wchar_t> modulePath(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) return false;
    wchar_t* slash = std::wcsrchr(modulePath.data(), L'\\');
    if (!slash) return false;
    *(slash + 1) = L'\0';
    const std::wstring markerPath = std::wstring(modulePath.data()) + kPendingMarkerFileName;
    if (markerPath.size() + 1 > outPathCount) return false;
    std::wmemcpy(outPath, markerPath.c_str(), markerPath.size() + 1);
    return true;
}

namespace {

Result SealComposeAttemptUnlocked(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    if (!markerPath || !markerPath[0] || !composeManifestPath || !composeManifestPath[0] ||
        !ValidateAttemptShape(attempt)) {
        return Result::PathRejected;
    }

    std::string markerBytes;
    MarkerRecord marker;
    Result result = ReadPreparedAuthority(markerPath, &markerBytes, &marker);
    if (result != Result::Created || !AttemptMatchesMarker(attempt, marker))
        return result == Result::Created ? Result::InvalidMarker : result;

    bool manifestExists = false;
    std::string manifestAfterHash;
    if (!CaptureSmallFileIdentity(
            composeManifestPath, kMaximumManifestBytes, &manifestExists, &manifestAfterHash) ||
        !manifestExists) {
        return Result::InvalidManifest;
    }
    if (attempt.manifestExisted &&
        _stricmp(manifestAfterHash.c_str(), attempt.manifestBeforeSha256) == 0) {
        return Result::ManifestStale;
    }

    DerivedPaths stagePaths;
    if (!ResolveExistingStagePaths(markerPath, marker, &stagePaths, nullptr))
        return Result::PathRejected;
    std::string manifestBytes;
    result = ReadAndValidateComposeManifest(
        composeManifestPath, stagePaths, attempt.battleId, &manifestBytes);
    if (result != Result::Created) return result;

    std::string stageBackupHash;
    result = HashContainedFile(
        stagePaths.backup, stagePaths.finalRoot, Result::SourceMissing, &stageBackupHash);
    if (result != Result::Created) return result;
    if (_stricmp(stageBackupHash.c_str(), marker.sourceSha256.c_str()) != 0)
        return Result::SourceHashMismatch;

    ReadyRecord ready;
    ready.attemptId = marker.attemptId;
    result = HashContainedFile(
        stagePaths.deploy, stagePaths.finalRoot, Result::DestinationMissing, &ready.deployedSha256);
    if (result != Result::Created) return result;
    if (_stricmp(ready.deployedSha256.c_str(), marker.sourceSha256.c_str()) == 0)
        return Result::NoRestoreNeeded;

    const std::wstring readyPath = ReadyPath(markerPath, marker.attemptId);
    const std::string readyBytes = SerializeReady(ready);
    if (PathExists(readyPath.c_str())) {
        std::string existing;
        return ReadBoundedFile(readyPath.c_str(), kMaximumMarkerBytes, &existing) &&
                existing == readyBytes
            ? Result::StagedReady
            : Result::InvalidMarker;
    }
    return WriteNewAuthorityFile(readyPath.c_str(), readyBytes)
        ? Result::StagedReady
        : Result::IoFailure;
}

Result PublishComposeAttemptUnlocked(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    if (!markerPath || !markerPath[0] || !modBtlRoot || !modBtlRoot[0] ||
        !ValidateAttemptShape(attempt)) {
        return Result::PathRejected;
    }

    std::string markerBytes;
    MarkerRecord marker;
    Result result = ReadPreparedAuthority(markerPath, &markerBytes, &marker);
    if (result != Result::Created || !AttemptMatchesMarker(attempt, marker))
        return result == Result::Created ? Result::InvalidMarker : result;
    std::string readyBytes;
    ReadyRecord ready;
    result = ReadReadyAuthority(markerPath, marker, &readyBytes, &ready);
    if (result != Result::StagedReady) return result;

    DerivedPaths livePaths;
    if (!BuildDerivedPaths(modBtlRoot, marker.battleId, &livePaths) ||
        livePaths.deployRelative != marker.deployRelative ||
        livePaths.backupRelative != marker.backupRelative) {
        return Result::PathRejected;
    }
    Result beforeFailure = Result::DestinationHashMismatch;
    if (!DestinationMatchesBefore(livePaths, marker, &beforeFailure)) return beforeFailure;

    const std::wstring rollback = PublishRollbackPath(livePaths, marker.attemptId);
    if (PathExists(rollback.c_str())) return Result::PendingExists;

    DerivedPaths stagePaths;
    if (!ResolveExistingStagePaths(markerPath, marker, &stagePaths, nullptr))
        return Result::PathRejected;
    std::string stageHash;
    result = HashContainedFile(
        stagePaths.deploy, stagePaths.finalRoot, Result::DestinationMissing, &stageHash);
    if (result != Result::Created) return result;
    if (_stricmp(stageHash.c_str(), ready.deployedSha256.c_str()) != 0)
        return Result::DestinationHashMismatch;

    if (marker.destinationExisted) {
        return ReplaceLivePreservingMetadata(
            stagePaths.deploy,
            stagePaths.finalRoot,
            livePaths,
            marker,
            marker.destinationBeforeSha256,
            ready.deployedSha256);
    }
    return PublishIntoAbsentLive(
        stagePaths.deploy, stagePaths.finalRoot, livePaths, ready.deployedSha256);
}

Result RestorePendingUnlocked(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot) {
    if (!PathExists(markerPath)) return Result::NoMarker;
    std::string markerBytes;
    MarkerRecord marker;
    Result result = ReadPreparedAuthority(markerPath, &markerBytes, &marker);
    if (result != Result::Created) return result;

    DerivedPaths livePaths;
    if (!BuildDerivedPaths(modBtlRoot, marker.battleId, &livePaths) ||
        livePaths.deployRelative != marker.deployRelative ||
        livePaths.backupRelative != marker.backupRelative) {
        return Result::PathRejected;
    }
    std::string sourceHash;
    result = HashContainedFile(
        livePaths.backup, livePaths.finalRoot, Result::SourceMissing, &sourceHash);
    if (result != Result::Created) return result;
    if (_stricmp(sourceHash.c_str(), marker.sourceSha256.c_str()) != 0)
        return Result::SourceHashMismatch;

    if (marker.version == 1) {
        std::string destinationHash;
        result = HashContainedFile(
            livePaths.deploy, livePaths.finalRoot, Result::DestinationMissing, &destinationHash);
        if (result != Result::Created) return result;
        if (_stricmp(destinationHash.c_str(), marker.sourceSha256.c_str()) == 0) {
            return FinalizeConsumption(markerPath, marker, markerBytes, nullptr)
                ? Result::AlreadyRestoredConsumed
                : Result::IoFailure;
        }
        if (_stricmp(destinationHash.c_str(), marker.deployedSha256.c_str()) != 0)
            return Result::DestinationHashMismatch;
        MarkerRecord replacementMarker = marker;
        replacementMarker.attemptId = marker.deployedSha256.substr(0, 32);
        result = ReplaceLivePreservingMetadata(
            livePaths.backup,
            livePaths.finalRoot,
            livePaths,
            replacementMarker,
            marker.deployedSha256,
            marker.sourceSha256);
        if (result != Result::Published) return result;
        return FinalizeConsumption(markerPath, marker, markerBytes, nullptr)
            ? Result::Restored
            : Result::IoFailure;
    }

    std::string readyBytes;
    ReadyRecord ready;
    const Result readyResult = ReadReadyAuthority(markerPath, marker, &readyBytes, &ready);
    if (readyResult != Result::NoMarker && readyResult != Result::StagedReady)
        return readyResult;

    Result beforeFailure = Result::DestinationHashMismatch;
    if (DestinationMatchesBefore(livePaths, marker, &beforeFailure)) {
        return FinalizeConsumption(
                   markerPath,
                   marker,
                   markerBytes,
                   readyResult == Result::StagedReady ? &ready : nullptr)
            ? Result::AlreadyRestoredConsumed
            : Result::IoFailure;
    }
    if (readyResult != Result::StagedReady) return beforeFailure;

    std::string destinationHash;
    result = HashContainedFile(
        livePaths.deploy, livePaths.finalRoot, Result::DestinationMissing, &destinationHash);
    if (result != Result::Created) return result;
    if (_stricmp(destinationHash.c_str(), ready.deployedSha256.c_str()) != 0)
        return Result::DestinationHashMismatch;

    const std::wstring rollback = PublishRollbackPath(livePaths, marker.attemptId);
    if (PathExists(rollback.c_str())) {
        if (!marker.destinationExisted) return Result::DestinationHashMismatch;
        std::string rollbackHash;
        result = HashContainedFile(
            rollback, livePaths.finalRoot, Result::DestinationMissing, &rollbackHash);
        if (result != Result::Created) return result;
        if (_stricmp(rollbackHash.c_str(), marker.destinationBeforeSha256.c_str()) != 0)
            return Result::DestinationHashMismatch;
        if (!DeleteFileW(rollback.c_str())) return Result::IoFailure;
    }

    if (marker.destinationExisted) {
        result = ReplaceLivePreservingMetadata(
            livePaths.backup,
            livePaths.finalRoot,
            livePaths,
            marker,
            ready.deployedSha256,
            marker.sourceSha256);
        if (result != Result::Published) return result;
    } else {
        result = DeleteOwnedLiveFile(livePaths, ready.deployedSha256);
        if (result != Result::Restored) return result;
    }

    if (!DestinationMatchesBefore(livePaths, marker, &beforeFailure))
        return beforeFailure == Result::DestinationHashMismatch
            ? Result::PostRestoreMismatch
            : beforeFailure;
    return FinalizeConsumption(markerPath, marker, markerBytes, &ready)
        ? Result::Restored
        : Result::IoFailure;
}

}  // namespace

Result PrepareComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const wchar_t* modBtlRoot,
    const wchar_t* vanillaBtlRoot,
    const char* expectedBattleId,
    ComposeAttempt* outAttempt) {
    if (!markerPath || !markerPath[0] || !composeManifestPath || !composeManifestPath[0] ||
        !modBtlRoot || !modBtlRoot[0] || !IsStrictBattleId(expectedBattleId) || !outAttempt) {
        return Result::PathRejected;
    }
    *outAttempt = ComposeAttempt{};
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    if (PathExists(markerPath)) return Result::PendingExists;

    DerivedPaths livePaths;
    if (!BuildDerivedPaths(modBtlRoot, expectedBattleId, &livePaths))
        return Result::PathRejected;
    bool manifestExisted = false;
    std::string manifestBeforeHash;
    if (!CaptureSmallFileIdentity(
            composeManifestPath, kMaximumManifestBytes, &manifestExisted, &manifestBeforeHash)) {
        return Result::InvalidManifest;
    }

    const bool destinationExisted = PathExists(livePaths.deploy.c_str());
    const bool backupExisted = PathExists(livePaths.backup.c_str());
    std::string sourceHash;
    std::string destinationBeforeHash;
    if (backupExisted) {
        Result result = HashContainedFile(
            livePaths.backup, livePaths.finalRoot, Result::SourceMissing, &sourceHash);
        if (result != Result::Created) return result;
        if (destinationExisted) {
            result = HashContainedFile(
                livePaths.deploy,
                livePaths.finalRoot,
                Result::DestinationMissing,
                &destinationBeforeHash);
            if (result != Result::Created) return result;
            if (_stricmp(destinationBeforeHash.c_str(), sourceHash.c_str()) != 0)
                return Result::SourceHashMismatch;
        } else {
            DerivedPaths vanillaPaths;
            std::string vanillaHash;
            if (!vanillaBtlRoot || !vanillaBtlRoot[0] ||
                !BuildDerivedPaths(vanillaBtlRoot, expectedBattleId, &vanillaPaths)) {
                return Result::SourceMissing;
            }
            result = HashContainedFile(
                vanillaPaths.deploy, vanillaPaths.finalRoot, Result::SourceMissing, &vanillaHash);
            if (result != Result::Created) return result;
            if (_stricmp(vanillaHash.c_str(), sourceHash.c_str()) != 0)
                return Result::SourceHashMismatch;
        }
    } else {
        if (destinationExisted) {
            Result result = CreateVerifiedBackupFromSource(
                livePaths.deploy, livePaths.finalRoot, livePaths, &sourceHash);
            if (result != Result::Created) return result;
            destinationBeforeHash = sourceHash;
        } else {
            DerivedPaths vanillaPaths;
            if (!vanillaBtlRoot || !vanillaBtlRoot[0] ||
                !BuildDerivedPaths(vanillaBtlRoot, expectedBattleId, &vanillaPaths)) {
                return Result::SourceMissing;
            }
            Result result = CreateVerifiedBackupFromSource(
                vanillaPaths.deploy, vanillaPaths.finalRoot, livePaths, &sourceHash);
            if (result != Result::Created) return result;
        }
    }

    std::string attemptId;
    std::wstring stageRoot;
    bool stageCreated = false;
    for (unsigned retry = 0; retry < 8 && !stageCreated; ++retry) {
        if (!GenerateAttemptId(&attemptId)) return Result::IoFailure;
        stageCreated = CreateStageTree(markerPath, attemptId, expectedBattleId, &stageRoot);
    }
    if (!stageCreated) return Result::IoFailure;

    MarkerRecord marker;
    marker.version = 2;
    marker.battleId = expectedBattleId;
    marker.attemptId = attemptId;
    marker.deployRelative = livePaths.deployRelative;
    marker.backupRelative = livePaths.backupRelative;
    marker.stageRelative = StageRelativeForAttempt(attemptId);
    marker.sourceSha256 = sourceHash;
    marker.destinationExisted = destinationExisted;
    marker.destinationBeforeSha256 = destinationBeforeHash;
    marker.manifestExisted = manifestExisted;
    marker.manifestBeforeSha256 = manifestBeforeHash;

    DerivedPaths stagePaths;
    if (!BuildDerivedPaths(stageRoot.c_str(), expectedBattleId, &stagePaths) ||
        !CopyFileW(livePaths.backup.c_str(), stagePaths.deploy.c_str(), TRUE)) {
        CleanupOwnedStage(markerPath, marker);
        return Result::IoFailure;
    }
    std::string stagedSeedHash;
    Result result = HashContainedFile(
        stagePaths.deploy, stagePaths.finalRoot, Result::DestinationMissing, &stagedSeedHash);
    if (result != Result::Created || _stricmp(stagedSeedHash.c_str(), sourceHash.c_str()) != 0) {
        CleanupOwnedStage(markerPath, marker);
        return result == Result::Created ? Result::PostRestoreMismatch : result;
    }

    ComposeAttempt attempt = {};
    if (!CopyAscii(attempt.battleId, std::size(attempt.battleId), marker.battleId) ||
        !CopyAscii(attempt.attemptId, std::size(attempt.attemptId), marker.attemptId) ||
        !CopyAscii(
            attempt.expectedSourceSha256,
            std::size(attempt.expectedSourceSha256),
            marker.sourceSha256) ||
        (destinationExisted && !CopyAscii(
            attempt.destinationBeforeSha256,
            std::size(attempt.destinationBeforeSha256),
            destinationBeforeHash)) ||
        (manifestExisted && !CopyAscii(
            attempt.manifestBeforeSha256,
            std::size(attempt.manifestBeforeSha256),
            manifestBeforeHash)) ||
        !CopyWide(attempt.stagingModRoot, std::size(attempt.stagingModRoot), stageRoot)) {
        CleanupOwnedStage(markerPath, marker);
        return Result::IoFailure;
    }
    attempt.destinationExisted = destinationExisted;
    attempt.manifestExisted = manifestExisted;

    // WHY: PREPARED is immutable and flushed before CreateProcess. A crash at any later boundary
    // therefore has exact prestate authority while the lab can mutate only this attempt's stage.
    if (!WriteNewAuthorityFile(markerPath, SerializeMarker(marker))) {
        CleanupOwnedStage(markerPath, marker);
        return Result::IoFailure;
    }
    *outAttempt = attempt;
    return Result::PreflightReady;
}

Result SealComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    return SealComposeAttemptUnlocked(
        markerPath, composeManifestPath, attempt, featureEnabled);
}

Result PublishComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    return PublishComposeAttemptUnlocked(markerPath, modBtlRoot, attempt, featureEnabled);
}

Result FinalizeComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* composeManifestPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    Result result = SealComposeAttemptUnlocked(
        markerPath, composeManifestPath, attempt, featureEnabled);
    if (result == Result::NoRestoreNeeded) {
        const Result recovered = RestorePendingUnlocked(markerPath, modBtlRoot);
        return recovered == Result::AlreadyRestoredConsumed
            ? Result::NoRestoreNeeded
            : recovered;
    }
    if (result != Result::StagedReady) return result;
    result = PublishComposeAttemptUnlocked(markerPath, modBtlRoot, attempt, featureEnabled);
    return result == Result::Published ? Result::Created : result;
}

Result RollbackComposeAttempt(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    const ComposeAttempt& attempt) {
    if (!markerPath || !markerPath[0] || !modBtlRoot || !modBtlRoot[0] ||
        !ValidateAttemptShape(attempt)) {
        return Result::PathRejected;
    }
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    std::string markerBytes;
    MarkerRecord marker;
    const Result read = ReadPreparedAuthority(markerPath, &markerBytes, &marker);
    if (read != Result::Created || !AttemptMatchesMarker(attempt, marker))
        return read == Result::Created ? Result::InvalidMarker : read;
    return RestorePendingUnlocked(markerPath, modBtlRoot);
}

Result RestorePending(
    const wchar_t* markerPath,
    const wchar_t* modBtlRoot,
    RestorePolicy policy) {
    // WHY: validation-only returns before creating/opening the cross-process lock, so even the
    // durable lock file remains untouched. Feature OFF may enter only this owned teardown path.
    if (policy.validateOnly) return Result::BlockedValidateOnly;
    if (!markerPath || !markerPath[0] || !modBtlRoot || !modBtlRoot[0])
        return Result::PathRejected;
    // Feature OFF with no self-owned marker is a strict no-write path, including no `.lock` file.
    if (!PathExists(markerPath)) return Result::NoMarker;
    ScopedSrwExclusive processLock;
    ScopedTransactionLock transactionLock(markerPath);
    if (!transactionLock.locked()) return Result::IoFailure;
    return RestorePendingUnlocked(markerPath, modBtlRoot);
}

bool ProductionDiskTransactionsAvailable() {
    return !kProductionDiskTransactionsQuarantined;
}

bool ModuleMarkerPresent(HMODULE module) {
    // WHY: marker presence used to perform read-only path I/O even while every transaction adapter
    // was quarantined. Keep the promotion boundary literal so boot and live-OFF probes stay inert.
    if (!ProductionDiskTransactionsAvailable()) return false;
    wchar_t markerPath[32768] = {};
    return ResolveModuleMarkerPath(module, markerPath, std::size(markerPath)) && PathExists(markerPath);
}

Result PrepareComposeAttemptForModule(
    HMODULE module,
    const char* composeManifestPath,
    const char* modBtlRoot,
    const char* vanillaBtlRoot,
    const char* expectedBattleId,
    ComposeAttempt* outAttempt) {
    if (!ProductionDiskTransactionsAvailable()) return Result::Quarantined;
    wchar_t markerPath[32768] = {};
    std::wstring manifestWide;
    std::wstring modRootWide;
    std::wstring vanillaRootWide;
    if (!ResolveModuleMarkerPath(module, markerPath, std::size(markerPath)) ||
        !AnsiPathToWide(composeManifestPath, &manifestWide) ||
        !AnsiPathToWide(modBtlRoot, &modRootWide) ||
        !AnsiPathToWide(vanillaBtlRoot, &vanillaRootWide)) {
        return Result::PathRejected;
    }
    return PrepareComposeAttempt(
        markerPath,
        manifestWide.c_str(),
        modRootWide.c_str(),
        vanillaRootWide.c_str(),
        expectedBattleId,
        outAttempt);
}

Result FinalizeComposeAttemptForModule(
    HMODULE module,
    const char* composeManifestPath,
    const char* modBtlRoot,
    const ComposeAttempt& attempt,
    bool featureEnabled) {
    if (!featureEnabled) return Result::BlockedFeatureOff;
    if (!ProductionDiskTransactionsAvailable()) return Result::Quarantined;
    wchar_t markerPath[32768] = {};
    std::wstring manifestWide;
    std::wstring modRootWide;
    if (!ResolveModuleMarkerPath(module, markerPath, std::size(markerPath)) ||
        !AnsiPathToWide(composeManifestPath, &manifestWide) ||
        !AnsiPathToWide(modBtlRoot, &modRootWide)) {
        return Result::PathRejected;
    }
    return FinalizeComposeAttempt(
        markerPath, manifestWide.c_str(), modRootWide.c_str(), attempt, featureEnabled);
}

Result RollbackComposeAttemptForModule(
    HMODULE module,
    const char* modBtlRoot,
    const ComposeAttempt& attempt) {
    if (!ProductionDiskTransactionsAvailable()) return Result::Quarantined;
    wchar_t markerPath[32768] = {};
    std::wstring modRootWide;
    if (!ResolveModuleMarkerPath(module, markerPath, std::size(markerPath)) ||
        !AnsiPathToWide(modBtlRoot, &modRootWide)) {
        return Result::PathRejected;
    }
    return RollbackComposeAttempt(markerPath, modRootWide.c_str(), attempt);
}

bool ComposeAttemptStagingModRootAnsi(
    const ComposeAttempt& attempt,
    char* outPath,
    size_t outPathCount) {
    return ValidateAttemptShape(attempt) &&
        WidePathToAnsi(attempt.stagingModRoot, outPath, outPathCount);
}

Result RestorePendingForModule(
    HMODULE module,
    const char* modBtlRoot,
    RestorePolicy policy) {
    // Keep validation-only ahead of every adapter operation. The production quarantine is next, so
    // normal and feature-OFF calls both stop before module/path/lock/marker I/O.
    if (policy.validateOnly) return Result::BlockedValidateOnly;
    if (!ProductionDiskTransactionsAvailable()) return Result::Quarantined;
    wchar_t markerPath[32768] = {};
    std::wstring modRootWide;
    if (!ResolveModuleMarkerPath(module, markerPath, std::size(markerPath)) ||
        !AnsiPathToWide(modBtlRoot, &modRootWide)) {
        return Result::PathRejected;
    }
    return RestorePending(markerPath, modRootWide.c_str(), policy);
}

const char* ResultName(Result result) {
    switch (result) {
        case Result::Created: return "CREATED";
        case Result::Updated: return "UPDATED";
        case Result::PreflightReady: return "PREFLIGHT_READY";
        case Result::NoRestoreNeeded: return "NO_RESTORE_NEEDED";
        case Result::Restored: return "RESTORED";
        case Result::AlreadyRestoredConsumed: return "ALREADY_RESTORED_CONSUMED";
        case Result::BlockedValidateOnly: return "BLOCKED_VALIDATE_ONLY";
        case Result::BlockedFeatureOff: return "BLOCKED_FEATURE_OFF";
        case Result::NoMarker: return "NO_MARKER";
        case Result::PendingExists: return "PENDING_EXISTS";
        case Result::InvalidManifest: return "INVALID_MANIFEST";
        case Result::ManifestStale: return "MANIFEST_STALE";
        case Result::ManifestMismatch: return "MANIFEST_MISMATCH";
        case Result::InvalidMarker: return "INVALID_MARKER";
        case Result::PathRejected: return "PATH_REJECTED";
        case Result::SourceMissing: return "SOURCE_MISSING";
        case Result::DestinationMissing: return "DESTINATION_MISSING";
        case Result::SourceHashMismatch: return "SOURCE_HASH_MISMATCH";
        case Result::DestinationHashMismatch: return "DESTINATION_HASH_MISMATCH";
        case Result::IoFailure: return "IO_FAILURE";
        case Result::PostRestoreMismatch: return "POST_RESTORE_MISMATCH";
        case Result::StagedReady: return "STAGED_READY";
        case Result::Published: return "PUBLISHED";
        case Result::Quarantined: return "QUARANTINED";
    }
    return "UNKNOWN";
}

}  // namespace FfxHooks::ArenaComposeRestore
