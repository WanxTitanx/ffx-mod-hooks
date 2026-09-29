import struct
import unittest
from commands import Bank,SPELLS,build,encode

def fixture():
    pool=bytearray(b'\0');rows=[]
    for id in range(370):
        row=bytearray(96);name='Radiant Ward' if id==320 else 'Umbral Ward' if id==321 else f'Command {id}'
        struct.pack_into('<H',row,0,len(pool));pool.extend(encode(name));rows.append(row)
    for id in (320,321):
        rows[id][37]=2;rows[id][43]=1
    header=bytearray(20);struct.pack_into('<H',header,0,1);struct.pack_into('<HHHHI',header,8,0,369,96,370*96,20)
    return bytes(header)+b''.join(rows)+bytes(pool)

class Commands(unittest.TestCase):
    def test_growth_preserves_other_rows_and_text(self):
        before=fixture();after,receipt=build(before);a,b=Bank(before),Bank(after)
        self.assertEqual(b.count,374);self.assertEqual(receipt['unchanged_other_rows'],368)
        self.assertTrue(b.pool.startswith(a.pool))
        for id in range(370):
            if id not in (320,321):self.assertEqual(a.rows[id],b.rows[id])
        self.assertEqual([b.name(s.id) for s in SPELLS],[s.name for s in SPELLS])
        self.assertEqual(build(after)[0],after)

    def test_occupied_or_duplicate_identity_rejected(self):
        before=fixture();bank=Bank(before);rows=list(bank.rows);rows[50]=rows[320]
        with self.assertRaisesRegex(ValueError,'Duplicate'):build(before[:20]+b''.join(rows)+bank.pool)

    def test_modified_existing_cost_is_not_accepted(self):
        data=bytearray(build(fixture())[0]);data[20+320*96+37]=99
        with self.assertRaisesRegex(ValueError,'differs'):build(bytes(data))

    def test_unknown_layout_and_bad_text_are_rejected(self):
        data=bytearray(fixture());struct.pack_into('<H',data,12,95)
        with self.assertRaises(ValueError):build(bytes(data))
        data=bytearray(fixture());struct.pack_into('<H',data,20,65535)
        with self.assertRaises(ValueError):build(bytes(data))

    def test_explicit_caster_scope(self):
        data,_=build(fixture(),'all');bank=Bank(data)
        for spell in SPELLS:self.assertEqual((bank.rows[spell.id][25],bank.rows[spell.id][24]),(255,2))
        with self.assertRaises(ValueError):build(data,'yuna')

if __name__=='__main__':unittest.main(verbosity=2)
