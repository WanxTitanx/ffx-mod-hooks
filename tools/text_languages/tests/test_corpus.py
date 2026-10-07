"""Offline corpus contracts; fixtures contain only synthetic text. Jarvis-HOOK."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))


class CorpusTests(unittest.TestCase):
    def module(self):
        self.assertIsNotNone(importlib.util.find_spec('corpus'), 'Offline corpus reader is missing')
        import corpus
        return corpus

    def test_kernel_absolute_row_ids_aliases_flags_and_empty_string(self):
        c = self.module()
        data = bytearray(36)
        struct.pack_into('<HHHH', data, 8, 101, 101, 16, 16)
        struct.pack_into('<8H', data, 20, 1, 7, 1, 9, 0, 11, 2, 13)
        data += b'\0\x50\x51\0'
        rows = c.parse_kernel(bytes(data), 'monster2.bin')
        self.assertEqual([(r['row'], r['slot'], r['flags'], r['raw_hex']) for r in rows],
                         [(101, 0, 7, '5051'), (101, 1, 9, '5051'),
                          (101, 2, 11, ''), (101, 3, 13, '51')])
        self.assertFalse(rows[2]['null_pointer'])

    def test_zero_kernel_offset_is_first_string_not_a_null_pointer(self):
        c = self.module()
        data = bytearray(36)
        struct.pack_into('<4H', data, 8, 0, 0, 16, 16)
        data += bytes([95, 126, 131, 120, 126, 125, 0])
        rows = c.parse_kernel(bytes(data), 'item.bin')
        self.assertEqual(rows[0]['text'], 'Potion')
        self.assertFalse(rows[0]['null_pointer'])

    def test_battle_text_metadata_is_not_a_string_offset(self):
        c = self.module()
        data = bytearray(28)
        struct.pack_into('<4H', data, 8, 0, 0, 8, 8)
        # Both metadata values deliberately land inside a valid string. A
        # bounds-only parser would expose its suffixes as invented messages.
        struct.pack_into('<4H', data, 20, 0, 2, 5, 3)
        data += c.restore_preview('ABCD') + b'\0' + c.restore_preview('EF') + b'\0'
        rows = c.parse_kernel(bytes(data), 'btl_txt.bin')
        self.assertEqual([(r['slot'], r['text'], r['flags']) for r in rows],
                         [(0, 'ABCD', 2), (1, 'EF', 3)])

    def test_sphere_gameplay_tail_and_nonlocalized_ui_tails_are_not_text(self):
        c = self.module()
        for name in ('sphere.bin', 'btlend_txt.bin', 'build_txt.bin', 'name_txt.bin', 'save_txt.bin'):
            with self.subTest(name=name):
                data = bytearray(36)
                struct.pack_into('<4H', data, 8, 0, 0, 16, 16)
                struct.pack_into('<8H', data, 20, 0, 277, 5, 25, 1, 0x103, 1, 0)
                data += c.restore_preview('ABCD') + b'\0' + c.restore_preview('-') + b'\0'
                rows = c.parse_kernel(bytes(data), name)
                self.assertEqual([(r['slot'], r['text']) for r in rows], [(0, 'ABCD'), (1, '-')])

    def test_field_null_variant_and_shared_pointer(self):
        c = self.module()
        data = struct.pack('<8H', 16, 0x300, 16, 0x301, 0, 12, 18, 13) + b'\x50\0\x51\0'
        rows = c.parse_field(data)
        self.assertEqual([r['raw_hex'] for r in rows], ['50', '50', '', '51'])
        self.assertEqual(rows[1]['flags'], 0x301)
        self.assertTrue(rows[2]['null_pointer'])

    def test_bad_offsets_and_missing_terminators_rejected(self):
        c = self.module()
        for data in (b'', struct.pack('<4H', 8, 0, 7, 0) + b'A\0',
                     struct.pack('<4H', 8, 0, 8, 0) + b'A'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                c.parse_field(data)

    def test_weapon_model_words_are_not_text_references(self):
        c = self.module()
        data = bytearray(92)
        struct.pack_into('<HHHH', data, 8, 0, 0, 72, 72)
        for slot in range(14):
            struct.pack_into('<HH', data, 20 + 4 * slot, 1, slot)
        data[76:92] = b'\xff' * 16
        data += b'\0\x50\0'
        self.assertEqual(len(c.parse_kernel(bytes(data), 'w_name.bin')), 14)

    def test_preview_preserves_every_byte_and_escapes_literal_braces(self):
        c = self.module()
        raw = bytes([0x50, 3, 19, 48, 0xF0, 0x8A, 0x8C, 0x10, 0x31])
        preview = c.preview(raw)
        self.assertEqual(c.restore_preview(preview['text']), raw)
        self.assertIn('{CTRL:13:30}', preview['text'])
        self.assertTrue(preview['opaque'])

    def test_unknown_resource_never_guessed_as_a_text_table(self):
        c = self.module()
        self.assertIsNone(c.family_for('ffx_data/texture.bin'))
        self.assertIsNone(c.family_for(c.MASTER + 'battle/kernel/ply_save.bin'))

    def test_legacy_tilde_profile_does_not_change_native_umlauts(self):
        c = self.module()
        raw = bytes([166, 180, 189, 203])
        native = c.preview(raw)
        legacy = c.preview(raw, legacy=True)
        self.assertEqual(native['text'], 'ÄÖäö')
        self.assertEqual(legacy['text'], 'ÃÕãõ')
        self.assertEqual(c.restore_preview(legacy['text'], legacy=True), raw)

    def test_unused_out_of_pool_kernel_reference_is_preserved_as_issue(self):
        c = self.module()
        data = bytearray(36)
        struct.pack_into('<4H', data, 8, 0, 0, 16, 16)
        struct.pack_into('<8H', data, 20, 1, 0, 1, 0, 65535, 27, 0, 0)
        data += b'\0\x50\0'
        rows = c.parse_kernel(bytes(data), 'menu_txt.bin')
        self.assertEqual(rows[0]['raw_hex'], '50')
        self.assertEqual(rows[2]['offset'], 36 + 65535)
        self.assertTrue(rows[2]['invalid_pointer'])

    def test_zero_filled_placeholder_is_empty_not_an_invented_text(self):
        self.assertEqual(self.module().parse_field(bytes(168)), [])

    def test_macro_chunk_ids_aliases_and_bounds(self):
        c = self.module()
        data = bytearray(64)
        struct.pack_into('<I', data, 6 * 4, 64)
        data += struct.pack('<HH', 4, 4) + b'\x50\0'
        rows = c.parse_macro(bytes(data))
        self.assertEqual([(r['row'], r['slot'], r['raw_hex']) for r in rows],
                         [(393216, 0, '50'), (393216, 1, '50')])
        self.assertEqual(rows[0]['macro_id'], 1536)
        struct.pack_into('<H', data, 66, 3)
        with self.assertRaises(ValueError):
            c.parse_macro(bytes(data))

    def test_macro_chunk_can_exceed_256_entries_without_id_collision(self):
        c = self.module()
        data = bytearray(64)
        struct.pack_into('<I', data, 11 * 4, 64)
        data += struct.pack('<HH', 1044, 1044) * 261 + b'\x50\0'
        rows = c.parse_macro(bytes(data))
        self.assertEqual(len(rows), 522)
        self.assertEqual(len({(r['row'], r['slot']) for r in rows}), 522)

    def test_macro_invalid_variant_does_not_discard_valid_sibling(self):
        c = self.module()
        data = bytearray(64)
        struct.pack_into('<I', data, 6 * 4, 64)
        data += struct.pack('<HH', 4, 65535) + b'\x50\0'
        rows = c.parse_macro(bytes(data))
        self.assertEqual(rows[0]['text'], 'A')
        self.assertTrue(rows[1]['invalid_pointer'])

    def test_macro_zero_variant_stays_inactive_during_extraction(self):
        c=self.module();data=bytearray(64)
        struct.pack_into('<I',data,0,64)
        data+=struct.pack('<HH',4,0)+b'\x50\0'
        rows=c.parse_macro(bytes(data))
        self.assertEqual(rows[0]['text'],'A')
        self.assertTrue(rows[1]['null_pointer'])
        self.assertFalse(rows[1]['invalid_pointer'])
        self.assertEqual((rows[1]['offset'],rows[1]['raw_hex'],rows[1]['text']),(0,'',''))

    def test_lockit_line_identity_retains_empty_rows_and_bytes(self):
        c = self.module()
        rows = c.parse_lockit(b'\r\n\x50\x03\x51\r\n')
        self.assertEqual([r['raw_hex'] for r in rows], ['', '500351', ''])
        self.assertEqual([r['offset'] for r in rows], [0, 2, 7])
        self.assertEqual(rows[1]['text'], 'A\nB')
        with self.assertRaises(ValueError):
            c.parse_lockit(b'\x50\n\x51')

    def test_lockit_exposes_candidates_without_claiming_one_global_codec(self):
        row = self.module().parse_lockit(b'Keyboard')[0]
        self.assertEqual(row['utf8_candidate'],'Keyboard')
        self.assertEqual(row['encoding'],'mixed_unresolved')
        self.assertNotEqual(row['text'],'Keyboard')

    def test_output_cannot_overwrite_inputs_or_existing_work(self):
        c = self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / 'game'
            (game / 'data').mkdir(parents=True)
            vbf = game / 'data/source.vbf'
            vbf.write_bytes(b'source')
            (game / 'FFX.exe').write_bytes(b'sentinel')
            legacy = root / 'legacy'
            legacy.mkdir()
            for dest in (game / 'review', legacy / 'review', legacy):
                with self.subTest(dest=dest), self.assertRaises(ValueError):
                    c.check_destination(vbf, legacy, dest)
            c.check_destination(vbf, legacy, root / 'new')
            self.assertEqual(vbf.read_bytes(), b'source')


if __name__ == '__main__':
    unittest.main()
