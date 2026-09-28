#!/usr/bin/env bash
set -euo pipefail

precode_root="$(cd "$(dirname "$0")" && pwd -P)"
mkdir -p "$precode_root/build"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -static-libgcc -static-libstdc++ \
  -I"$precode_root/include" \
  "$precode_root/src/mod_ideas.cpp" \
  "$precode_root/tests/test_mod_ideas.cpp" \
  -o "$precode_root/build/mod_ideas_win32.exe"
file "$precode_root/build/mod_ideas_win32.exe"
