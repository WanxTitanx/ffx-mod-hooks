#!/usr/bin/env python3
"""Jarvis-HOOK: private native font parsing/pixel regressions; never starts FFX."""
from pathlib import Path
import argparse
import hashlib
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
DLL=ROOT/'src/runtime/FfxHooksDll'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--sanitize',action='store_true')
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    fixture=args.fixture;original=(fixture/'native-fonts.vbf').read_bytes()
    (fixture/'truncated.vbf').write_bytes(original[:12])
    bad=bytearray(original);header,count=struct.unpack_from('<IQ',bad,4)
    struct.pack_into('<Q',bad,16+count*16+8,1<<40)
    bad[-16:]=hashlib.md5(bad[:header]).digest();(fixture/'bad-extent.vbf').write_bytes(bad)
    bad=bytearray(original);bad[header+8]^=0x80;(fixture/'bad-block.vbf').write_bytes(bad)
    exe=args.out/'UiNativeFontRt0'
    flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',*flags,
        str(DLL/'hooks/UiNativeFont.cpp'),str(DLL/'tests/UiNativeFontRt0.cpp'),'-lcrypto','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(fixture/'native-fonts.vbf'),str(fixture)],check=True)


if __name__=='__main__':main()
