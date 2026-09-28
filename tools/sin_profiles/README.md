# S.I.N. private profile export — Jarvis-HOOK

This offline exporter uses the existing FFX Editor compiler, planner and validator.
It reads clean monster files and writes an explicit private output directory.
It never edits the Editor inputs or the game installation. The runtime receives
only AI views; its generated C++ header contains hashes, not game bytecode.

Requirements: .NET 10 SDK, a compatible built Editor assembly and its dependencies,
clean pilot monster files, and the installed Spira Reforge command table. Keep
all game assets, generated `.ai` files and `_sin-ai-v1.bin` outside Git.

From this repository, build with the exact Editor assembly path:

```sh
dotnet build tools/sin_profiles/SinProfiles.csproj -c Release \
  -p:EditorAssemblyPath="/absolute/path/FFXProjectEditor.dll"
```

Run from the Editor repository root so its existing recipe resources resolve:

```sh
dotnet /absolute/hooks/tools/sin_profiles/bin/Release/net10.0/SinProfiles.dll \
  --editor /absolute/path/FFXProjectEditor.dll \
  --source '/absolute/Editor/mods/Spira Reforge/sin-clean-bins' \
  --output /absolute/private/sin-profiles
```

The manifest records the Editor assembly identity, pack identity and each AI
profile. Reproduce the admission header into a temporary file for comparison:

```sh
python3 tools/sin_profiles/generate_proofs.py \
  --manifest /absolute/private/sin-profiles/manifest.json \
  --pack /absolute/private/sin-profiles/_sin-ai-v1.bin \
  --commands /absolute/game/data/mods/ffx_ps2/ffx/master/new_uspc/battle/kernel/monmagic2.bin \
  --output /absolute/private/SinAiProfiles.generated.h
```

Compare with `hooks/SinAiProfiles.generated.h` under the runtime project before
changing the supported identities. A newly generated profile is not gameplay
validation. The runtime also checks native registration identity, natural battle
ownership, original AI hashes and exact custom command dependencies, and retains
the original script when any admission check fails.
