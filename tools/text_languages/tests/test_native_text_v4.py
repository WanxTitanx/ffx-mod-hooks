"""API 4 keeps wire identities while permitting container-backed text growth."""
from pathlib import Path
from contextlib import contextmanager
from types import SimpleNamespace
from unittest.mock import patch
import json,struct,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import pack
from asset_io import AssetError
from test_extended import indexed

class NativeTextV4Tests(unittest.TestCase):
    def test_speaker_prefix_is_preserved_with_choices_and_positioning(self):
        for speaker in ('{CTRL:13:30}', '{CTRL:19:30}'):
            for control in ('{CTRL:10:30}', '{CTRL:07:54}'):
                with self.subTest(speaker=speaker,control=control):
                    source=pack.encode(speaker+'\n'+control+'Original option',api=4)
                    valid=pack.encode(speaker+'\n'+control+'Opção',api=4)
                    self.assertTrue(pack.compatible_controls(source,valid,api=4))
                    self.assertFalse(pack.compatible_controls(source,pack.encode('A',api=4)+valid,api=4))

    def test_retained_native_symbols_roundtrip_without_raw_control_escape(self):
        raw=bytes([0x8f,0x90,0x9c,0xd4])
        self.assertEqual(pack.encode(pack.decode(raw,api=4),api=4),raw)
        with self.assertRaises(AssetError):pack.encode('{GLYPH:01}',api=4)
        with self.assertRaises(AssetError):pack.encode('{GLYPH:9C}',api=3)
        self.assertEqual(pack.control_api(bytes([7,0x54,1])),4)

    def test_native_spacing_style_and_large_variable_roundtrip(self):
        text='{CTRL:07:54}{CTRL:0E:40}{CTRL:12:45}{CTRL:0B:20}Info{CTRL:0E:41}'
        raw=pack.encode(text,api=4)
        self.assertEqual(pack.decode(raw,api=4),text)
        with self.assertRaises(AssetError):pack.encode(text,api=3)

    def test_ordinals_require_the_new_profile(self):
        self.assertEqual(pack.encode('1º 2ª',api=4),pack.encode('1',api=4)+bytes([246])+pack.encode(' 2',api=4)+bytes([247]))
        with self.assertRaises(AssetError):pack.encode('1º',api=3)

    def test_growth_retains_old_api_rejection_and_absolute_limits(self):
        layout=pack.describe_resource('command.bin');source=indexed(96,4)
        edits=[dict(row=101,slot=0,text='Portuguese text with a longer translated description')]
        target=pack.rebuild_resource(source,edits,layout,api=4)
        widths=bytes([24]*230)
        with self.assertRaises(AssetError):pack.validate_edits(source,target,edits,widths,layout=layout,api=3)
        audit=pack.validate_edits(source,target,edits,widths,layout=layout,api=4)
        self.assertEqual(audit[0]['required_api'],4)
        self.assertTrue(audit[0]['layout_review_required'])
        with self.assertRaises(AssetError):pack.encode('A'*2049,api=4)

    def test_content_line_reflow_preserves_state_and_choice_layout(self):
        a=pack.encode('A{CTRL:13:30}\nB',api=4);b=pack.encode('A\n{CTRL:13:30}B',api=4)
        self.assertTrue(pack.compatible_controls(a,b,api=4))
        self.assertFalse(pack.compatible_controls(a,b,api=3))
        self.assertFalse(pack.compatible_controls(a,pack.encode('A\n{CTRL:13:31}B',api=4),api=4))
        a=pack.encode('{CTRL:10:30}A\n{CTRL:10:31}B',api=4)
        b=pack.encode('{CTRL:10:30}A{CTRL:10:31}\nB',api=4)
        self.assertFalse(pack.compatible_controls(a,b,api=4))

