#!/usr/bin/env python3
"""Prove selected regressions detect real defects, not just missing modules.

Each defect is inserted into a new temporary copy of this research package.
The original source and all game files remain unchanged. A syntax/import error
is not accepted as a detected behavioral defect.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent
MUTATIONS=(
    ('stale_contents_header','codec.py','ch[1] = len(grid.nodes)',
     'ch[1] = grid.contents_header[1]',
     'test_codec.CodecTests.test_append_updates_both_header_counts_and_payload'),
    ('color_floats_mistaken_for_bytes','memory_contract.py',"('colors',64)","('colors',16)",
     'test_memory.ProtocolTests.test_measured_stock_units'),
    ('uv_floats_mistaken_for_bytes','memory_contract.py',"('uv',32)","('uv',8)",
     'test_memory.ProtocolTests.test_measured_stock_units'),
    ('one_byte_overrun_allowed','memory_contract.py','size > handle.length-offset:',
     'size > handle.length-offset+1:',
     'test_memory.MemoryTests.test_one_byte_overrun_is_detected_before_recording_a_store'),
    ('released_buffer_still_live','memory_contract.py','if not record.alive:',
     'if False:',
     'test_memory.MemoryTests.test_write_after_free_is_rejected'),
    ('old_callback_generation_accepted','memory_contract.py','if handle.generation != generation:',
     'if False:',
     'test_memory.MemoryTests.test_stale_generation_is_rejected_for_address_only_callback'),
    ('append_boundary_decoded_as_fixed','memory_contract.py','if command < self.quads:',
     'if command <= self.quads:',
     'test_memory.ProtocolTests.test_roundtrip_all_commands_at_every_scale'),
    ('embedded_node_limit_raised_without_layout','codec.py','Limits(1024, 1024, 128, 130)',
     'Limits(1025, 1024, 128, 130)',
     'test_codec.CodecTests.test_fixed_native_node_array_limit_rejects_1025'),
    ('wrong_fault_counted_as_expected','native_harness.py',
     "require(error.evidence.get('kind')==kind,f'expected {kind}, got {error.evidence}')",
     "require(True,'wrong error accepted')",
     'test_harness_gate.HarnessGateTests.test_unrelated_emulator_fault_is_not_counted_as_expected_overrun'),
    ('signature_mismatch_ignored','emulator_patches.py',
     'end > len(image) or image[patch.rva:end] != patch.expected',
     'end > len(image)',
     'test_patches.PatchTests.test_corrupt_each_instruction_refuses_entire_plan'),
)


def run() -> dict:
    results=[]
    for name,filename,before,after,test in MUTATIONS:
        with tempfile.TemporaryDirectory(prefix='sphere-grid-mutant-') as directory:
            destination=Path(directory)/'lab'
            shutil.copytree(ROOT,destination,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
            target=destination/filename
            content=target.read_text()
            if content.count(before)!=1:
                raise RuntimeError(f'{name}: mutation site must occur exactly once')
            target.write_text(content.replace(before,after))
            environment=os.environ.copy();environment['PYTHONDONTWRITEBYTECODE']='1'
            completed=subprocess.run([sys.executable,'-m','unittest',test,'-v'],
                cwd=destination/'tests',env=environment,capture_output=True,text=True,timeout=30)
            evidence=completed.stdout+completed.stderr
            detected=(completed.returncode!=0 and 'FAIL:' in evidence and
                      'failures=' in evidence and 'errors=' not in evidence and
                      'SyntaxError' not in evidence and 'ModuleNotFoundError' not in evidence)
            results.append({'mutation':name,'test':test,'behavioral_failure_observed':detected,
                            'exit_code':completed.returncode,'evidence_tail':evidence[-2200:]})
    return {'scope':'temporary research copies only','mutations':len(results),
            'detected':sum(r['behavioral_failure_observed'] for r in results),'results':results}


if __name__=='__main__':
    result=run();print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['detected']==result['mutations'] else 1)
