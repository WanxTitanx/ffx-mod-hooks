#!/usr/bin/env bash
set -euo pipefail

precode_root="$(cd "$(dirname "$0")" && pwd -P)"
mkdir -p "$precode_root/build"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$precode_root/include" \
  "$precode_root/src/mod_ideas.cpp" \
  "$precode_root/tests/test_mod_ideas.cpp" \
  -o "$precode_root/build/mod_ideas_sanitized"
ASAN_OPTIONS=detect_leaks=1 "$precode_root/build/mod_ideas_sanitized"
