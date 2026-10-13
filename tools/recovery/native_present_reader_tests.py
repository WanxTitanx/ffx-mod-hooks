#!/usr/bin/env python3
"""Emit an MSVC x86 harness for the real native swapchain reader and Win32 guards.

Only private VirtualAlloc pages and a test vtable are used. No game or D3D device
is started. Run the emitted program for both admitted executable profiles.
"""
import argparse
from pathlib import Path
from present_startup_tests import function_block

PREFIX = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <cstdint>
#include <cstdio>
#include "ffx_addresses.h"
static std::uintptr_t g_base=0;
static_assert(sizeof(void*)==4,"native swapchain field is a DWORD");
static_assert(RVA_FFX_RENDERER_SWAPCHAIN==0x894F74,"both exact profiles share this field");
static void NativePresent() {}
'''

TEST = r'''
static unsigned checks=0,failures=0;
static void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
int main(){
    void** table=nullptr;void* present=nullptr;
    Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"absent module is rejected");
    const auto bytes=static_cast<SIZE_T>(RVA_FFX_RENDERER_SWAPCHAIN+0x1000);
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;g_base=reinterpret_cast<std::uintptr_t>(image);
    auto** field=reinterpret_cast<void**>(image+RVA_FFX_RENDERER_SWAPCHAIN);
    Check(!AuroraD3DReadNativeRendererPresentSlot(nullptr,&present),"null table output is rejected");
    Check(!AuroraD3DReadNativeRendererPresentSlot(&table,nullptr),"null entry output is rejected");
    Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present)&&!table&&!present,"unpublished swapchain remains pending");
    void* entries[16]{};entries[8]=reinterpret_cast<void*>(&NativePresent);
    struct Swap {void** vtable;};Swap swap{entries};*field=&swap;
    Check(AuroraD3DReadNativeRendererPresentSlot(&table,&present)&&table==entries&&present==entries[8],"real reader selects the DWORD object and vtable slot eight");
    Check(AuroraD3DCommittedExecutable(present),"actual compiled executable entry is admitted by VirtualQuery");
    entries[8]=nullptr;Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present)&&!table&&!present,"empty slot clears outputs");
    swap.vtable=nullptr;Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"empty vtable remains pending");
    *field=reinterpret_cast<void*>(1);Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present)&&!table&&!present,"bad object pointer is contained by real x86 SEH");
    void* denied=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);if(!denied)return 2;
    *field=denied;Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"inaccessible object is contained by SEH");
    *field=&swap;swap.vtable=static_cast<void**>(denied);Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"inaccessible vtable is contained by SEH");
    Check(!AuroraD3DCommittedExecutable(image),"writable non-code pages are rejected");
    Check(!AuroraD3DCommittedExecutable(nullptr)&&!AuroraD3DCommittedExecutable(reinterpret_cast<void*>(1)),"null and uncommitted entry pointers are rejected");
    void* guarded=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READ|PAGE_GUARD);if(!guarded)return 2;
    Check(!AuroraD3DCommittedExecutable(guarded),"guarded executable pages are rejected without accessing them");
    swap.vtable=entries;entries[8]=image;*field=&swap;
    Check(AuroraD3DReadNativeRendererPresentSlot(&table,&present)&&!AuroraD3DCommittedExecutable(present),"the actual reader plus code-page gate rejects a data-only slot");
    DWORD prior=0,ignored=0;VirtualProtect(field,4,PAGE_NOACCESS,&prior);
    Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"an inaccessible renderer field is contained by SEH");
    VirtualProtect(field,4,prior,&ignored);VirtualFree(image,0,MEM_RELEASE);
    Check(!AuroraD3DReadNativeRendererPresentSlot(&table,&present),"an unmapped module field is contained by SEH");
    g_base=0;VirtualFree(denied,0,MEM_RELEASE);VirtualFree(guarded,0,MEM_RELEASE);
    std::printf("NativePresentReaderRt1: %u checks, %u failures; private Win32 pages, no game/device\n",checks,failures);
    return failures?1:0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-cpp', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/runtime/FfxHooksDll/dllmain.cpp').read_text()
    code = PREFIX
    for signature in ('static bool AuroraD3DReadNativeRendererPresentSlot(',
                      'static bool AuroraD3DCommittedExecutable('):
        code += function_block(source, signature)
    with args.emit_cpp.open('x') as out:
        out.write(code + TEST)


if __name__ == '__main__':
    main()
