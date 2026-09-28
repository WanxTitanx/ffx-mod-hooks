#!/usr/bin/env python3
"""Pin native Equip signatures from the supported PE; never modify the executable."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile

EXPECTED = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TARGETS = {"controller": 0x4CEFF0, "draw": 0x4CF640, "enter": 0x4CF800,
           "leave": 0x4CF8E0, "cursor": 0x4CF960, "scale_y": 0x2449D0, "scale_x": 0x244990,
           "status_overview": 0x4D26E0}
SUPPORT = {"root_state": 0x4CF35F, "native_category_toggle": 0x4CF444,
           # Include each entry plus preceding padding so no HIGHLOW relocation
           # is cut by the fixed 32-byte signature boundary.
           "read_direction_entry_window": 0x4BE438, "read_edge_entry_window": 0x4BE478,
           "current_actor": 0x4A9810, "menu_sfx": 0x486B00,
           "portrait_transition": 0x4BF720, "portrait_transition_arguments": 0x4CF014,
           "native_ability_font": 0x505AB0, "native_font_measure": 0x505290,
           "native_caption": 0x4F8D50, "status_animation": 0x4D3090}
STATUS = {"status_draw": 0x4D2760, "status_list": 0x4D2DE0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pe", type=Path, required=True)
    args = parser.parse_args()
    raw = args.pe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED:
        raise SystemExit("Unsupported executable identity")
    pe = pefile.PE(data=raw)
    relocations = {entry.rva for block in pe.DIRECTORY_ENTRY_BASERELOC
                   for entry in block.entries if entry.type == 3}
    lines = ["// Generated from the supported PE by build_native_evidence.py.", "#pragma once",
             '#include "EquipmentWorkshopEvidence.h"', "namespace FfxHooks::Arcana::Evidence {"]
    rows = []
    for group, definitions in (("spans", {**TARGETS, **SUPPORT}), ("statusSpans", STATUS)):
        lines.append("inline constexpr EquipmentWorkshop::Evidence::Span " + group + "[]={")
        for name, rva in definitions.items():
            data = pe.get_data(rva, 32)
            offsets = sorted(x-rva for x in relocations if rva <= x < rva+32)
            assert len(offsets) <= 4 and all(x <= 28 for x in offsets)
            offsets += [255]*(4-len(offsets))
            lines.append("    {0x%Xu,{%s},{%s}}, // %s" %
                         (rva, ",".join("0x%02X" % b for b in data), ",".join(map(str, offsets)), name))
            rows.append({"name": name, "rva": hex(rva), "width": 32, "relocation_offsets": offsets,
                         "sha256": hashlib.sha256(data).hexdigest(), "hooked": name in TARGETS or name == "status_draw",
                         "group": group})
        lines.append("};")
    lines += ["}", ""]
    (ROOT / "src/runtime/FfxHooksDll/hooks/ArcanaEvidence.generated.h").write_text("\n".join(lines))
    (HERE / "native-ui-evidence.json").write_text(json.dumps({
        "pe_sha256": EXPECTED, "address_form": "RVA, x86 32-bit", "preferred_base": "0x400000",
        "context_state_offset": "0x1c", "root_idle_state": 10, "native_category_rva": "0x146a5e4",
        "native_category_allowed_values": [0, 1], "spans": rows,
        "portrait_transition_fields_rva": ["0x1fcc3c8", "0x1fcc3c4", "0x1fcc3c0", "0x1fcc3bc"],
        "portrait_transition_field_width": 4,
        "status_context_fields": {"retiring_byte": "0x41", "mode": "0x58", "category": "0x5c", "page": "0x60", "aeon": "0x64"},
        "status_native_capacity": 22,
        "menu_feedback": {"dispatcher_rva": "0x486b00", "move_confirm": 1, "error": 3, "cancel": 4,
                          "ownership": "One cue after consumed private input; delegated native controls own their audio."},
        "status_typography": {"ability_font_rva": "0x505ab0", "measure_rva": "0x505290",
                              "caption_rva": "0x4f8d50", "caption_id_context_offset": "0x68",
                              "native_caption_size": [430, 36], "outline": "native", "width_fit": "uniform per section"},
        "status_overview": {"draw_rva": "0x4d26e0", "animation_phase_offset": "0x52", "phase_width": 2,
                            "animation_rva": "0x4d3090", "row_y": 490, "row_height": 44,
                            "armor_bottom": 482, "stats_top": 552, "slots": "2 or 3 in one read-only row"},
        "status_visibility": {"cover_step_rva": "0x146a9bc", "cover_amount_rva": "0x146a9c0",
                              "cover_field_width": 2, "cover_range": [0, 4096],
                              "cover_set_step_rva": "0x4d2630", "cover_advance_rva": "0x4d3250",
                              "cover_get_rva": "0x4d48e0", "cover_reset_rva": "0x4d4ea0",
                              "retire_page_rva": "0x4d2650", "cover_draw_rva": "0x4d4140",
                              "rule": "Overview additions require a live page, zero cover and nonpositive cover step; ability additions require a live page."},
        "equip_cursor_order": "Suppress the earlier native Tarot cursor and draw it once after opaque extension rows; native Weapon/Armor delegation is unchanged.",
        "status_extension": "Private direct-draw rows; existing native ability buffer remains unchanged. Borrow the validated Workshop status bridge when installed.",
        "ability_row_scale_returns": {"icon_width": "0x4f4f66", "icon_height": "0x4f4f54", "icon_y": "0x4f4f78", "text_y": "0x4f4fc8"},
        "header_scale_y_returns": {"equipment_panel": "0x4cf6c3", "equipment_caption": "0x4cf712",
                                   "abilities_panel": "0x4cf766", "abilities_caption": "0x4cf7b5"},
        "scope": "RT0 static bytes and caller/context analysis; not an in-game observation",
    }, indent=2)+"\n")
    print(f"PASS Arcana native UI evidence: {len(TARGETS)} UI targets, {len(SUPPORT)} supporting spans, {len(STATUS)} conditional Status spans; executable unchanged")


if __name__ == "__main__":
    main()
