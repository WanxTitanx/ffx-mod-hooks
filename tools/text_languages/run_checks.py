#!/usr/bin/env python3
"""Offline MOD-006 checks. Optional private fixtures never leave the selected machine."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'src/runtime/FfxHooksDll'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pack', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    if bool(args.pack) != bool(args.reference):
        parser.error('--pack and --reference must be supplied together')
    if os.name == 'nt':
        parser.error('Use text_languages_rt1.ps1 for the Windows/MSVC suite')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(out).free < 256 * 1024 * 1024:
        parser.error('Insufficient build space')
    results = []
    def run(name, command, **kwargs):
        completed = subprocess.run(command, cwd=ROOT, text=True, errors='replace',
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
        (out / (name + '.log')).write_text(completed.stdout, encoding='utf-8')
        print(completed.stdout, end='', flush=True)
        results.append(dict(name=name, command=list(map(str, command)), exit=completed.returncode))
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        if completed.returncode:
            raise RuntimeError(f'{name} failed: {out / (name + ".log")}')
    run('python', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tools/text_languages/tests', '-p', 'test_*.py', '-v'])
    common = [str(HERE / 'hooks/TextLanguageCore.cpp')]
    payload = common + [str(HERE / 'hooks/TextLanguagePayload.cpp')]
    targets = [('Core', common), ('Payload', payload), ('Field', payload)]
    for name, sources in targets:
        modes = [('release', [])]
        if args.sanitizers:
            modes.append(('sanitized', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']))
        for mode, flags in modes:
            executable = out / f'{name}-{mode}'
            run(f'{name}-{mode}-build', ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', *flags,
                *sources, str(HERE / f'tests/TextLanguage{name}Rt0.cpp'), '-o', str(executable)])
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1')
            run(f'{name}-{mode}', [str(executable)], env=env)
    pack_sources = payload + [str(HERE / 'hooks/TextLanguagePack.cpp')]
    for test, filename in [('Validate', 'TextLanguageValidate'), ('Pack', 'TextLanguagePackRt0')]:
        executable = out / filename
        run(test + '-build', ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', *pack_sources,
                             str(HERE / f'tests/{filename}.cpp'), '-lcrypto', '-o', str(executable)])
        if args.pack:
            run(test, [str(executable), str(args.pack.resolve()), str(args.reference.resolve())])
    if args.pack:
        run('Received-pack', [sys.executable, str(ROOT / 'tools/text_languages/check_received_pack.py'),
            '--validator', str(out / 'TextLanguageValidate'), '--pack', str(args.pack.resolve()),
            '--reference', str(args.reference.resolve()), '--output', str(out / 'received-pack')])
    print('PRIVATE_PACK=' + ('VERIFIED_RT0' if args.pack else 'NOT_SUPPLIED'))
    identity = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in list((HERE / 'hooks').glob('TextLanguage*')) + list((HERE / 'tests').glob('TextLanguage*'))
                + list((ROOT / 'tools/text_languages').glob('*.py'))}
    (out / 'inputs.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        raise SystemExit(str(error))
