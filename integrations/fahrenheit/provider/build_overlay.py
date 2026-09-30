"""Create/verify the optional LGPL provider from the exact reviewed Fahrenheit source.

No checkout is edited in place. Reversing every patch must recover the pinned
upstream hash; a rewritten marker cannot bless modified implementation bytes.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
REFERENCE = HERE.parent / "upstream-reference.json"
MARKER = "ffx-hooks-cooperative.json"
ADDED = "src/core/ffx_cooperative_services.cs"
PATCHES = {
    "src/core/bootloader.cs": [(
        "        FhInternal.Methods.commit();",
        "        bool committed = FhInternal.Methods.commit();\n"
        "        FhCooperativeServices.Current.CompleteBootstrap(committed);"
    )],
    "src/runtime/save_impl.cs": [(
        "        return FhCall.SaveDataManager_debugSave_Internal_6F0650.hook(this, impl_autosave)",
        "        bool installed = FhCall.SaveDataManager_debugSave_Internal_6F0650.hook(this, impl_autosave)"
    ), (
        "            && (!is_ffx || FFX.FhCall.FUN_2EFFF0.hook(this, signal_enter_albd));",
        "            && (!is_ffx || FFX.FhCall.FUN_2EFFF0.hook(this, signal_enter_albd));\n"
        "        if (installed && is_ffx) FhCooperativeServices.Current.RegisterSaveProvider(this);\n"
        "        return installed;"
    ), (
        "    internal void signal_exit_abort() {\n        FhSavePal.pal_set_cancel_state(1);",
        "    internal void signal_exit_abort() {\n"
        "        signal_exit_abort(false);\n"
        "    }\n"
        "    private void signal_exit_abort(bool transportOwnsCleanup) {\n"
        "        if (!transportOwnsCleanup && FhGlobal.game_id is FhGameId.FFX && _mode is not FhSaveSystemMode.SAVE)\n"
        "            FhCooperativeServices.Current.CancelPendingRead((nint)FhSavePal.pal_addr_buf_save(), FhSavePal.pal_sz_buf_save());\n"
        "        FhSavePal.pal_set_cancel_state(1);"
    ), (
        "        using (FileStream save_stream = File.OpenWrite(save_path)) {\n"
        "            save_stream.Write(save);\n        }\n\n        FhInternal.State.state_save_slot(0);",
        "        if (FhGlobal.game_id is not FhGameId.FFX || !FhCooperativeServices.Current.TryWrite(save_path, 0, save)) {\n"
        "            using FileStream save_stream = File.OpenWrite(save_path);\n"
        "            save_stream.Write(save);\n        }\n\n        FhInternal.State.state_save_slot(0);"
    ), (
        "        using (FileStream save_stream = File.OpenWrite(save_path)) {\n"
        "            save_stream.Write(save);\n        }\n\n        FhInternal.State.state_save_slot(slot);",
        "        if (FhGlobal.game_id is not FhGameId.FFX || !FhCooperativeServices.Current.TryWrite(save_path, slot, save)) {\n"
        "            using FileStream save_stream = File.OpenWrite(save_path);\n"
        "            save_stream.Write(save);\n        }\n\n        FhInternal.State.state_save_slot(slot);"
    ), (
        "        // TODO: add popups on success/failure\n"
        "        using (FileStream save_stream = File.OpenRead(save_name)) {\n"
        "            save_stream.ReadExactly(save);\n        }\n\n"
        "        // From Iggy state handler 0xF\n"
        "        if (FhCall.SaveDataCheckCrc.fnptr!() == 0) {\n"
        "            save.Clear();\n\n            signal_exit_abort();\n            return;\n        }",
        "        // The cooperative client sees the real ref buffer before CRC and RAM copy.\n"
        "        bool cooperative;\n"
        "        bool transportOwnsCleanup = false;\n"
        "        try {\n"
        "            cooperative = FhGlobal.game_id is FhGameId.FFX &&\n"
        "                FhCooperativeServices.Current.TryRead(save_name, slot, save, () => FhCall.SaveDataCheckCrc.fnptr!() != 0, out transportOwnsCleanup);\n"
        "        } catch (InvalidDataException) {\n"
        "            signal_exit_abort(transportOwnsCleanup);\n            return;\n"
        "        } catch {\n"
        "            signal_exit_abort(transportOwnsCleanup);\n            throw;\n        }\n"
        "        if (!cooperative) {\n"
        "            using (FileStream save_stream = File.OpenRead(save_name)) save_stream.ReadExactly(save);\n"
        "            if (FhCall.SaveDataCheckCrc.fnptr!() == 0) {\n"
        "                save.Clear();\n                signal_exit_abort();\n                return;\n            }\n        }"
    )],
    "src/runtime/fileloader.cs": [(
        "        return FhCall.Phyre_PSerialization_PStreamFile_ctor           .hook(this, h_fopen)",
        "        bool installed = FhCall.Phyre_PSerialization_PStreamFile_ctor           .hook(this, h_fopen)"
    ), (
        "            && FhCall.fiosUnifyFilename                               .hook(this, h_fiosUnifyFilename);",
        "            && FhCall.fiosUnifyFilename                               .hook(this, h_fiosUnifyFilename);\n"
        "        if (installed && FhGlobal.game_id is FhGameId.FFX)\n"
        "            FhCooperativeServices.Current.RegisterResourceProvider(this, path => _index.ContainsKey(path.ToUpperInvariant()));\n"
        "        return installed;"
    ), (
        "            if (!_index.TryGetValue(path_str, out string? path_modded)) {",
        "            // The client checks the full EFL index before pairing fonts/text.\n"
        "            // Only read-only, explicitly claimed resources transfer an OS handle.\n"
        "            if (FhGlobal.game_id is FhGameId.FFX && FhCooperativeServices.Current.TryOpenResource(path_str, read_only, out nint handle)) {\n"
        "                ptr_this->handle_vbf = null;\n"
        "                ptr_this->handle_os = new HANDLE((void*)handle);\n"
        "                return ptr_this;\n            }\n"
        "            if (!_index.TryGetValue(path_str, out string? path_modded)) {"
    )],
}
PATCHED = tuple(PATCHES)


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("Expected exactly one pinned patch context: " + old[:90])
    return text.replace(old, new, 1)


def load_source_verifier():
    spec = importlib.util.spec_from_file_location(
        "fahrenheit_reference", HERE.parent / "verify_upstream.py"
    )
    if spec is None or spec.loader is None:
        raise ValueError("Cannot load the Fahrenheit source verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def recover_upstream_bytes(name: str, data: bytes) -> bytes:
    if name not in PATCHES:
        return data
    text = data.decode("utf-8")
    for old, new in reversed(PATCHES[name]):
        text = replace_once(text, new, old)
    return text.encode("utf-8")


def verify_overlay(destination: Path) -> None:
    reference = json.loads(REFERENCE.read_text())
    marker = json.loads((destination / MARKER).read_text())
    core = (HERE / "FhCooperativeServices.cs").read_bytes()
    expected = {"protocol": 2, "revision": reference["revision"],
                "core_sha256": hashlib.sha256(core).hexdigest(), "patched": list(PATCHED)}
    if marker != expected or b"\0" in core:
        raise ValueError("Cooperative provider identity/source is invalid")
    if (destination / ADDED).read_bytes() != core:
        raise ValueError("Cooperative service source differs from the reviewed overlay")
    manifest = load_source_verifier().verify_manifest(
        destination,
        recover_bytes=recover_upstream_bytes,
        required_extra_files={MARKER, ADDED},
    )
    if manifest.revision != reference["revision"]:
        raise ValueError("Provider marker revision differs from the complete manifest")


def create_overlay(source: Path, destination: Path) -> None:
    source, destination = source.resolve(), destination.absolute()
    if destination.exists():
        raise FileExistsError(destination)
    if source == destination or source in destination.parents:
        raise ValueError("The provider copy must be outside the original checkout")
    spec = importlib.util.spec_from_file_location("fahrenheit_reference", HERE.parent / "verify_upstream.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.verify(source)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git", "artifacts", "bin", "obj", "__pycache__"))
    for name, replacements in PATCHES.items():
        path = destination / name
        text = path.read_bytes().decode("utf-8")
        for old, new in replacements:
            text = replace_once(text, old, new)
        path.write_bytes(text.encode("utf-8"))
    core = (HERE / "FhCooperativeServices.cs").read_bytes()
    (destination / ADDED).write_bytes(core)
    reference = json.loads(REFERENCE.read_text())
    (destination / MARKER).write_text(json.dumps({"protocol": 2, "revision": reference["revision"],
        "core_sha256": hashlib.sha256(core).hexdigest(), "patched": list(PATCHED)}, indent=2) + "\n")
    verify_overlay(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path, nargs="?")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    try:
        if args.verify:
            verify_overlay(args.source)
        elif args.destination:
            create_overlay(args.source, args.destination)
        else:
            parser.error("provide a new destination or --verify")
        print("Fahrenheit cooperative provider: PASS (protocol 2, pinned and reversible overlay)")
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + "\n")
