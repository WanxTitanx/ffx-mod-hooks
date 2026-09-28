# Runtime distribution notices

Jarvis-HOOK, 2026-09-28. These notices accompany the static dependencies in the
FFX Hooks Windows DLL. No third-party implementation was adapted for this
packaging change. The repository root LICENSE and NOTICE also ship in the ZIP.

| Notice | Upstream | License | Copied from |
| --- | --- | --- | --- |
| polyhook2.txt | https://github.com/stevemk14ebr/PolyHook_2_0 | MIT | Existing `vcpkg_installed/x86-windows-static/share/polyhook2/copyright` |
| zydis.txt | https://github.com/zyantific/zydis | MIT | Existing `vcpkg_installed/x86-windows-static/share/zydis/copyright` |
| zycore.txt | https://github.com/zyantific/zycore-c | MIT | Existing `vcpkg_installed/x86-windows-static/share/zycore/copyright` |
| asmjit.txt | https://github.com/asmjit/asmjit | Zlib | Existing `vcpkg_installed/x86-windows-static/share/asmjit/copyright` |
| asmtk.txt | https://github.com/asmjit/asmtk | Zlib | Existing `vcpkg_installed/x86-windows-static/share/asmtk/copyright` |
| minhook.txt | https://github.com/TsudaKageyu/minhook | BSD-2-Clause | License header from the already vendored MinHook source |

The vcpkg paths are relative to `src/runtime/FfxHooksDll/`. The copied notices
are tracked here (trailing whitespace normalized without changing license text), so assembling the runtime package does not require that local
dependency cache. Build dependencies remain declared by the existing pinned
vcpkg workflow. Arcana adds no new native third-party dependency.
