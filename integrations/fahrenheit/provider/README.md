# Optional cooperative Fahrenheit provider

Jarvis-HOOK adds protocol 2 to the reviewed alpha12 source without editing the
original checkout. This is a local integration, not an upstream Fahrenheit release.
It uses added public callbacks rather than reflection, private managed offsets
or interception of unrelated .NET FileStream operations.

## Build a paired V2 candidate

Use .NET 10, Python 3 and unmodified Fahrenheit source at
cdb145d93295c1c6e2bf4766fda5a12877369f54. From the FFX Hooks repository root:

~~~sh
python3 integrations/fahrenheit/provider/build_overlay.py /path/to/fahrenheit /path/to/new-cooperative-copy
python3 integrations/fahrenheit/provider/build_overlay.py /path/to/new-cooperative-copy --verify
dotnet build /path/to/new-cooperative-copy/src/runtime/Fahrenheit.Runtime.csproj -c Release
dotnet build integrations/fahrenheit/FfxHooks.Fahrenheit.csproj -c Release -p:FahrenheitSourceRoot=/path/to/new-cooperative-copy
python3 integrations/fahrenheit/validate_package.py
~~~

Build the native DLL with build_hooks.ps1 -WithPolyHook -Release on Windows.
These commands never deploy or start the game. The overlay refuses an existing
destination. Verification reverses every patch and compares recovered bytes with
pinned upstream hashes; a modified marker cannot authorize different code.

The provider changes three source files and adds one core service: bootstrap,
save/load/cancel transport and read-only EFL resolution. Existing APIs and module
callbacks keep their shape. Without a cooperative client, original I/O paths
remain available. With an attached client, failed services cannot silently fall
through to unprojected save output.

Verification covers the complete 501-file official source tree, including core,
runtime and build inputs. Additional C# files and injected build targets are
rejected even inside bin/obj-named directories. The verifier proves the reviewed
source relationship, not the authenticity of a compiler or dependency cache.

Failed managed reads have one cleanup owner. The save UI closes without retiring
an issued ticket twice; CRC rejection retains the game's abort-and-return path,
while real I/O failures retain the upstream exception policy. Native cancellation
or abort refusals are reported to the managed caller.

## Packaging

Treat the native DLL, V2 bridge's four runtime/manifest members and matching
modified fh.dll/fhr.dll provider outputs as one candidate. Never put private
copies of framework or MinHook DLLs in the bridge directory. Provider outputs
belong to their existing provider layout and dependencies, not another DINPUT8
proxy. The separately built V1 bridge remains usable with stock Fahrenheit and
explicit restrictions. Do not combine a V2 bridge with a V1 provider.

Installation and in-game tests require separate authorization. Restart to change
provider, bridge, text package or native DLL; no hot unload is advertised.
Fastload selection remains Fahrenheit-owned. Protocol 2 coordinates actual I/O,
not a competing bootstrap shortcut.

Complete corresponding modified source, COPYING, COPYING.LESSER and NOTICE must
accompany redistributed modified provider binaries. Generated source is retained
beside local candidates. These commands submit no upstream modification.
See [licensing and provenance](NOTICE.md).

## Tests

~~~sh
dotnet run --project integrations/fahrenheit/provider/tests/ProviderIoTests.csproj -c Release
dotnet run --project integrations/fahrenheit/tests/BridgeLifecycleTests.csproj -c Release
python3 integrations/fahrenheit/provider/tests/test_overlay.py
python3 -m unittest integrations.fahrenheit.tests.test_verify_upstream integrations.fahrenheit.provider.tests.test_overlay
python3 integrations/fahrenheit/provider/tests/run_composition.py --reference /path/to/fahrenheit
~~~

The source-verification tests use the pinned checkout in work/fahrenheit/upstream.
The composition test accepts an explicit reference directory and executes the
actual patched load/UI methods with controlled game endpoints.

Native fixtures: fahrenheit_managed_io_rt1.ps1 and fahrenheit_text_rt1.ps1 under
the DLL project. The supplied tests/run_transport.ps1 builds a self-contained
x86 CLR harness against a supplied native candidate and exact private PE/save
fixtures. It maps FFX with DONT_RESOLVE_DLL_REFERENCES and never calls its entrypoint.

The [current evidence ledger](../../../docs/research/F8_F7_AUDIO_FAHRENHEIT_V2_2026_09_29.md)
records counts, candidate identities and limits. These tests do not establish
live F7/F8 visual acceptance or compatibility with arbitrary mods.
