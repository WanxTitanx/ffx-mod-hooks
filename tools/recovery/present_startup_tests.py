#!/usr/bin/env python3
"""Execute the actual late Present acquisition thread with a simulated clock.

The real AuroraD3DLatePresentFallbackThreadProc (and, when present, the native
renderer-swapchain helper) is extracted from dllmain.cpp and compiled against
simulated Win32/arbiter dependencies. No game, DLL, D3D device or native memory
is accessed. A failure on the pre-change source is the expected RED result: the
previous thread always waited the full fallback delay before probing a dummy.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

MOCK = r'''
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
using LONG=std::int32_t;
using DWORD=std::uint32_t;
using LPVOID=void*;
using HANDLE=void*;
#define WINAPI
namespace Sim {
DWORD now=0;
int delayEnv=15000,nativeEnv=1;
bool delayEnvSet=false,nativeEnvSet=false;
long installCompleteAt=200,swapAvailableAt=0,externalReadyAt=-1,stopAt=-1;
bool presentExecutable=true,nativeHookOk=true,dummyCreateOk=true,dummyHookOk=true;
int nativeReads=0,nativeHooks=0,dummyCreates=0,dummyHooks=0,terminals=0;
long firstNativeHookAt=-1,dummyAt=-1;
bool ready=false;
std::string reason,logs;
void Reset(){now=0;delayEnv=15000;nativeEnv=1;delayEnvSet=nativeEnvSet=false;
 installCompleteAt=200;swapAvailableAt=0;externalReadyAt=-1;stopAt=-1;
 presentExecutable=nativeHookOk=dummyCreateOk=dummyHookOk=true;
 nativeReads=nativeHooks=dummyCreates=dummyHooks=terminals=0;firstNativeHookAt=dummyAt=-1;
 ready=false;reason.clear();logs.clear();}
}
volatile LONG g_auroraD3DFallbackRunning=1;
volatile LONG g_installHooksComplete=0;
HANDLE g_auroraD3DFallbackThread=nullptr;
static void World(){
 if(Sim::installCompleteAt>=0&&static_cast<long>(Sim::now)>=Sim::installCompleteAt)g_installHooksComplete=1;
 if(Sim::externalReadyAt>=0&&static_cast<long>(Sim::now)>=Sim::externalReadyAt&&!Sim::ready){Sim::ready=true;Sim::reason="create-device";}
 if(Sim::stopAt>=0&&static_cast<long>(Sim::now)>=Sim::stopAt)g_auroraD3DFallbackRunning=0;
}
LONG InterlockedCompareExchange(volatile LONG* p,LONG value,LONG expected){World();const LONG old=*p;if(old==expected)*p=value;return old;}
LONG InterlockedExchange(volatile LONG* p,LONG value){const LONG old=*p;*p=value;return old;}
void Sleep(DWORD ms){Sim::now+=ms;World();}
int EnvInt(const char* name,int fallback){
 if(std::strcmp(name,"FFXHOOKS_AURORA_D3D_FALLBACK_DELAY_MS")==0)return Sim::delayEnvSet?Sim::delayEnv:fallback;
 if(std::strcmp(name,"FFXHOOKS_AURORA_D3D_NATIVE_SWAPCHAIN")==0)return Sim::nativeEnvSet?Sim::nativeEnv:fallback;
 return fallback;}
void Log(const char* format,...){char text[512]{};va_list args;va_start(args,format);
 std::vsnprintf(text,sizeof(text),format,args);va_end(args);Sim::logs+=text;}
bool AuroraD3DPresentReady(){World();return Sim::ready;}
static void* g_nativeTable[16]={};
static void* g_dummyTable[16]={};
bool AuroraD3DReadNativeRendererPresentSlot(void*** outVtable,void** outPresent){
 ++Sim::nativeReads;World();
 if(Sim::swapAvailableAt<0||static_cast<long>(Sim::now)<Sim::swapAvailableAt){*outVtable=nullptr;*outPresent=nullptr;return false;}
 g_nativeTable[8]=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10001000u));
 *outVtable=g_nativeTable;*outPresent=g_nativeTable[8];return true;}
bool AuroraD3DCommittedExecutable(const void*){return Sim::presentExecutable;}
bool AuroraD3DHookPresentFromVtable(void** vtable,const char* reason){
 const bool native=std::strcmp(reason,"native-renderer-swapchain")==0;
 if(native){++Sim::nativeHooks;if(Sim::firstNativeHookAt<0)Sim::firstNativeHookAt=static_cast<long>(Sim::now);}
 else ++Sim::dummyHooks;
 const bool ok=native?(Sim::nativeHookOk&&vtable==g_nativeTable):(Sim::dummyHookOk&&vtable==g_dummyTable);
 if(ok&&!Sim::ready){Sim::ready=true;Sim::reason=reason;}
 return Sim::ready;}
bool AuroraD3DCreateDummySwapChain(void*** outVtable){
 ++Sim::dummyCreates;Sim::dummyAt=static_cast<long>(Sim::now);
 if(!Sim::dummyCreateOk){*outVtable=nullptr;return false;}
 g_dummyTable[8]=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x20002000u));*outVtable=g_dummyTable;return true;}
bool TryPublishAuroraD3DPresentTerminal(){if(Sim::ready)return false;++Sim::terminals;return true;}
'''

TEST = r'''
int total=0,failed=0;
void Check(bool condition,const char* message){++total;if(!condition){++failed;std::fprintf(stderr,"FAIL: %s\n",message);}}
void Run(){g_auroraD3DFallbackRunning=1;g_installHooksComplete=0;g_auroraD3DFallbackThread=nullptr;
 World();(void)AuroraD3DLatePresentFallbackThreadProc(nullptr);}
int main(){
 Sim::Reset();Run();
 Check(Sim::ready&&Sim::reason=="native-renderer-swapchain","existing game swapchain supplies Present");
 Check(Sim::firstNativeHookAt>=200&&Sim::firstNativeHookAt<=250,"Present is taken within one poll after InstallHooks completes");
 Check(Sim::dummyCreates==0&&Sim::terminals==0,"no dummy device and no terminal publication on the native path");
 Check(g_auroraD3DFallbackRunning==0,"thread retires its running flag");

 Sim::Reset();Sim::installCompleteAt=-1;Run();
 Check(Sim::nativeHooks==0,"native swapchain is never taken before InstallHooks completes");
 Check(Sim::dummyCreates==1&&Sim::dummyAt==15000&&Sim::reason=="late-dummy-swapchain","incomplete install keeps the historical 15 s dummy order");

 Sim::Reset();Sim::swapAvailableAt=3000;Run();
 Check(Sim::reason=="native-renderer-swapchain"&&Sim::firstNativeHookAt>=3000&&Sim::firstNativeHookAt<=3050,
  "late renderer publication is taken within one poll");
 Check(Sim::nativeHooks==1&&Sim::dummyCreates==0,"polling before publication performs no hook attempt");

 Sim::Reset();Sim::swapAvailableAt=-1;Run();
 Check(Sim::nativeHooks==0&&Sim::dummyCreates==1&&Sim::dummyAt==15000,"absent renderer swapchain retains the bounded dummy fallback");
 Check(Sim::ready&&Sim::reason=="late-dummy-swapchain"&&Sim::terminals==0,"dummy success remains non-terminal");

 Sim::Reset();Sim::swapAvailableAt=-1;Sim::dummyHookOk=false;Run();
 Check(!Sim::ready&&Sim::terminals==1,"completed unsuccessful dummy attempt publishes terminal exactly once");

 Sim::Reset();Sim::nativeEnvSet=true;Sim::nativeEnv=0;Run();
 Check(Sim::nativeReads==0&&Sim::dummyAt==15000,"FFXHOOKS_AURORA_D3D_NATIVE_SWAPCHAIN=0 restores dummy-only timing");

 Sim::Reset();Sim::presentExecutable=false;Run();
 Check(Sim::nativeReads==1&&Sim::nativeHooks==0,"non-executable native slot is rejected once, without a detour");
 Check(Sim::dummyAt==15000&&Sim::reason=="late-dummy-swapchain","rejected native slot leaves the dummy path its turn");

 Sim::Reset();Sim::nativeHookOk=false;Run();
 Check(Sim::nativeHooks==1&&Sim::nativeReads==1,"failed native detour is not retried");
 Check(Sim::dummyCreates==1&&Sim::dummyAt==15000,"failed native detour still reaches the bounded dummy attempt");

 Sim::Reset();Sim::installCompleteAt=500;Sim::externalReadyAt=100;Run();
 Check(Sim::reason=="create-device"&&Sim::nativeHooks==0&&Sim::dummyCreates==0&&Sim::terminals==0,
  "an earlier CreateDevice installation ends the thread without another attempt");

 Sim::Reset();Sim::installCompleteAt=5000;Sim::stopAt=1000;Run();
 Check(Sim::nativeHooks==0&&Sim::dummyCreates==0&&Sim::terminals==0,"stop before completion performs no attempt and no terminal");

 Sim::Reset();Sim::delayEnvSet=true;Sim::delayEnv=0;Run();
 Check(Sim::nativeReads==0&&Sim::dummyAt==0&&Sim::reason=="late-dummy-swapchain","zero delay keeps its immediate dummy meaning");

 std::printf("PresentStartupSimulatedRt0: %d/%d passed (actual late Present thread, simulated clock)\n",total-failed,total);
 return failed?1:0;
}
'''


def function_block(source, signature, required=True):
    start = source.find(signature)
    if start < 0:
        if required:
            raise SystemExit(f'missing function: {signature}')
        return ''
    brace = source.index('{', start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1] + '\n'
    raise SystemExit(f'unbalanced function: {signature}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-cpp', type=Path, help='Write the standalone harness into a NEW file')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/runtime/FfxHooksDll/dllmain.cpp').read_text(encoding='utf-8')
    native = function_block(source, 'static bool AuroraD3DTryHookNativeRendererPresent(bool* attempted)', required=False)
    late = function_block(source, 'static DWORD WINAPI AuroraD3DLatePresentFallbackThreadProc(LPVOID)')
    translation = MOCK + native + late + TEST
    if args.emit_cpp:
        with args.emit_cpp.open('x', encoding='utf-8') as stream:
            stream.write(translation)
        return
    compiler = shutil.which(os.environ.get('CXX', 'g++'))
    if not compiler:
        raise SystemExit('C++17 compiler required')
    with tempfile.TemporaryDirectory(prefix='ffx-present-startup-rt0-') as temp:
        path = Path(temp)
        cpp = path / 'test.cpp'
        exe = path / 'test'
        cpp.write_text(translation)
        flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
        if os.environ.get('FFX_PRESENT_TEST_SANITIZE'):
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        subprocess.run([compiler, *flags, str(cpp), '-o', str(exe)], check=True, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
