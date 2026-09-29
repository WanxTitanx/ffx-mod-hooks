#!/usr/bin/env python3
"""Native F8 Photo routing contracts: execute actual count/title functions.
Label/action/Back wiring is checked structurally, not as a rendered game menu.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
def function(source,name):
    match=re.search(r'^.*\b'+re.escape(name)+r'\([^\n]*\)\s*(?:noexcept\s*)?\{',source,re.M)
    if not match:raise AssertionError('Missing actual function '+name)
    start=match.start();at=match.end();depth=1
    # Selected functions contain no brace literals; reject a missing closing brace.
    while at<len(source) and depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    if depth:raise AssertionError('Unbalanced function '+name)
    return source[start:at]
class PhotoMenuTests(unittest.TestCase):
    def setUp(self):
        self.ui=(ROOT/'src/runtime/FfxHooksDll/hooks/NativeSettingsUi.inl').read_text()
        self.dll=(ROOT/'src/runtime/FfxHooksDll/dllmain.cpp').read_text()
    def test_actual_counts_titles_preserve_existing_pages(self):
        enum=re.search(r'enum class NativeSettingsPage\s*\{[^}]+\};',self.ui).group()
        actions=(ROOT/'src/runtime/BattlePhotoMode/PhotoModeActions.h').read_text()
        menu_count=function(actions,'MenuCount')
        count=function(self.ui,'F8NativeSettingsCount');title=function(self.ui,'F8NativeSettingsTitle')
        # Compile Seymour's actual portable declaration rather than duplicating
        # its count in a stub as this shared page function gains dependencies.
        text='''#include <cstdio>\n#include <cstring>\n#include "SeymourCompatibilityHook.h"\n#include "VanguardCatalog.h"\n#include "ModFeatureCatalog.h"\nnamespace FfxHooks::NativeBindings {enum class Action {Count=4};}\nnamespace PhotoMode {'''+menu_count+'''}\nint g_nativeSettingsLanguage=0;unsigned g_nativeElementIndex=0;char g_nativeHookElementTitle[65]="Element color";\n'''+enum+'\n'+function(self.ui,'F8RewardPage')+'\nstatic int F8RewardCount(NativeSettingsPage){return 0;}\nstatic const char* F8RewardTitle(NativeSettingsPage){return \"Rewards\";}\n'+function(self.ui,'F8NativeVanguardGroup')+'\n'+count+'\n'+title+r'''
int main(){
 int failed=0;
 static_assert(static_cast<int>(NativeSettingsPage::Workshop)==12);
 static_assert(static_cast<int>(NativeSettingsPage::ElementVisibility)==17);
 static_assert(static_cast<int>(NativeSettingsPage::ArenaOptions)==18);
 static_assert(static_cast<int>(NativeSettingsPage::TextLanguages)==32);
 static_assert(static_cast<int>(NativeSettingsPage::PhotoMode)==43);
 static_assert(static_cast<int>(NativeSettingsPage::Seymour)==44);
 const auto photo=NativeSettingsPage::PhotoMode;
 if(F8NativeSettingsCount(photo)!=27){std::puts("FAIL Photo has 26 actions/settings and Back");++failed;}
 if(std::strcmp(F8NativeSettingsTitle(photo),"Battle Photo Mode")){std::puts("FAIL Photo title");++failed;}
 if(F8NativeSettingsCount(NativeSettingsPage::Workshop)!=6){std::puts("FAIL protected Workshop count");++failed;}
 if(F8NativeSettingsCount(NativeSettingsPage::ElementVisibility)!=7){std::puts("FAIL protected element count");++failed;}
 return failed?1:0;
}
'''
        compiler=shutil.which(os.environ.get('CXX','g++'))
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='ffx-photo-menu-') as temp:
            cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test';cpp.write_text(text)
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'src/runtime/FfxHooksDll/hooks'),str(cpp),'-o',str(exe)],check=True,timeout=60)
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
    def test_existing_back_precedes_photo_action_and_label_delegation(self):
        label=function(self.ui,'F8NativeSettingsLabel');action=function(self.ui,'F8NativeSettingsActivate')
        self.assertIn('PhotoMode::MenuLabel(row,out,capacity)',label)
        self.assertIn('PhotoMode::MenuAction(row)',action)
        self.assertLess(label.index('if(row==count-1)'),label.index('PhotoMode::MenuLabel'))
        self.assertLess(action.index('F8NativeSettingsPop(obj);return;'),action.index('PhotoMode::MenuAction'))
        self.assertIn('PhotoMode::Detail(g_nativeSettingsNotice',action)
    def test_native_dev_entry_and_nonblocking_detach_are_wired(self):
        self.assertRegex(self.dll,r'if\(strcmp\(tabName,"Dev"\)==0 && g_f7RowCount<31\)\s*g_f7Rows\[g_f7RowCount\+\+\]=\{"Battle Photo Mode",F7RT_OPTIONS,static_cast<int>\(NativeSettingsPage::PhotoMode\)')
        detach=self.dll[self.dll.index('case DLL_PROCESS_DETACH:'):]
        self.assertIn('PhotoMode::RequestStop();',detach)
        self.assertNotIn('PhotoMode::Exit()',detach)
        self.assertNotIn('PhotoMode::ToggleCapability',detach)
if __name__=='__main__':unittest.main(verbosity=2)
