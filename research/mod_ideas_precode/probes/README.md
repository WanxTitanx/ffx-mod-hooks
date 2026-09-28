# Linked-source save layout probe

This executable compiles the current FFX Editor FfxSaveEquipment.cs without copying or editing it. It replaces only FfxSaveCore with the two byte accessors the class uses. It does not write the fixture or any game save.

Run from the Hooks precode worktree:

~~~bash
dotnet run --project research/mod_ideas_precode/probes/save_layout/save_layout_probe.csproj \
  -p:EditorRoot=/home/wanderson/Documents/ffx-editor-main -- \
  /home/wanderson/Documents/ffx-editor-main/FFXProjectEditor.Tests/Fixtures/Save/user_ffx_000
~~~

The pinned fixture is a genuine 26880-byte PC save with SHA-256 6e2a617b58cc058a72526f20a31bf00b4d79847f7784ce5845935538e77af0b3. It stays in the Editor checkout and outside this branch. The success verdict means the defect is reproduced, not that gear writing is safe.

## Aeon element authoring probe

This second executable links the current Editor project and edits seven existing Aeon command rows in memory. It checks the row owners, the exact changed file offsets, and a reread. It never writes command.bin.

~~~bash
DOTNET_ROLL_FORWARD=Major dotnet run \
  --project research/mod_ideas_precode/probes/aeon_elements/aeon_element_probe.csproj \
  -p:EditorRoot=/home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor -- \
  /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor/FFXProjectEditor.Tests/Fixtures/Battle/command.bin
~~~

The probe uses command IDs 203–206 for Valefor and 216–218 for Bahamut, matching the FFX Editor command catalog. Separate in-memory copies edit Kimahri Ronso Rage #104's OD cost and Valefor Attack #203's CharacterUser. A pass proves localized data authoring; menu behavior and the separate OD menu-ready gate remain runtime questions.

## PE signature probe

The Python PE probe requires the SHA-256 and PE identity of the archived Steam FFX.exe copy used for this research. It maps each RVA to file bytes and checks 11 signatures. The input is read only.

~~~bash
python3 research/mod_ideas_precode/probes/pe_signature_probe.py /path/to/verified/FFX.exe
~~~

For another executable, --inspect-unknown prints diagnostic bytes but exits without a passing identity verdict. An IDA flat address equals the listed RVA plus this PE's image base of 0x400000.

## One-command offline suite

The runner checks the pinned Editor commit and a clean Editor worktree, executes the C++ tests and sanitizers, a Windows x86 cross-compilation, PE signatures, both linked-source probes, the Editor writer tests, and monmagic grow in a temporary directory. It writes only logs and a JSON summary under research/mod_ideas_precode/build/, which Git ignores. The PE32 executable is compiled but not run on this host.

~~~bash
python3 research/mod_ideas_precode/run_offline_suite.py \
  --editor-root /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor \
  --ffx-exe '/path/to/archived/FFX.exe'
~~~

The verdict PASS_RT0_AND_MODEL covers only source and offline behavior. The game, installed executable, UI, animations, save writer, and RT2 remain separate gates.

## Isolated Windows x86 model run

After run_win32_compile.sh, the following probe uploads the standalone model executable through QEMU guest agent to a random filename under C:/Users/Public/Documents, verifies its SHA-256, runs it, and deletes it in a finally block. It does not load FFX.exe or any Hooks DLL.

~~~bash
python3 research/mod_ideas_precode/probes/run_win32_vm.py \
  --exe research/mod_ideas_precode/build/mod_ideas_win32.exe \
  --vm-exec /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor/research_tools/Vm/vm_exec.sh
~~~

A passing result is an isolated Windows x86 model observation (RT1); it is not a game observation (RT2).

## MSVC x86 compiler gate

run_msvc_vm.py transfers only the three pure C++17 source files to a random directory under C:/Users/Public/Documents, invokes the VM's Visual Studio Community x86 compiler, runs the resulting standalone test executable, and removes the directory. It never builds or loads the Hooks DLL.

~~~bash
python3 research/mod_ideas_precode/probes/run_msvc_vm.py \
  --vm-exec /home/wanderson/.codex/worktrees/mod-ideas-precode/ffx-editor/research_tools/Vm/vm_exec.sh
~~~

The command targets windows11-dev-next. Use --vcvarsall if the installed Visual Studio path differs. Its output establishes MSVC x86 source compatibility and isolated model execution only.
