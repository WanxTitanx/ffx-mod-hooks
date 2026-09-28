#!/usr/bin/env python3
"""Verify the Arcana research/art delivery; never load or modify game files."""

import argparse
import ast
import hashlib
import json
import re
import subprocess
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ART = ROOT / "assets/mod-008-arcana"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--local-evidence", action="store_true",
                        help="Also rehash recorded external sources, image origins and the PE")
    args = parser.parse_args()
    catalog_path = HERE / "cards.proposed.json"
    catalog = json.loads(catalog_path.read_text())
    cards = catalog["cards"]
    manifest = json.loads((HERE / "assets-manifest.json").read_text())
    rows = manifest["assets"]
    require(catalog["runtime_compatible"] is False, "Design catalog must not claim runtime support")
    require(len(cards) == 78 and len(rows) == 81 and not manifest["missing"], "Incomplete deck")
    require(len({r["role"] for r in rows}) == len({r["sha256"] for r in rows}) == 81,
            "Duplicate asset role or image")
    by_role = {r["role"]: r for r in rows}
    for row in rows:
        path = (ROOT / row["path"]).resolve()
        require(path.is_relative_to(ART.resolve()), "Asset outside the art directory")
        require(digest(path) == row["sha256"] and path.stat().st_size == row["bytes"],
                f"Asset drift: {row['path']}")
        with Image.open(path) as image:
            require((image.width, image.height, image.mode) ==
                    (row["width"], row["height"], row["mode"]), "Image metadata drift")
            image.verify()
    for card in cards:
        row = by_role[card["key"]]
        require(row["path"] == card["asset"], "Catalog/manifest selection differs")
        require((row["width"], row["height"]) == (1024, 1536), "Unexpected card dimensions")
        receipt = json.loads((HERE / "generation-receipts" / f"{card['id']:03d}.json").read_text())
        selected = (receipt.get("revisions") or [receipt])[-1]
        require(selected["asset"] == card["asset"], "Receipt selects a different revision")
        if "sha256" in selected:
            require(selected["sha256"] == row["sha256"], "Revision receipt hash differs")
        if args.local_evidence:
            require(digest(Path(selected["source"])) == row["sha256"], "Generation source drift")

    contract = json.loads((HERE / "contract-validation.json").read_text())
    require(contract["catalog_sha256"] == digest(catalog_path), "Stale contract report")
    require(contract["result"] == "PASS_RT0_MODEL_ONLY", "Missing passing model report")
    page = (ART / "gallery.html").read_text()
    script = re.search(r"<script>(.*?)</script>", page, re.S).group(1)
    data = json.loads(re.search(r"const cards=(.*?);let filter=", script, re.S).group(1))
    require(len(data) == len(cards), "Gallery count differs")
    for entry, card in zip(data, cards):
        require((entry["id"], entry["name"], entry["effect"]) ==
                (card["id"], card["item_name_en"], card["mechanics_proposed"]), "Gallery data drift")
        require((ART / entry["image"]).resolve() == (ROOT / card["asset"]).resolve(),
                "Gallery image differs from selected concept")
    subprocess.run(["node", "--check"], input=script, text=True, check=True)
    python_paths = list(HERE.glob("*.py")) + [
        ROOT / "research/mod_ideas_precode/generate_ledger.py",
        ROOT / "research/mod_ideas_precode/run_offline_suite.py",
    ]
    for path in python_paths:
        ast.parse(path.read_text(), filename=str(path))

    documents = list((ROOT / "docs/mod-ideas").glob("MOD 008*"))
    documents += list((ROOT / "docs/research").glob("MOD_008*"))
    documents += [HERE / "README.md", ROOT / "docs/MOD_IDEAS_BACKLOG.md"]
    # The concept worktree also carried an unrelated MOD-006 Editor handoff.
    # Actual links in the selected Arcana documents are still checked below.
    links = 0
    for path in documents:
        for target in re.findall(r"\]\(([^)]+)\)", path.read_text()):
            target = target.strip("<>").split("#", 1)[0]
            if not target or re.match(r"^[a-z]+:", target):
                continue
            require((path.parent / target).exists(), f"Broken link in {path.name}: {target}")
            links += 1

    source_count = 0
    if args.local_evidence:
        inventory = json.loads((HERE / "source-inventory.json").read_text())
        for row in inventory["files"]:
            require(digest(Path(row["path"])) == row["sha256"], "Research source drift")
            source_count += 1
        fandom = json.loads((HERE / "fandom-api-receipt.json").read_text())
        require(digest(Path(fandom["path"])) == fandom["sha256"], "Fandom snapshot drift")
        pe = json.loads((HERE / "pe-evidence.json").read_text())
        require(digest(Path(pe["exe_path"])) == pe["exe_sha256"], "Executable identity drift")

    print(json.dumps({
        "result": "PASS_RT0_DELIVERY", "cards": len(cards), "assets": len(rows),
        "selected_generation_receipts": len(cards), "local_links": links,
        "python_syntax_files": len(python_paths), "gallery_javascript_syntax": True,
        "catalog_sha256": digest(catalog_path), "manifest_sha256": digest(HERE / "assets-manifest.json"),
        "local_evidence_requested": args.local_evidence, "research_sources_rehashed": source_count,
        "browser_executed": False, "native_functions_executed": False,
        "limits": ["Visual art review is separate", "No browser navigation test",
                   "Art/catalog validation only; native evidence is in runtime-validation.json", "No RT2"],
    }, indent=2))


if __name__ == "__main__":
    main()
