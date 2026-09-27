import ctypes as C
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'host'))
from bridge import Core
class NativeProgressionTests(unittest.TestCase):
 def test_native_customize_reader_is_bounded_and_matches_menu(self):
  gate=getattr(Core().dll,'ws_customize_unlocked',None)
  self.assertIsNotNone(gate,'The shared native Customize admission accessor is missing')
  gate.argtypes=[C.c_uint];gate.restype=C.c_uint
  for n,expected in ((0,1),(1,0),(0x447,0),(0x448,1),(0x449,1),(65535,1),(65536,0),(0xFFFFFFFF,0)):
   self.assertEqual(gate(n),expected)
 def test_runtime_signatures_include_native_customize_reader(self):
  root=Path(__file__).resolve().parents[3]
  header=(root/'src/runtime/FfxHooksDll/hooks/EquipmentWorkshopEvidence.h').read_text()
  for rva in ('0x4E1DF4u','0x46C400u','0x48C7A0u','0x385300u'):
   self.assertIn(rva,header)
 def test_pinned_menu_condition_and_dispatch_are_unchanged(self):
  source=os.environ.get('WORKSHOP_NATIVE_PE_FIXTURE')
  if not source:self.skipTest('Private supported PE not supplied')
  data=Path(source).read_bytes()
  self.assertEqual(hashlib.sha256(data).hexdigest(),'78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced')
  pe=struct.unpack_from('<I',data,60)[0];count=struct.unpack_from('<H',data,pe+6)[0];optional=struct.unpack_from('<H',data,pe+20)[0];sections=[]
  for i in range(count):
   size,rva,rawsize,raw=struct.unpack_from('<4I',data,pe+24+optional+40*i+8);sections.append((rva,max(size,rawsize),raw))
  def read(va,size):
   target=va-0x400000;off=next(raw+target-start for start,length,raw in sections if start<=target<start+length);return data[off:off+size]
  self.assertEqual(read(0x8E1DF4,18).hex(),'85ff740881ff480400007c0681ce80000000')
  self.assertEqual(read(0x86C400,13).hex(),'e89b0302000fb780ec0b0000c3')
  self.assertEqual(read(0x88C7A0,5).hex(),'e95b8befff')
  self.assertEqual(read(0x785300,6).hex(),'b890ca1201c3')
  self.assertEqual(read(0x8E2617,1),b'\x05')
  self.assertEqual(struct.unpack('<I',read(0x8E25F8,4))[0],0x8E25CA)
  self.assertEqual(read(0x8E25CA,9).hex(),'6a006a09e8dd7afcff')
  self.assertEqual(read(0x8AA812,7).hex(),'68fca7c5006a09')
if __name__=='__main__':unittest.main()
