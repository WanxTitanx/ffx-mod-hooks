# MinHook — bundled source

MinHook by Tsuda Kageyu — https://github.com/TsudaKageyu/minhook

License: BSD-2-Clause

These source files are bundled directly in the repo (not via vcpkg) because
the vcpkg `minhook` port is header/source-only and does not produce a `.lib`
in CI environments. Compiling the 4 `.c` files directly is the reliable
approach for both local and CI builds.

Only `hde32.c` is needed for x86 builds; `hde64.c` is included for completeness.
