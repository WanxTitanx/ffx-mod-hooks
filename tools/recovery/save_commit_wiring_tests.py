#!/usr/bin/env python3
"""Actual save-IAT publication/rollback functions, simulated OS imports."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]

def block(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]

MOCK=r'''
#include "src/runtime/FfxHooksDll/hooks/NativeSaveCommitAdapter.h"
#include <atomic>
#include <cstdio>
#define __cdecl
using namespace FfxHooks;
constexpr unsigned kReadImport=0x70c3f4,kWriteImport=0x70c428,kCloseImport=0x70c3f0;
struct ImportPatch{bool owned=false;};
ImportPatch readPatch{},writePatch{},closePatch{};
std::atomic<unsigned> readiness{0};
std::atomic<bool> verifiedSaveIoReady{false};
bool preparedOnce=true,verifiedSaveRequested=false;
unsigned publishes=0,restores=0,failedRva=0,closeCalls=0;
NativeSaveCommit::Runtime verifiedSaveRuntime;
std::size_t ReadShim(void*,std::size_t,std::size_t,void*){return 0;}
std::size_t WriteShim(const void*,std::size_t,std::size_t,void*){return 0;}
std::size_t OriginalRead(void*,std::size_t,std::size_t,void*){return 0;}
std::size_t OriginalWrite(const void*,std::size_t,std::size_t,void*){return 0;}
int OriginalClose(void*){++closeCalls;return -17;}
auto readOriginal=OriginalRead;
auto writeOriginal=OriginalWrite;
auto closeOriginal=OriginalClose;
bool PublishImport(unsigned rva,void*,void*,ImportPatch* patch){
    ++publishes;if(rva==failedRva)return false;patch->owned=true;return true;
}
bool RestoreImport(unsigned,void*,void*,ImportPatch* patch){++restores;patch->owned=false;return true;}
bool RestoreIoImports() noexcept;
unsigned checks=0,failures=0;
void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL: %s\n",why);}}
void Reset(){readiness=0;preparedOnce=true;verifiedSaveRequested=false;verifiedSaveIoReady=false;
 readPatch={};writePatch={};closePatch={};publishes=restores=failedRva=0;}
'''
TEST=r'''
int main(){
 Reset();Check(InstallIoImports(),"legacy save publisher installs unchanged");
 Check(publishes==2&&!closePatch.owned&&!verifiedSaveIoReady,"no close interception without a verified subscriber");
 Check(RestoreIoImports()&&!readPatch.owned&&!writePatch.owned,"legacy rollback retained");
 Reset();verifiedSaveRequested=true;
 Check(InstallIoImports()&&publishes==3&&closePatch.owned&&verifiedSaveIoReady,"optional close joins the same transaction");
 Check(RestoreIoImports()&&!verifiedSaveIoReady&&!closePatch.owned,"verified admission closes before IAT restoration");
 for(unsigned rva:{kReadImport,kWriteImport,kCloseImport}){
   Reset();verifiedSaveRequested=true;failedRva=rva;
   Check(!InstallIoImports(),"each import failure blocks installation");
   Check(!readPatch.owned&&!writePatch.owned&&!closePatch.owned&&!verifiedSaveIoReady,"partial publication is compensated");
 }
 Reset();preparedOnce=false;Check(!InstallIoImports()&&publishes==0,"unprepared install has no effects");
 Reset();readiness=2;Check(!InstallIoImports()&&publishes==0,"stopped publisher cannot reinstall");
 Check(CloseShim(reinterpret_cast<void*>(1))==-17&&closeCalls==1,"inactive metadata forwards close once");
 std::printf("NativeSaveCommitWiringRt0: %u/%u passed\n",checks-failures,checks);
 return failures?1:0;
}
'''
def main():
    source=(ROOT/'src/runtime/FfxHooksDll/hooks/RonsoPoolRuntime.cpp').read_text()
    signatures=('int __cdecl CloseShim(', 'bool InstallIoImports()', 'bool RestoreIoImports()')
    if any(signature not in source for signature in signatures):
        print('FAIL: verified close is not wired into the existing native save publisher');return 1
    cpp=MOCK+'\n'+'\n'.join(block(source,s) for s in signatures)+'\n'+TEST
    with tempfile.TemporaryDirectory(prefix='ffx-save-commit-wiring-') as temp:
        path=Path(temp)/'test.cpp';exe=Path(temp)/'test';path.write_text(cpp)
        subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O2','-Wall','-Wextra',
            '-Werror','-pthread','-I',str(ROOT),str(path),
            str(ROOT/'src/runtime/FfxHooksDll/hooks/RonsoPoolSave.cpp'),'-o',str(exe)],check=True,timeout=60)
        subprocess.run([str(exe)],check=True,timeout=15)
    return 0
if __name__=='__main__':raise SystemExit(main())
