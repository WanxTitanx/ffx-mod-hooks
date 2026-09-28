#!/usr/bin/env python3
"""Exercise the compiled recipient preflight against private package copies.

Jarvis-HOOK. No game is launched and the supplied package/reference stay unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(validator: Path, package: Path, reference: Path, output: Path) -> int:
    validator = validator.resolve(strict=True)
    package = package.resolve(strict=True)
    reference = reference.resolve(strict=True)
    output = output.resolve()
    if output == package or package in output.parents or output == reference or reference in output.parents:
        raise ValueError('Keep test output outside the package and reference')
    output.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name: str, path: Path, admitted: bool) -> bool:
        process = subprocess.run([str(validator), str(path), str(reference)],
                                 capture_output=True, text=True, errors='replace', timeout=30)
        text = process.stdout + process.stderr
        passed = (process.returncode == (0 if admitted else 1)
                  and ('ADMITTED RT0' in text if admitted else 'REJECTED:' in text))
        results.append(dict(name=name, passed=passed, exit=process.returncode, output=text))
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)
        return passed

    with tempfile.TemporaryDirectory(prefix='received-pack-', dir=output) as temporary:
        root = Path(temporary)
        valid = root / 'valid'
        shutil.copytree(package, valid)
        if not check('valid received package', valid, True):
            # Only a semantically admitted manifest can select paths for the
            # deliberate negative-fixture mutations below.
            raise ValueError('Package rejected before negative-fixture preparation')
        manifest = json.loads((valid / 'manifest.json').read_text(encoding='utf-8'))
        for name in ('missing-resource', 'changed-resource', 'wrong-executable',
                     'unsupported-version', 'unsupported-locale', 'unsafe-resource-path', 'voice-resource'):
            candidate = root / name
            shutil.copytree(valid, candidate)
            data = json.loads(json.dumps(manifest))
            resource = data['resources'][0]
            leaf = candidate / resource['path']
            if name == 'missing-resource':
                leaf.unlink()
            elif name == 'changed-resource':
                content = bytearray(leaf.read_bytes())
                content[-1] ^= 1
                leaf.write_bytes(content)
            elif name == 'wrong-executable':
                data['executable_sha256'] = 'a' * 64
            elif name == 'unsupported-version':
                data['hook_api'] = 999
            elif name == 'unsupported-locale':
                data['locale'] = 'es-MX'
            elif name == 'unsafe-resource-path':
                resource['path'] = '../escape.bin'
            elif name == 'voice-resource':
                resource['request'] = '/FFX_Data/Sound/Voice/JP/voice.fsb'
            (candidate / 'manifest.json').write_text(json.dumps(data), encoding='utf-8')
            check(name, candidate, False)

        # canonical() used to erase this redirect before validation, so recipient
        # preflight could accept a directory which the runtime refuses to pin.
        alias = root / 'package-alias'
        if os.name == 'nt':
            process = subprocess.run(['cmd', '/d', '/c', 'mklink', '/J', str(alias), str(valid)],
                                     capture_output=True, text=True, errors='replace', timeout=30)
            if process.returncode:
                raise RuntimeError('Cannot create the isolated junction fixture: ' + process.stdout + process.stderr)
        else:
            alias.symlink_to(valid, target_is_directory=True)
        try:
            check('redirected package root', alias, False)
        finally:
            if os.name == 'nt':
                os.rmdir(alias)  # Remove only the owned junction, never its target.
            else:
                alias.unlink()
        check('rejected copies preserve valid package', valid, True)

    report = dict(validator_sha256=hashlib.sha256(validator.read_bytes()).hexdigest(),
                  checks=len(results), failures=sum(not row['passed'] for row in results), results=results)
    (output / 'received-pack-results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Received package preflight: {report['checks']} checks, {report['failures']} failures", flush=True)
    return 1 if report['failures'] else 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(run(args.validator, args.pack, args.reference, args.output))
