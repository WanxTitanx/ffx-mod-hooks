#!/usr/bin/env python3
"""Execute the actual F7 element draft functions without a game or Windows UI."""
import argparse
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def function(source, token):
    start=source.index(token);opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path);args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'src/runtime/FfxHooksDll/dllmain.cpp').read_text()
    parts=['#include <cstdio>\nnamespace NativeMenu {void PlaySfx(int){}}\n'
           'static int g_f7Vals[32]{},g_f7DiffPresetIdx=0;static bool g_f7DifficultyEnabled=false;\n'
           'static unsigned g_f7CustomElementBit=32; constexpr int F7_STATUS_COUNT=25;\n'
           'enum F7DiffCol {F7DC_PRESETS,F7DC_BASE,F7DC_AUTO,F7DC_WEAK,F7DC_RESIST,F7DC_ABSORB,F7DC_ACTIONS};\n'
           'static void F7DiffSetStatus(const char*){}\n']
    token='static unsigned F7ElementRowMask('
    if token in source:parts.append(function(source,token))
    parts.extend(function(source,t) for t in ('static int F7DiffColRows(','static void F7DiffToggleBit('))
    parts.append(r'''
static unsigned checks=0,failed=0;
static void Check(bool ok,const char* s){++checks;if(!ok){++failed;std::printf("FAIL %s\n",s);}}
int main(){
 for(unsigned custom:{32u,64u}){
  g_f7CustomElementBit=custom;
  Check(F7DiffColRows(F7DC_WEAK)==7&&F7DiffColRows(F7DC_RESIST)==7&&F7DiffColRows(F7DC_ABSORB)==7,"all affinity columns expose seven entries");
  const unsigned masks[]={1,2,4,8,16,128,custom};
  for(int row=0;row<7;++row){
   for(auto& v:g_f7Vals)v=0;g_f7DifficultyEnabled=false;g_f7DiffPresetIdx=0;
   F7DiffToggleBit(13,row);
   Check(g_f7Vals[13]==static_cast<int>(masks[row]),"row maps to its real Holy Darkness or selected Custom bit");
   Check(g_f7DifficultyEnabled&&g_f7DiffPresetIdx==-1,"editing an OFF preset stages an enabled Custom difficulty");
   F7DiffToggleBit(11,row);
   Check(g_f7Vals[13]==0&&g_f7Vals[11]==static_cast<int>(masks[row]),"new affinity clears a competing selection for that bit only");
  }
 }
 std::printf("F7_ELEMENT_DRAFT %u/%u passed\n",checks-failed,checks);return failed?1:0;
}
'''.replace('#include <cstdio>',''))
    code=out/'draft.cpp';code.write_text('#include <initializer_list>\n'+'\n'.join(parts))
    executable=out/'draft';subprocess.run(['c++','-std=c++17',str(code),'-o',str(executable)],check=True)
    run=subprocess.run([str(executable)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'result.log').write_text(run.stdout);print(run.stdout,end='');return run.returncode

if __name__=='__main__':raise SystemExit(main())
