#pragma once
#include "UiLanguage.h"
#include <cstdio>
#include <type_traits>

// Jarvis-HOOK: Raw preserves user names, filenames and configuration identities.
namespace FfxHooks::UiLanguage {
struct Raw { const char* value; };
inline const char* Argument(Raw value) noexcept { return value.value ? value.value : ""; }
inline const char* Argument(const char* value) noexcept { return Text(value); }
inline const char* Argument(char* value) noexcept { return Text(value); }
template<class T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
inline T Argument(T value) noexcept { return value; }

template<class... Args>
inline bool Format(char* output, std::size_t capacity, const char* source, Args... args) noexcept {
    if (!output || !capacity || !source) return false;
    // Only compiled templates reach printf. The catalog builder validates
    // argument types, order and widths against the canonical English source.
    char complete[4096]{};
    const int length = std::snprintf(complete, sizeof(complete), Text(source), Argument(args)...);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(complete) || !ValidUtf8(complete)) {
        output[0] = 0;
        return false;
    }
    const auto copied = CopyUtf8(output, capacity, complete);
    return copied == static_cast<std::size_t>(length);
}
}
