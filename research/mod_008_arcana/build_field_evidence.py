#!/usr/bin/env python3
"""Jarvis-HOOK: pin clean-room field-message API evidence from both private PEs.

This reads executable bytes and existing IDA correspondence records. It never
starts FFX, modifies an executable/database, or copies decompiled implementations.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import pefile

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    "legacy": "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced",
    "steam": "0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d",
}
ENTRIES = {
    "Sync": "FFX_Field_SelectControlledActor",
    "GetEvent": "FFX_Field_GetEventStruct",
    "InitSlot": "FFX_MenuText_InitItemSlot",
    "SetColors": "FFX_Menu_ListItemSetMargins",
    "SetStyle": "FFX_Menu_SetListItemPosAndSize",
    "SetText": "FFX_MenuText_InitItemStrings",
    "Layout": "FFX_Menu_ComputeListItemLayout",
    "LayoutFixed": "FFX_Menu_CalcListItemGeometry",
    "SetWidth": "FFX_Menu_SetListItemWidth",
    "SelectSlot": "FFX_Menu_ClearAllItemSlots",
    "Open": "FFX_Menu_ListItemActivateAndSkip",
    "Close": "FFX_Menu_SetListItemSelect",
    "TextState": "FFX_MenuText_GetItemKind",
    "Template": "Menu_LocalizedStringLookup",
    "ParamKind": "BtlUI_GetBtlFieldByte",
    "ParamValue": "BtlUI_GetBtlFieldDword",
    "RefreshControl": "FFX_Field_ClearAndSelectActor",
    "InitTextBank": "FFX_MenuText_InitPointerTables",
    "InitFrameBank": "FFX_Menu_ListItemStyleInit",
    "InitSlotBank": "FFX_MenuText_SwitchBufferBank",
    "NormalizeInput": "FFX_Field_IsEmptyCellFromPos",
    "Project": "FFX_Render_Loose_642A60",
    "ProjectPoint": "FFX_Render_WorldToScreen",
    "Viewport": "FFX_Menu2D_GetNativeViewportSize1920x1080",
}
DATA = {
    "ContextPointer": 0x1326B28,
    "Contexts": 0x1325BA0,
    "Bank": 0x1326B82,
    "Events": 0x1326D70,
    "Managers": 0x1865B3C,
    "Windows": 0x18676F0,
    "Responses": 0x1868A90,
    "Frames": 0x1876DA8,
    "Parameters": 0x1869AB0,
    "WindowPrimary": 0x18663F0,
    "ResponsePrimary": 0x1867710,
    "FramePrimary": 0x18765E8,
    "ParameterPrimary": 0x1868AB0,
    "ManagerPrimary": 0x1865CE0,
    "Held": 0x13270C0,
    "Pressed": 0x13270D0,
    "InputMap": 0x1330488,
    "ProjectionOwner": 0xCCB9D8,
}


def read_pe(path, label):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED[label]:
        raise ValueError("Unsupported private " + label + " executable")
    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        raise ValueError("Expected the exact x86 preferred image")
    relocs = {r.rva for block in pe.DIRECTORY_ENTRY_BASERELOC for r in block.entries if r.type == 3}
    return pe, relocs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--steam", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    pes = {"legacy": read_pe(args.legacy, "legacy"), "steam": read_pe(args.steam, "steam")}
    records = {}
    for path in sorted([*args.evidence.glob("field-message*.json"), *args.evidence.glob("projection-*.json")]):
        text = path.read_text(encoding="utf-8-sig")
        document = json.loads(text[text.index("{"):])
        if document["sha256"] != EXPECTED["steam"]:
            raise ValueError("Mismatched IDA input identity")
        for row in document.get("functions", []):
            if row["c"] != "None":
                records[row["name"]] = row
    pairs = {}
    for name, row in records.items():
        old = re.search(r"Legacy VA (0x[0-9A-Fa-f]+)", row["c"])
        length = re.search(r"Complete (\d+)-byte normalized", row["c"])
        if old and length:
            pairs[name] = (int(old[1], 16)-0x400000, int(row["va"], 16)-0x400000, int(length[1]))
    data_pairs = {}
    witnesses = {}
    for key, value in DATA.items():
        found = []
        for name, (old_rva, new_rva, size) in pairs.items():
            old_bytes = pes["legacy"][0].get_data(old_rva, size)
            new_bytes = pes["steam"][0].get_data(new_rva, size)
            for rva in sorted(pes["steam"][1]):
                offset = rva-new_rva
                if not 2 <= offset <= size-4 or old_rva+offset not in pes["legacy"][1]:
                    continue
                if int.from_bytes(new_bytes[offset:offset+4], "little") != value:
                    continue
                if new_bytes[offset-2:offset] != old_bytes[offset-2:offset]:
                    continue
                previous = int.from_bytes(old_bytes[offset:offset+4], "little")
                found.append((previous, name, offset))
        if not found or len({x[0] for x in found}) != 1:
            raise ValueError("No unambiguous relocated operand for " + key)
        data_pairs[key] = (found[0][0]-0x400000, value-0x400000)
        witnesses[key] = [{"function": n, "operand_offset": off} for _, n, off in found]

    lines = ["// Generated by build_field_evidence.py from the two pinned private executables.",
             "#pragma once", '#include "EquipmentWorkshopEvidence.h"',
             "namespace FfxHooks::Arcana::FieldPresentation::Evidence {",
             "enum class Entry {" + ",".join(ENTRIES) + ",Count};"]
    report = {"identity": EXPECTED, "address_form": "RVA", "architecture": "x86", "data_witnesses": witnesses, "profiles": {}}
    for label in ("steam", "legacy"):
        index = 1 if label == "steam" else 0
        pe, relocs = pes[label]
        lines.append("#ifdef FFXHOOKS_TARGET_STEAM_20261001" if label == "steam" else "#else")
        targets = []
        spans = []
        for key, name in ENTRIES.items():
            rva = pairs[name][index]
            targets.append(rva)
            for prefix in (0, 4, 8, 12, 16):
                start = rva-prefix
                offsets = sorted(x-start for x in relocs if start <= x < start+32)
                overlaps = any(start-3 <= x < start for x in relocs)
                if len(offsets) <= 4 and all(x <= 28 for x in offsets) and not overlaps:
                    break
            else:
                raise ValueError("No complete relocation window for " + name)
            raw = pe.get_data(start, 32)
            if len(raw) != 32:
                raise ValueError("Truncated entry evidence")
            spans.append((start, raw, offsets + [255]*(4-len(offsets)), key))
        lines.append("inline constexpr std::uint32_t entries[]={" + ",".join(f"0x{x:08X}u" for x in targets) + "};")
        for key, pair in data_pairs.items():
            lines.append(f"inline constexpr std::uint32_t k{key}=0x{pair[index]:08X}u;")
        lines.append("inline constexpr EquipmentWorkshop::Evidence::Span spans[]={")
        for rva, raw, offsets, key in spans:
            lines.append(" {0x%08Xu,{%s},{%s}}, // %s" % (rva, ",".join(f"0x{x:02X}" for x in raw), ",".join(map(str, offsets)), key))
        lines.append("};")
        report["profiles"][label] = {"entries": dict(zip(ENTRIES, map(hex, targets))),
            "data": {key: hex(pair[index]) for key, pair in data_pairs.items()},
            "spans": [{"key": key, "rva": hex(rva), "bytes": 32, "sha256": hashlib.sha256(raw).hexdigest()} for rva, raw, _, key in spans]}
    lines += ["#endif", "inline constexpr std::uint32_t Rva(Entry entry) noexcept {return entries[static_cast<unsigned>(entry)];}", "}", ""]
    destination = ROOT / "src/runtime/FfxHooksDll/hooks/ArcanaFieldEvidence.generated.h"
    destination.write_text("\n".join(lines))
    (args.evidence / "field-native-evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(ENTRIES)} native entry witnesses and {len(DATA)} data bindings per profile")


if __name__ == "__main__":
    main()
