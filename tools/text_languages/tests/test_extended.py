"""API 3 producer contracts using synthetic, non-game data. Jarvis-HOOK."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pack
from asset_io import AssetError


def indexed(stride, slots):
    data = bytearray(20 + stride)
    struct.pack_into('<4H', data, 8, 101, 101, stride, stride)
    for slot in range(slots):
        struct.pack_into('<HH', data, 20 + slot * 4, 0, 0x100 + slot)
    data += pack.encode('Formation configuration') + b'\0'
    return bytes(data)


def chunk(first, second):
    data = bytearray(struct.pack('<4H', 8, 8, 0, 0))
    data += pack.encode(first) + b'\0'
    struct.pack_into('<HH', data, 4, len(data), len(data))
    return bytes(data + pack.encode(second) + b'\0')


class ExtendedAuthoringTests(unittest.TestCase):
    def layout(self, name):
        self.assertTrue(hasattr(pack, 'describe_resource'), 'API 3 resource catalogue is missing')
        return pack.describe_resource(name)

    def test_closed_catalogue_and_canonical_aliases(self):
        for name, stride, slots in [('item.bin',96,4),('command.bin',96,4),('a_ability.bin',108,4),
                ('important.bin',20,4),('monster2.bin',128,4),('monmagic1.bin',92,4),
                ('panel.bin',24,4),('sphere.bin',16,2),('w_name.bin',72,14)]:
            with self.subTest(name=name):
                layout = self.layout(name)
                self.assertEqual((layout.stride,layout.slots,layout.minimum_api),(stride,slots,3))
                self.assertEqual(layout, self.layout('battle/kernel/'+name))
        for bad in ['../item.bin','battle/kernel/ply_save.bin','battle/btl/foo/bar.bin',
                    'menu/albheddic.bin','lockit/ffx_loc_kit_ps3_us.bin','sound/voice.bin']:
            with self.subTest(bad=bad), self.assertRaises(AssetError):
                self.layout(bad)

    def test_indexed_edit_preserves_gameplay_flags_and_first_pool_string(self):
        layout = self.layout('command.bin')
        source = indexed(96,4)
        target = pack.rebuild_resource(source,[{'row':101,'slot':0,'text':'Info'}],layout)
        self.assertEqual(target[:20], source[:20])
        self.assertEqual(target[22:len(source)], source[22:])
        pointer = struct.unpack_from('<H',target,20)[0]
        self.assertEqual(pack.script_at(target,116+pointer),pack.encode('Info'))
        self.assertEqual(pack.script_at(source,116),pack.encode('Formation configuration'))

    def test_weapon_slots_do_not_write_model_words(self):
        source = indexed(72,14)
        target = pack.rebuild_resource(source,[{'row':101,'slot':13,'text':'Info'}],self.layout('w_name.bin'))
        self.assertEqual(target[76:92],source[76:92])
        with self.assertRaises(AssetError):
            pack.rebuild_resource(source,[{'row':101,'slot':14,'text':'A'}],self.layout('w_name.bin'))

    def test_battle_text_edit_preserves_metadata_between_two_references(self):
        layout = self.layout('btl_txt.bin')
        self.assertEqual((layout.stride, layout.slots, layout.offset_step), (8, 2, 4))
        source = indexed(8, 2)
        edit = {'row':101, 'slot':1, 'text':'Info'}
        for target in (pack.rebuild_resource(source, [edit], layout), pack.append_kernel(source, [edit])):
            self.assertEqual(target[20:24], source[20:24])
            self.assertEqual(target[26:len(source)], source[26:])
            self.assertEqual(pack.script_at(target, 28 + struct.unpack_from('<H', target, 24)[0]), pack.encode('Info'))
            pack.validate_edits(source, target, [edit], bytes([24] * 230), layout=layout)
            pack.validate_edits(source, target, [edit], bytes([24] * 230))
        with self.assertRaises(AssetError):
            pack.rebuild_resource(source, [{'row':101, 'slot':2, 'text':'A'}], layout)

    def test_help_only_tables_never_author_the_native_tail(self):
        for name in ('sphere.bin','btlend_txt.bin','build_txt.bin','name_txt.bin','save_txt.bin'):
            with self.subTest(name=name):
                layout=self.layout(name)
                source=bytearray(indexed(16,2))
                struct.pack_into('<4H',source,28,1,0x103,1,0)
                source=bytes(source)
                target=pack.rebuild_resource(source,[{'row':101,'slot':0,'text':'Info'}],layout)
                self.assertEqual(target[28:36],source[28:36])
                for slot in (2,3):
                    with self.assertRaises(AssetError):
                        pack.rebuild_resource(source,[{'row':101,'slot':slot,'text':'A'}],layout)

    def test_exact_stride_is_required(self):
        with self.assertRaises(AssetError):
            pack.rebuild_resource(indexed(92,4),[{'row':101,'slot':0,'text':'A'}],self.layout('item.bin'))

    def test_macro_first_row_aliases_and_later_chunk_survive_relocation(self):
        first = chunk('Formation configuration','Character information')
        second = chunk('Original chunk','More original text')
        source = bytearray(64)
        struct.pack_into('<II',source,24,64,64+len(first))
        source += first + second
        target = pack.rebuild_resource(bytes(source),[{'row':6*65536,'slot':0,'text':'Info'}],self.layout('menu/macrodic.dcp'))
        self.assertEqual(struct.unpack_from('<I',target,24)[0],64)
        self.assertEqual(struct.unpack_from('<H',target,64)[0],8)
        self.assertEqual(pack.script_at(target,72),pack.encode('Info'))
        alias = struct.unpack_from('<H',target,66)[0]
        self.assertEqual(pack.script_at(target,64+alias),pack.encode('Formation configuration'))
        later = struct.unpack_from('<I',target,28)[0]
        self.assertEqual(target[later:],second)
        self.assertEqual(pack.rebuild_resource(bytes(source),[],self.layout('menu/macrodic.dcp')),source)
        for bad in [{'row':5*65536,'slot':0,'text':'A'},{'row':True,'slot':0,'text':'A'}]:
            with self.assertRaises(AssetError):
                pack.rebuild_resource(bytes(source),[bad],self.layout('menu/macrodic.dcp'))

    def test_null_field_variant_is_preserved(self):
        source=bytearray(struct.pack('<8H',16,0x140,0,0x141,0,0,0,0))
        source+=pack.encode('Formation configuration')+b'\0'
        struct.pack_into('<HH',source,8,len(source),0)
        struct.pack_into('<HH',source,12,len(source),0)
        source+=pack.encode('Other')+b'\0'
        target=pack.rebuild_resource(bytes(source),[{'row':0,'slot':0,'text':'Info'}],self.layout('menu/menumain.bin'))
        self.assertEqual(struct.unpack_from('<HH',target,4),(0,0x141))
        with self.assertRaises(AssetError):
            pack.rebuild_resource(bytes(source),[{'row':0,'slot':1,'text':'Activate'}],self.layout('menu/menumain.bin'))

    def test_unknown_executable_cannot_produce_a_profile_claim(self):
        self.assertTrue(hasattr(pack,'executable_identity'))
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'FFX.exe'
            path.write_bytes(b'not a supported executable')
            with self.assertRaises(AssetError):
                pack.executable_identity(path)

    def test_reference_extractor_uses_the_same_versioned_catalogue(self):
        import reference
        for name in ('item.bin','w_name.bin','menu/menumain.bin','menu/macrodic.dcp',
                     'battle/btl/besa01_00/besa01_00.bin'):
            layout = self.layout(name)
            resource = {'family':layout.family,'request':layout.request}
            self.assertEqual(reference.archive_name(resource,3),layout.member)
            with self.assertRaises(AssetError):
                reference.archive_name(resource,2)
            with self.assertRaises(AssetError):
                reference.archive_name(resource|{'family':'events'},3)

    def test_api3_escape_controls_preserve_parameters_and_roundtrip(self):
        text='{CTRL:01}{CTRL:10:30}{CTRL:20:31}{CTRL:23:FF}{CTRL:0A:88}Info'
        raw=bytes((1,16,48,32,49,35,255,10,136))+pack.encode('Info')
        self.assertEqual(pack.encode(text),raw)
        self.assertEqual(pack.encode(pack.decode(raw)),raw)
        source=indexed(96,4)
        prefix=bytes((16,48,32,49))
        source=source[:116]+prefix+pack.encode('Formation configuration')+b'\0'
        edits=[{'row':101,'slot':0,'text':'{CTRL:10:30}{CTRL:20:31}Info'}]
        layout=self.layout('command.bin')
        target=pack.rebuild_resource(source,edits,layout)
        audit=pack.validate_edits(source,target,edits,bytes([24]*230),layout=layout)
        self.assertEqual(audit[0]['required_api'],3)
        for unsupported in ('{CTRL:04}','{CTRL:0E:30}','{CTRL:06:30}','{CTRL:10:00}'):
            with self.assertRaises(AssetError):pack.encode(unsupported)

    def test_repeated_translations_share_one_appended_string(self):
        source=indexed(96,4)
        edits=[{'row':101,'slot':slot,'text':'Info'} for slot in (0,1,2,3)]
        target=pack.rebuild_resource(source,edits,self.layout('command.bin'))
        self.assertEqual(len(target),len(source)+len(pack.encode('Info'))+1)
        self.assertEqual(len({struct.unpack_from('<H',target,20+slot*4)[0] for slot in range(4)}),1)

    def test_full_corpus_recipe_bound_is_separate_from_manifest_bound(self):
        import json
        self.assertTrue(hasattr(pack,'MAX_MANIFEST'))
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'large-recipe.json'
            value={'locale':'pt-BR','display_name':'Corpus','pack_version':'1.0.0',
                   'resources':{'menu/menumain.bin':[{'row':n,'slot':0,'text':'A'*80} for n in range(10000)]}}
            p.write_text(json.dumps(value))
            self.assertGreater(p.stat().st_size,1024*1024)
            self.assertEqual(len(pack.load_recipe(p)['resources']['menu/menumain.bin']),10000)
            with self.assertRaises(AssetError):pack.load_recipe(p,pack.MAX_MANIFEST)


if __name__ == '__main__':
    unittest.main()