class ProducerBoundaryTests(unittest.TestCase):
    @contextmanager
    def fixture(self,source_by_member):
        # Only external asset/identity/font acquisition is stubbed. Real table
        # rewriting, validation and directory publication run on synthetic data.
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);native=root/'native';native.mkdir()
            vbf=native/'source.vbf';vbf.write_bytes(b'synthetic archive')
            exe=native/'FFX.exe';exe.write_bytes(b'synthetic identity')
            fonts={'base.ftc':bytes([24]*230),**{name:b'synthetic atlas' for name in pack.ATLAS_NAMES}}
            class Archive:
                def __init__(self,_):pass
                def __enter__(self):return self
                def __exit__(self,*_):pass
                def read(self,name):
                    if name==pack.METRICS:return fonts['base.ftc']
                    if name.startswith(pack.ATLAS_ROOT):return fonts[name.removeprefix(pack.ATLAS_ROOT)]
                    return source_by_member[name]
            with patch.object(pack,'VbfArchive',Archive),patch.object(pack,'executable_identity',return_value=pack.EXE_SHA),\
                 patch.object(pack,'build_font',return_value=(fonts,[],{})),\
                 patch.object(pack,'ftc_layout',return_value=SimpleNamespace(metrics=0,count=230)):
                yield root,vbf,exe

    def test_explicit_api_does_not_silently_upgrade_for_preserved_controls(self):
        name='event/obj_ps3/ss/ssbt0000/ssbt0000.bin'
        source=struct.pack('<4H',8,0,8,0)+pack.encode('{CTRL:01}Original text')+b'\0'
        recipe=dict(locale='pt-BR',display_name='Portuguese',pack_version='1.0.0',
                    resources={name:[dict(row=0,slot=0,text='{CTRL:01}Info')]})
        with self.fixture({pack.MASTER+name:source}) as (root,vbf,exe):
            with self.assertRaisesRegex(AssetError,'API'):
                pack.build(vbf,root/'explicit',recipe,root/'reference',exe,hook_api=2)
            self.assertFalse((root/'explicit').exists());self.assertFalse((root/'reference').exists())
            pack.build(vbf,root/'inferred',recipe,executable=exe)
            self.assertEqual(json.loads((root/'inferred/manifest.json').read_text())['hook_api'],3)

    def test_graphic_input_location_cannot_select_output_or_reference_path(self):
        request='/ffx_data/gamedata/ps3data/menu_us/example.dds.phyre';member=request[1:]
        source=b'BASE';target=b'PTBR';relative='graphics/'+member
        catalog={'entries':[dict(request=request,size=4,source_sha256=pack.digest(source),sha256=pack.digest(target))]}
        recipe=dict(locale='pt-BR',display_name='Portuguese',pack_version='1.0.0',
                    resources={'menu_txt.bin':[dict(row=101,slot=0,text='Info')]})
        native={pack.KERNEL+'menu_txt.bin':indexed(16,4),member:source}
        with self.fixture(native) as (root,vbf,exe):
            compiled=root/'compiled';compiled.mkdir();input_file=compiled/'input.bin';input_file.write_bytes(target)
            metadata={'records':[dict(request=request,resource=member,path=str(input_file))]}
            (compiled/'graphics-manifest.json').write_text(json.dumps(metadata))
            original_read=Path.read_text;catalog_path=Path(pack.__file__).with_name('graphics_profiles.json')
            def read(path,*args,**kwargs):
                return json.dumps(catalog) if path==catalog_path else original_read(path,*args,**kwargs)
            with patch.object(Path,'read_text',read):
                pack.build(vbf,root/'output',recipe,root/'reference',exe,hook_api=4,graphics=compiled)
            resources=json.loads((root/'output/manifest.json').read_text())['resources']
            graphic=next(r for r in resources if r['family']=='ui_texture')
            self.assertEqual(graphic['path'],relative)
            self.assertEqual((root/'output'/relative).read_bytes(),target)
            self.assertEqual((root/'reference'/relative).read_bytes(),source)
            self.assertEqual(input_file.read_bytes(),target)

if __name__=='__main__':unittest.main()
