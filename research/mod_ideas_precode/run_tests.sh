#!/usr/bin/env bash
set -euo pipefail

precode_root="$(cd "$(dirname "$0")" && pwd -P)"
mkdir -p "$precode_root/build"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$precode_root/include" \
  "$precode_root/src/mod_ideas.cpp" \
  "$precode_root/tests/test_mod_ideas.cpp" \
  -o "$precode_root/build/mod_ideas_tests"
"$precode_root/build/mod_ideas_tests"
