"""Execute actual GridTeach Nul/menu bodies with a private bank and real learned core.

The native resolver/range/copy boundary is simulated. This is menu logic evidence,
not a live ABI, animation or gameplay observation.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def function(source,name):
    match=re.search(r'^.*\b'+name+r'\([^\n]*\)\s*(?:noexcept\s*)?\{',source,re.M)
    if not match:raise AssertionError('Missing actual function '+name)
    at=match.end();depth=1
    while depth and at<len(source):
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    if depth:raise AssertionError('Unbalanced actual function')
    return source[match.start():at].replace('__try','try').replace('__except(EXCEPTION_EXECUTE_HANDLER)','catch(...)')

PRE=r'''
#include "src/runtime/FfxHooksDll/hooks/NulElementCommands.h"
#include "src/runtime/FfxHooksDll/hooks/GridLearnedCore.h"
#include "src/runtime/FfxHooksDll/shared/ffx_addresses.h"
#include <cstdio>
#define __cdecl
using namespace FfxHooks;
alignas(16) unsigned char rings[7*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE]{};
std::array<std::array<std::uint8_t,96>,374> bank{};
std::uintptr_t module=0;
bool admitted=true,nulSpellMenus=false;
bool Admitted(){return admitted;}
namespace Learning {
GridLearned::State state;
bool Has(unsigned owner,unsigned id){return state.Has(owner,id);}
bool Ready(){return state.Ready();}
}
namespace RecoveryNative {
bool Range(std::uintptr_t p,std::size_t n,std::uintptr_t=0,bool=false,bool=false){
 const auto in=[&](const void* begin,std::size_t bytes){const auto a=reinterpret_cast<std::uintptr_t>(begin);return p>=a&&p-a<=bytes&&n<=bytes-(p-a);};
 return in(rings,sizeof(rings))||in(bank.data(),sizeof(bank));
}
bool Copy(void* out,const void* in,std::size_t n){
 if(reinterpret_cast<std::uintptr_t>(in)==RVA_FFX_BATTLE_COMMAND_RING_BASE_PTR){
  const auto address=reinterpret_cast<std::uintptr_t>(rings);if(address>UINT32_MAX||n!=4)return false;
  const auto wire=static_cast<std::uint32_t>(address);std::memcpy(out,&wire,4);return true;
 }
 if(!Range(reinterpret_cast<std::uintptr_t>(in),n))return false;std::memcpy(out,in,n);return true;
}
}
using HasFn=int(*)(std::uint8_t,std::uint16_t);
int Original(std::uint8_t,std::uint16_t){return 7;}
void* hasOriginal=reinterpret_cast<void*>(&Original);
std::uint8_t* Entry(std::int16_t id,int){return bank[id>=0&&id<374?id:0].data();}
auto commandEntry=&Entry;
'''
POST=r'''
unsigned checks=0,failures=0;
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
bool Listed(unsigned owner,unsigned offset,unsigned count,unsigned id){
 const auto* row=reinterpret_cast<std::uint16_t*>(rings+owner*FFX_BATTLE_COMMAND_RING_SLOT_STRIDE+offset);
 for(unsigned i=0;i<count;++i)if(row[i]==(0x3000|id))return true;return false;
}
int main(){
 std::memset(rings,0xff,sizeof(rings));Learning::state.BeginSession();
 bank[366][25]=1;bank[343][25]=1;
 for(const auto& command:NulElements::Commands){auto& row=bank[command.id];row[16]=command.animation&255;row[17]=command.animation>>8;
  row[23]=row[24]=4;row[25]=1;row[26]=5;row[37]=2;row[43]=1;
  Check(AuthoredNul(command.id),"real resolver admits the canonical authored bank");
  Check(HasShim(1,0x3000|command.id)==0,"unlearned Nul is not granted by feature setup");
  Learning::state.Set(1,command.id,true);
  Check(HasShim(1,0x3000|command.id)==0,"learned canonical Nul remains unavailable while OFF");
 }
 nulSpellMenus=true;Learning::state.Set(1,343,true);const auto learned=Learning::state.Words();
 Check(HasShim(1,0x316e)==1&&!Learning::state.Has(1,366),"root opens from a learned Nul without writing a root unlock");
 UpdateExtendedMenu(1);
 for(const auto& command:NulElements::Commands){
  Check(HasShim(1,0x3000|command.id)==1&&HasShim(0,0x3000|command.id)==0,"learning and command owner stay character-bound");
  Check(Listed(1,296,24,command.id),"all six learned Nuls enter White Magic+");
  Check(!Listed(1,0,20,command.id),"child commands do not leak to the main ring");
 }
 Check(Listed(1,0,20,366)&&Listed(1,296,24,343),"existing white child and derived root survive together");
 UpdateExtendedMenu(1);Check(Learning::state.Words()==learned,"menu building never mutates persistent learning");
 Check(!AuthoredNul(373+1),"missing native row fallback is not a Nul");
 bank[373][37]=99;Check(!AuthoredNul(373),"edited payload does not claim canonical Nul admission");
 Learning::state.Clear();Check(!NulLearned(1)&&HasShim(1,0x3172)==0,"save reset cannot inherit Nuls");
 admitted=false;Check(HasShim(1,0x3172)==7,"stopped adapter delegates to native behavior");
 std::printf("NulMenuActualSource %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
'''

class Menu(unittest.TestCase):
    def test_actual_menu_binding_and_save_scope(self):
        source=(ROOT/'src/runtime/FfxHooksDll/hooks/GridTeachHook.cpp').read_text()
        names=['EntryAllows','AuthoredNul','NulLearned','HasShim','Empty','Append','Misplaced','UpdateExtendedMenu']
        code=PRE+'\n'+'\n'.join(function(source,name) for name in names)+'\n'+POST
        compiler=shutil.which(os.environ.get('CXX','g++'));self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='nul-menu-') as folder:
            cpp=Path(folder)/'test.cpp';exe=Path(folder)/'test';cpp.write_text(code)
            subprocess.run([compiler,'-std=c++17','-O2','-no-pie','-I',str(ROOT),str(cpp),'-o',str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=10)

if __name__=='__main__':unittest.main(verbosity=2)
