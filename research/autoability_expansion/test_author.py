"""Offline tests of table authoring; no live game assets are written."""
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import author


def fixture(stride, tail=b"\0original\0"):
    header=bytearray(20)
    struct.pack_into('<I',header,0,1)
    struct.pack_into('<4H',header,8,0,147,stride,148*stride)
    struct.pack_into('<I',header,16,20)
    rows=bytearray(148*stride)
    if stride==108: rows[108+0x11]=0x80
    else: struct.pack_into('<I',rows,4,123456)
    return bytes(header+rows+tail)


class AuthorTests(unittest.TestCase):
    def setUp(self):
        self.raw=json.loads(author.DEFINITIONS.read_text())
        self.rows=author.validate_definitions(self.raw)

    def test_latin_growth_preserves_existing_data_and_pool(self):
        old=fixture(108);new=author.build(old,'a_ability','new_uspc',self.rows)
        self.assertEqual(new[20:20+148*108],old[20:20+148*108])
        self.assertEqual(new[20+175*108:20+175*108+len(old[20+148*108:])],old[20+148*108:])
        author.header(new,108,174)

    def test_only_declared_effect_bytes_are_set(self):
        new=author.build(fixture(108),'a_ability','new_uspc',self.rows)
        for row in self.rows:
            got=new[20+row['id']*108:20+(row['id']+1)*108]
            self.assertEqual(got[16:],author.payload(row)[16:])
            if row['allowed_owners'] is not None:self.assertFalse(any(got[16:]))
        self.assertEqual(struct.unpack_from('<H',new,20+155*108+0x64)[0],0x1000)
        self.assertEqual(struct.unpack_from('<H',new,20+156*108+0x64)[0],0x2000)
        self.assertEqual(struct.unpack_from('<H',new,20+151*108+0x64)[0],0x600)

    def test_cjk_numeric_names(self):
        for locale in author.ASIAN:
            new=author.build(fixture(108),'a_ability',locale,self.rows);pool=new[20+175*108:]
            for row in self.rows:
                at=20+row['id']*108;offset=struct.unpack_from('<H',new,at)[0]
                self.assertEqual(pool[offset:].split(b'\0')[0],str(row['id']).encode())

    def test_rates_preserve_existing_values_and_suffix(self):
        old=fixture(4,b'trailer');new=author.build(old,'arms_rate','jppc',self.rows)
        self.assertEqual(old[20:20+148*4],new[20:20+148*4])
        self.assertFalse(any(new[20+148*4:20+175*4]));self.assertTrue(new.endswith(b'trailer'))

    def test_rejects_invalid_headers_and_truncation(self):
        for at,value in ((8,1),(10,146),(12,107),(14,10),(16,24)):
            data=bytearray(fixture(108));struct.pack_into('<H',data,at,value)
            with self.assertRaises(ValueError):author.build(bytes(data),'a_ability','new_uspc',self.rows)
        with self.assertRaises(ValueError):author.build(fixture(108)[:25],'a_ability','new_uspc',self.rows)

    def test_rejects_text_pool_overflow(self):
        with self.assertRaises(ValueError):author.build(fixture(108,b'\0'*65530),'a_ability','new_uspc',self.rows)

    def test_rejects_unknown_locale_and_unsupported_glyph(self):
        with self.assertRaises(ValueError):author.build(fixture(108),'a_ability','new_ptpc',self.rows)
        with self.assertRaises(ValueError):author.encode('unsupported \N{SNOWMAN}')

    def test_rejects_duplicate_or_colliding_ids(self):
        raw=copy.deepcopy(self.raw);raw['definitions'][0]['id']=147
        with self.assertRaises(ValueError):author.validate_definitions(raw)
        raw=copy.deepcopy(self.raw);raw['definitions'][1]['key']=raw['definitions'][0]['key']
        with self.assertRaises(ValueError):author.validate_definitions(raw)

    def test_exclusive_payload_must_remain_neutral(self):
        raw=copy.deepcopy(self.raw);raw['definitions'][0]['writes']={'flags64':0x600}
        with self.assertRaises(ValueError):author.validate_definitions(raw)

    def test_rejects_unsupported_fields_and_ranges(self):
        for writes in ({'owner':2},{'stat_amount':256},{'flags64':65536},{'stat_amount':-1}):
            with self.assertRaises(ValueError):author.payload({'writes':writes})

    def test_atomic_write_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'sample';p.write_bytes(b'before')
            author.atomic_write(p,b'after',0o600)
            self.assertEqual(p.read_bytes(),b'after');self.assertEqual(p.stat().st_mode&0o777,0o600)
            self.assertEqual(len(list(Path(d).iterdir())),1)

    def test_plan_refuses_drift_before_writing(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); rows=[]
            for i in range(25):
                kind='a_ability' if i<13 else 'arms_rate';data=fixture(108 if i<13 else 4)
                p=root/f'{i}.bin';p.write_bytes(data)
                rows.append({'path':str(p),'kind':kind,'locale':'new_uspc','group':'fixture',
                             'size':len(data),'sha256':author.digest(data),'mode':0o600})
            inv=root/'inventory.json';inv.write_text(json.dumps({'targets':rows}))
            changed=root/'0.bin';changed.write_bytes(changed.read_bytes()+b'changed')
            observed={p:p.read_bytes() for p in root.glob('*.bin')}
            with patch.object(author,'INVENTORY',inv):
                with self.assertRaises(ValueError):author.plan()
            self.assertEqual(observed,{p:p.read_bytes() for p in root.glob('*.bin')})

    def test_rollback_rejects_later_edits_and_restores_exactly(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); records=[]
            for i in range(25):
                p=root/f'target{i}';p.write_bytes(b'after')
                b=root/f'backup{i}';b.write_bytes(b'before')
                records.append({'path':str(p),'backup':b.name,'mode':0o600,
                                'before_sha256':author.digest(b'before'),'after_sha256':author.digest(b'after')})
            mp=root/'manifest.json';mp.write_text(json.dumps({'schema':1,'records':records}))
            (root/'target0').write_bytes(b'later edit')
            with patch.object(author,'targets',lambda:records),patch.object(author,'require_apps_closed',lambda:None):
                with self.assertRaises(ValueError):author.rollback(mp)
                self.assertEqual((root/'target1').read_bytes(),b'after')
                (root/'target0').write_bytes(b'after');author.rollback(mp)
            self.assertTrue(all(Path(x['path']).read_bytes()==b'before' for x in records))


if __name__=='__main__':unittest.main()
