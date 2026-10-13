import hashlib
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from asset_io import AssetError
from pack import append_kernel, edit_script, encode
import rebase_overlay
from rebase_overlay import build,rebase,native_member,native_limit,overlay_index,resolve_overlay
from text_layouts import describe_resource

def bank(names):
    stride=108;pool=20+len(names)*stride
    data=bytearray(pool)
    struct.pack_into('<HHIHHHHI',data,0,1,0,1,0,len(names)-1,stride,len(names)*stride,20)
    for row,name in enumerate(names):
        for slot,value in enumerate([name,'Description',name,'Description']):
            struct.pack_into('<H',data,20+row*stride+slot*4,len(data)-pool)
            data+=encode(value,api=4)+b'\0'
    return bytes(data)

class OverlayRebaseTests(unittest.TestCase):
    def test_actual_file_loader_and_vbf_namespaces(self):
        self.assertEqual(native_member('/FFX_Data/ffx_ps2/ffx/master/uspc/menu/base.ftc'),'ffx_ps2/ffx/master/uspc/menu/base.ftc')
        self.assertEqual(native_member('/ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_0.dds.phyre'),
                         'ffx_data/gamedata/ps3data/menu_us/base_ftc/d3d11/font_0_0.dds.phyre')
        with self.assertRaises(AssetError):native_member('/ffx_data/ffx_ps2/../outside.bin')

    def test_preserves_gameplay_new_rows_and_changed_mod_text(self):
        layout=describe_resource('battle/kernel/a_ability.bin')
        original=bank(['Punch','Stone'])
        translated=append_kernel(original,[{'row':0,'slot':0,'text':'Soco'},{'row':1,'slot':0,'text':'Pedra'}],layout,api=4)
        changed=bytearray(bank(['Punch','Different mod skill','New mod skill']));changed[20+16]=87;effective=bytes(changed)
        result,count,preserved=rebase(original,translated,effective,layout,bytes([20]*208))
        self.assertEqual(count,1)
        self.assertGreater(preserved,0)
        self.assertEqual(result[20+16],87)
        self.assertEqual(edit_script(result,{'row':0,'slot':0},layout),encode('Soco',api=4))
        for row in [1,2]:
            self.assertEqual(result[20+row*108:20+(row+1)*108],effective[20+row*108:20+(row+1)*108])
        self.assertEqual(effective,bytes(changed))

    def test_no_translation_guess_when_mod_changes_identity(self):
        layout=describe_resource('battle/kernel/a_ability.bin');original=bank(['Punch'])
        translated=append_kernel(original,[{'row':0,'slot':0,'text':'Soco'}],layout,api=4)
        effective=bank(['New effect'])
        result,count,_=rebase(original,translated,effective,layout,bytes([20]*208))
        self.assertEqual(result,effective);self.assertEqual(count,0)

    def test_full_battle_container_is_never_read_as_one_text_row(self):
        data=bytearray(128);struct.pack_into('<I',data,0,8);struct.pack_into('<I',data,32,len(data))
        layout=describe_resource('battle/btl/kami00_00/kami00_00.bin')
        with self.assertRaisesRegex(AssetError,'complete battle container'):
            rebase(bytes(data),bytes(data),bytes(data),layout,bytes([20]*208))

def fingerprint(data):
    return hashlib.sha256(data).hexdigest()

def package_fixture(root,entries):
    """Minimal API 4 package/reference pair. entries: (id, family, request, path, source, target)."""
    package,reference=root/'package',root/'reference'
    resources=[]
    for rid,family,request,path,source,target in entries:
        for base,data in ((package,target),(reference,source)):
            leaf=base/path;leaf.parent.mkdir(parents=True,exist_ok=True);leaf.write_bytes(data)
        resources.append({'id':rid,'family':family,'request':request,'path':path,'source_size':len(source),
                          'size':len(target),'source_sha256':fingerprint(source),'sha256':fingerprint(target)})
    (package/'manifest.json').write_text(json.dumps({'schema_version':4,'hook_api':4,'display_name':'fixture','resources':resources}))
    return package,reference

WESTERN=bytes(64)+bytes([20]*208)+bytes(32)  # base.ftc: advances at 64..271
METRICS=('western-metrics','font_metrics','/ffx_data/ffx_ps2/ffx/master/uspc/menu/base.ftc','font/base.ftc',WESTERN,WESTERN)
BATTLE_UI='/ffx_data/gamedata/ps3data/menu_us/d3d11/battle.dds.phyre'

