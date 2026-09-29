import struct
import unittest
from animations import clone_dll,tint,sha

class Animation(unittest.TestCase):
    def test_dxt5_preserves_header_indices_and_alpha_mode(self):
        header=b'private header';block=bytes([200,80])+bytes(range(6))+struct.pack('<HH',65535,31000)+bytes(range(4))
        data=header+block;layout={'bytes':len(data),'sha256':sha(data),'width':1,'height':1,'payload':len(header)}
        output=tint(data,layout,(.3,.8,1.2,.75));self.assertNotEqual(output,data)
        self.assertEqual(output[:len(header)],header);self.assertGreater(output[len(header)],output[len(header)+1])
        self.assertEqual(output[len(header)+2:len(header)+8],block[2:8]);self.assertEqual(output[-4:],block[-4:])
        with self.assertRaises(ValueError):tint(data+b'x',layout,(1,1,1,1))

    def test_clone_is_bounded_to_identity_strings(self):
        name='magic_0146'.encode('utf-16le');data=b'native code!'+name+b'\0\0'+name+b'\0\0'+name
        recipe={'dll_bytes':len(data),'dll_sha256':sha(data),'donor_magic':146,'text_section':{'offset':0,'bytes':12,'sha256':sha(data[:12])}}
        result=clone_dll(data,recipe,870);self.assertEqual(result[:12],data[:12]);self.assertEqual(len(result),len(data))
        self.assertEqual(result.count('magic_0870'.encode('utf-16le')),3)
        with self.assertRaises(ValueError):clone_dll(data+b'x',recipe,870)

if __name__=='__main__':unittest.main(verbosity=2)
