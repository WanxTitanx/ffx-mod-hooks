"""Jarvis-HOOK: field authoring, control preservation and codec interoperability."""
import importlib
from pathlib import Path
import struct
import sys
import unittest
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from asset_io import AssetError
import pack


def field(first, second):
    data = bytearray(16)
    struct.pack_into('<HHHH', data, 0, 16, 0x140, 16, 0x140)
    data += first + b'\0'
    struct.pack_into('<HHHH', data, 8, len(data), 0, len(data), 0)
    return bytes(data + second + b'\0')


class FieldAuthoringTests(unittest.TestCase):
    def builder(self):
        method = getattr(pack, 'rebuild_field', None)
        self.assertIsNotNone(method, 'Field-table authoring must be implemented')
        return method

    def test_first_row_preserves_implicit_header(self):
        original = field(pack.encode('Formation configuration'), pack.encode('Information'))
        result = self.builder()(original, [{'row': 0, 'slot': 0, 'text': 'Formação'}])
        self.assertEqual(struct.unpack_from('<H', result)[0], 16)
        self.assertEqual(result[2:4], original[2:4])
        self.assertEqual(result[6:8], original[6:8])
        self.assertEqual(pack.script_at(result, 16), pack.encode('Formação'))
        self.assertEqual(pack.script_at(result, struct.unpack_from('<H', result, 4)[0]), pack.encode('Formation configuration'))
        self.assertEqual(pack.script_at(result, struct.unpack_from('<H', result, 8)[0]), pack.encode('Information'))

    def test_no_edit_preserves_suffix_sharing_and_padding(self):
        original = bytearray(field(pack.encode('Formation'), pack.encode('Information')))
        struct.pack_into('<H', original, 12, struct.unpack_from('<H', original, 8)[0] + 2)
        original += b'\0' * 13
        self.assertEqual(self.builder()(bytes(original), []), original)

    def test_edits_reject_invalid_identity_and_overflow(self):
        original = field(pack.encode('Formation configuration'), pack.encode('Information'))
        for edits in ([{'row': 3, 'slot': 0, 'text': 'A'}], [{'row': 0, 'slot': 2, 'text': 'A'}],
                      [{'row': 0, 'slot': 0, 'text': 'A'}] * 2, [{'row': True, 'slot': 0, 'text': 'A'}]):
            with self.subTest(edits=edits), self.assertRaises(AssetError):
                self.builder()(original, edits)

    def test_native_controls_and_numeric_placeholders_roundtrip(self):
        self.assertEqual(pack.encode('{CTRL:09:30}{VAR:4}\n{TIDUS}'), bytes((9,48,18,52,3,19,48)))
        self.assertEqual(pack.encode('{{}}'), bytes((138,140)))
        for invalid in ('{CTRL:01:30}', '{CTRL:09:00}', '{VAR:99}', '{CTRL:0A:00}', '{CTRL:09:30:31}'):
            with self.subTest(invalid=invalid), self.assertRaises(AssetError):
                pack.encode(invalid)

    def test_native_header_control_is_preserved(self):
        widths = bytes([24] * 230)
        before = bytes((9,48,18,48)) + pack.encode(' captured!')
        after = bytes((9,48,18,48)) + pack.encode(' salvo!')
        old_control, old_width = pack.measure(before, widths)
        new_control, new_width = pack.measure(after, widths)
        self.assertEqual(old_control, new_control)
        self.assertLessEqual(new_width[0], old_width[0])


    def test_byte_growth_does_not_hide_behind_narrow_glyph_width(self):
        from pack import rebuild_field, encode, validate_edits
        original = field(encode('WW'), encode('Original'))
        edits = [{'row': 0, 'slot': 0, 'text': 'iiiiiiii'}]
        target = rebuild_field(original, edits)
        widths = bytearray([24] * 230)
        widths[encode('W')[0] - 48] = 56
        widths[encode('i')[0] - 48] = 1
        with self.assertRaises(AssetError):
            validate_edits(original, target, edits, widths, field=True)

    def test_supported_codec_roundtrip(self):
        for text in ('Português (Brasil): ações, órgãos!', '{{}} 42%', '{CTRL:09:30}{VAR:4}\n{TIDUS}', 'ÁÉÍÓÚ ÃÕ'):
            encoded = pack.encode(text)
            self.assertEqual(pack.encode(pack.decode(encoded)), encoded)

    def test_publisher_never_replaces_an_existing_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder)/'source', Path(folder)/'target'
            source.mkdir(); target.mkdir()
            (source/'manifest.json').write_text('fixture')
            with self.assertRaises(FileExistsError):
                pack.publish_directory(source, target)
            self.assertTrue((source/'manifest.json').is_file())
            self.assertEqual(list(target.iterdir()), [])

    def test_recipe_rejects_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'recipe.json'
            source.write_text('{"locale":"pt-BR","locale":"native"}')
            with self.assertRaises(AssetError):
                pack.load_recipe(source)


if __name__ == '__main__':
    unittest.main()