class OverlayResolutionTests(unittest.TestCase):
    """The File Loader resolves data/mods through Win32 under Wine/Proton: lookups
    are case-insensitive even though the Linux filesystem is not."""
    def test_wine_case_insensitive_overlay_lookup(self):
        with tempfile.TemporaryDirectory() as temporary:
            leaf=Path(temporary)/'FFX_Data/GameData/PS3Data/menu_us/D3D11/battle.dds.phyre'
            leaf.parent.mkdir(parents=True);leaf.write_bytes(b'mod')
            index=overlay_index(Path(temporary))
            self.assertEqual(resolve_overlay(index,BATTLE_UI[1:]),leaf)
            self.assertIsNone(resolve_overlay(index,'ffx_data/gamedata/ps3data/menu_us/d3d11/icon.dds.phyre'))

    def test_case_only_duplicates_are_ambiguous(self):
        with tempfile.TemporaryDirectory() as temporary:
            for name in ('D3D11','d3d11'):
                leaf=Path(temporary)/'menu_us'/name/'battle.dds.phyre';leaf.parent.mkdir(parents=True);leaf.write_bytes(name.encode())
            with self.assertRaisesRegex(AssetError,'ambiguous'):overlay_index(Path(temporary))

    def test_redirected_overlay_entry_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            outside=Path(temporary)/'outside.bin';outside.write_bytes(b'x')
            kernel=Path(temporary)/'mods/ffx_ps2/ffx/master/new_uspc/battle/kernel';kernel.mkdir(parents=True)
            os.symlink(outside,kernel/'Command.bin')
            index=overlay_index(Path(temporary)/'mods')
            with self.assertRaisesRegex(AssetError,'Redirected'):
                resolve_overlay(index,'ffx_ps2/ffx/master/new_uspc/battle/kernel/command.bin')

    def test_mod_replaced_ui_texture_is_left_to_the_mod(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            package,reference=package_fixture(root,[METRICS,('battle-ui','ui_texture',BATTLE_UI,'graphics/battle.dds.phyre',b'vanilla',b'traduzido')])
            leaf=root/'mods/FFX_Data/GameData/PS3Data/menu_us/D3D11/battle.dds.phyre'
            leaf.parent.mkdir(parents=True);leaf.write_bytes(b'mod texture')
            build(package,reference,root/'mods',root/'out')
            manifest=json.loads((root/'out/pack/manifest.json').read_text())
            self.assertEqual([r['id'] for r in manifest['resources']],['western-metrics'])
            report={r['id']:r for r in json.loads((root/'out/report.json').read_text())['resources']}
            self.assertEqual(report['battle-ui']['status'],'native-overlay-retained')
            self.assertEqual(report['western-metrics']['status'],'unchanged')
            self.assertFalse((root/'out/pack/graphics/battle.dds.phyre').exists())

    def test_mod_replaced_font_still_aborts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            package,reference=package_fixture(root,[METRICS])
            leaf=root/'mods/FFX_PS2/ffx/master/uspc/menu/BASE.FTC';leaf.parent.mkdir(parents=True);leaf.write_bytes(b'other font')
            with self.assertRaisesRegex(AssetError,'font'):build(package,reference,root/'mods',root/'out')


COMMAND='/ffx_data/ffx_ps2/ffx/master/new_uspc/battle/kernel/command.bin'

def table(names,descriptions,stride=96):
    """Indexed kernel table with a name (slot 0) and a description (slot 2) per row."""
    pool=20+len(names)*stride
    data=bytearray(pool)
    struct.pack_into('<HHIHHHHI',data,0,1,0,1,0,len(names)-1,stride,len(names)*stride,20)
    for row,(name,description) in enumerate(zip(names,descriptions)):
        for slot,value in enumerate([name,'-',description,'-']):
            struct.pack_into('<H',data,20+row*stride+slot*4,len(data)-pool)
            data+=encode(value,api=4)+b'\0'
    return bytes(data)

class NativeSizeLimitTests(unittest.TestCase):
    """FFX_Kernel_LoadFileToTable truncates four kernel table sizes to 16 bits."""
    def test_native_kernel_loader_limits_are_declared(self):
        # 0x382DF0 rounds sizes up to 16 bytes; four tables keep that size in 16 bits.
        for leaf in ('command.bin','a_ability.bin','item.bin','ply_rom.bin'):
            self.assertEqual(native_limit('ffx_ps2/ffx/master/new_uspc/battle/kernel/'+leaf),0xFFF0)
        for leaf in ('monmagic1.bin','monmagic2.bin'):
            self.assertEqual(native_limit('ffx_ps2/ffx/master/new_uspc/battle/kernel/'+leaf),0x25800)
        self.assertIsNone(native_limit('ffx_ps2/ffx/master/new_uspc/battle/kernel/btl_txt.bin'))
        self.assertIsNone(native_limit('ffx_data/gamedata/ps3data/menu_us/d3d11/battle.dds.phyre'))

    def test_keep_native_leaves_reviewed_slots_untranslated(self):
        layout=describe_resource('battle/kernel/command.bin')
        original=table(['Punch','Kick'],['Hits.','Kicks.'])
        translated=append_kernel(original,[{'row':0,'slot':0,'text':'Soco'},{'row':1,'slot':2,'text':'Chuta.'}],layout,api=4)
        result,count,_=rebase(original,translated,original,layout,bytes([20]*208),keep_native={(1,2)})
        self.assertEqual(count,1)
        self.assertEqual(edit_script(result,{'row':0,'slot':0},layout),encode('Soco',api=4))
        self.assertEqual(edit_script(result,{'row':1,'slot':2},layout),encode('Kicks.',api=4))

    def test_oversized_native_table_is_trimmed_or_left_to_the_mod(self):
        layout=describe_resource('battle/kernel/command.bin')
        original=table(['Punch','Kick','Throw'],['Hits once.','Kicks hard.','Throws far.'])
        edits=[{'row':row,'slot':0,'text':text} for row,text in enumerate(['Soco','Chute','Arremesso'])]
        edits.append({'row':2,'slot':2,'text':'Arremessa para muito longe.'})
        translated=append_kernel(original,edits,layout,api=4)
        saved=rebase_overlay.NATIVE_U16_LIMIT
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            package,reference=package_fixture(root,[METRICS,('command-bin','battle',COMMAND,'text/command.bin',original,translated)])
            (root/'mods').mkdir()
            try:
                rebase_overlay.NATIVE_U16_LIMIT=len(translated)-1
                build(package,reference,root/'mods',root/'omitted',keep_native={})
                manifest=json.loads((root/'omitted/pack/manifest.json').read_text())
                self.assertEqual([r['id'] for r in manifest['resources']],['western-metrics'])
                report={r['id']:r for r in json.loads((root/'omitted/report.json').read_text())['resources']}
                self.assertEqual(report['command-bin']['status'],'native-overlay-retained')
                self.assertIn('native kernel loader',report['command-bin']['reason'])
                build(package,reference,root/'mods',root/'trimmed',keep_native={COMMAND:{(2,2)}})
                manifest=json.loads((root/'trimmed/pack/manifest.json').read_text())
                entry=next(r for r in manifest['resources'] if r['id']=='command-bin')
                payload=(root/'trimmed/pack/text/command.bin').read_bytes()
                self.assertLessEqual(len(payload),rebase_overlay.NATIVE_U16_LIMIT)
                self.assertEqual(entry['size'],len(payload))
                self.assertEqual(edit_script(payload,{'row':2,'slot':0},layout),encode('Arremesso',api=4))
                self.assertEqual(edit_script(payload,{'row':2,'slot':2},layout),encode('Throws far.',api=4))
                report={r['id']:r for r in json.loads((root/'trimmed/report.json').read_text())['resources']}
                self.assertEqual(report['command-bin']['keptNative'],1)
            finally:
                rebase_overlay.NATIVE_U16_LIMIT=saved

    def test_extended_kernel_tables_raise_only_the_adapter_tables(self):
        # The optional DLL adapter owns loader cases 0/2/3; ply_rom keeps its native path.
        kernel='ffx_ps2/ffx/master/new_uspc/battle/kernel/'
        for leaf in ('command.bin','a_ability.bin','item.bin'):
            self.assertEqual(native_limit(kernel+leaf,extended=True),0x25800)
        self.assertEqual(native_limit(kernel+'ply_rom.bin',extended=True),0xFFF0)
        self.assertEqual(native_limit(kernel+'monmagic2.bin',extended=True),0x25800)
        self.assertIsNone(native_limit(kernel+'btl_txt.bin',extended=True))

    def test_extended_build_publishes_the_full_translation(self):
        layout=describe_resource('battle/kernel/command.bin')
        original=table(['Punch','Kick'],['Hits once.','Kicks hard.'])
        edits=[{'row':0,'slot':0,'text':'Soco'},{'row':1,'slot':2,'text':'Chuta com muita força.'}]
        translated=append_kernel(original,edits,layout,api=4)
        saved=rebase_overlay.NATIVE_U16_LIMIT
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            package,reference=package_fixture(root,[METRICS,('command-bin','battle',COMMAND,'text/command.bin',original,translated)])
            (root/'mods').mkdir()
            try:
                rebase_overlay.NATIVE_U16_LIMIT=len(translated)-1
                build(package,reference,root/'mods',root/'wide',keep_native={COMMAND:{(1,2)}},extended_kernel_tables=True)
                payload=(root/'wide/pack/text/command.bin').read_bytes()
                self.assertEqual(payload,translated)
                self.assertEqual(edit_script(payload,{'row':1,'slot':2},layout),encode('Chuta com muita força.',api=4))
                report=json.loads((root/'wide/report.json').read_text())
                self.assertEqual(report['kernelTables'],'32bit')
                entry={r['id']:r for r in report['resources']}['command-bin']
                self.assertEqual(entry['status'],'unchanged')
                self.assertNotIn('keptNative',entry)
                build(package,reference,root/'mods',root/'narrow',keep_native={COMMAND:{(1,2)}})
                self.assertEqual(json.loads((root/'narrow/report.json').read_text())['kernelTables'],'16bit')
                narrow=(root/'narrow/pack/text/command.bin').read_bytes()
                self.assertEqual(edit_script(narrow,{'row':1,'slot':2},layout),encode('Kicks hard.',api=4))
            finally:
                rebase_overlay.NATIVE_U16_LIMIT=saved

    def test_reviewed_command_deferrals_are_loaded(self):
        limits=rebase_overlay.load_native_limits()
        self.assertIn((239,2),limits[COMMAND]);self.assertIn((302,2),limits[COMMAND]);self.assertIn((303,2),limits[COMMAND])


if __name__=='__main__':unittest.main()
