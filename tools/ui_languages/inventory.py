#!/usr/bin/env python3
"""Inventory immutable F8 declarations; this is not a Windows build test."""
from __future__ import annotations
import argparse,json,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
HOOKS=ROOT/"src/runtime/FfxHooksDll/hooks"
def flag_inventory():
    source=(HOOKS/"F8FlagCatalog.cpp").read_text(encoding="utf-8")
    declarations=source[:source.index("#undef VANGUARD_GATE")].replace("#include <windows.h>","")
    program='#include <cstdio>\n'+declarations+'\n}\n}\n'
    program+=r'int main(){for(const auto& f:FfxHooks::kFlags)std::printf("%s\t%s\t%s\n",f.gate.canonicalKey,f.label,f.help);}'
    with tempfile.TemporaryDirectory(prefix="ffx-ui-inventory-") as directory:
        cpp=Path(directory)/"inventory.cpp";exe=Path(directory)/"inventory"
        cpp.write_text(program,encoding="utf-8")
        subprocess.run(["g++","-std=c++17","-I",str(HOOKS),str(cpp),"-o",str(exe)],check=True)
        output=subprocess.check_output([str(exe)],text=True,encoding="utf-8")
    return [dict(zip(("key","label","help"),line.split("\t"))) for line in output.splitlines()]
def ui_inventory():
    result=set()
    for name in ("NativeSettingsUi.inl","NativeRewardSettings.inl"):
        source=(HOOKS/name).read_text(encoding="utf-8")
        for literal in re.findall(r'"(?:[^"\\]|\\.)*"',source):
            try:value=json.loads(literal)
            except ValueError:continue
            if re.search("[A-Za-z]{2}",value) and not re.fullmatch("[a-zA-Z0-9_.]+",value) and "\n" not in value:result.add(value)
    return sorted(result)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--output",type=Path,required=True);args=parser.parse_args()
    flags=flag_inventory();strings=ui_inventory()
    args.output.write_text(json.dumps({"flags":flags,"strings":strings},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(f"F8 inventory: {len(flags)} flags, {len(strings)} UI strings -> {args.output}")
if __name__=="__main__":main()
