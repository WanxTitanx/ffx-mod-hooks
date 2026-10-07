"""Native texture compilation invariants using synthetic pixels only."""
from pathlib import Path
import io,json,struct,sys,tempfile,unittest
from unittest.mock import patch
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from asset_io import AssetError,phyre_layout
from inspect_assets import texture_image
import graphics

def fixture(image,fmt):
    raw=io.BytesIO()
    if fmt=='ARGB8':payload=image.tobytes('raw','BGRA')
    else:
        image.save(raw,format='DDS',pixel_format=fmt);payload=raw.getvalue()[128:]
    hit=128;start=hit+11+len(fmt)+38;data=bytearray(start)
    data[:5]=b'RYHPT';struct.pack_into('<II',data,hit-88,image.width,image.height)
    data[hit:hit+11]=b'PTexture2D\0';data[hit+11:hit+11+len(fmt)+1]=fmt.encode()+b'\0'
    return bytes(data)+payload

class GraphicsTests(unittest.TestCase):
    def test_invalid_preview_uids_are_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);native=root/'game/data';native.mkdir(parents=True)
            vbf=native/'source.vbf';vbf.write_bytes(b'No archive IO should occur')
            inputs=root/'images';inputs.mkdir()
            for n,uid in enumerate((None,42,'','../escape','/escape','a/b','a\\b','CON','nul','trailing.','a:stream','A'*65)):
                with self.subTest(uid=uid):
                    (inputs/'resource-sources.jsonl').write_text(json.dumps(dict(uid=uid))+'\n')
                    output=root/f'output-{n}'
                    argv=['graphics.py','--vbf',str(vbf),'--images',str(inputs),'--output',str(output)]
                    with patch.object(sys,'argv',argv),self.assertRaisesRegex(AssetError,'UID'):
                        graphics.main()
                    self.assertFalse(output.exists())

    def test_preview_uid_group_reuses_one_png_without_portable_name_collisions(self):
        first=dict(uid='g001',candidate_png='pt-BR/g001.png',candidate_png_sha256='a'*64)
        graphics.validate_review_uids([first,dict(first,resource='second native alias')])
        for changed in (dict(first,uid='G001'),dict(first,candidate_png='other.png'),
                        dict(first,candidate_png_sha256='b'*64)):
            with self.subTest(changed=changed),self.assertRaisesRegex(AssetError,'UID'):
                graphics.validate_review_uids([first,changed])

    def test_bc3_near_opaque_strokes_do_not_lose_alpha(self):
        source=fixture(Image.new('RGBA',(8,8),(32,196,140,255)),'DXT5')
        image=texture_image(source).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        pixels=np.array(image)
        alpha=np.array([[254,213,0,0],[254,50,0,0],[172,0,0,0],[50,0,0,0]],dtype=np.uint8)
        pixels[:4,:4,3]=alpha
        target,_=graphics.rebuild_texture(source,Image.fromarray(pixels))
        actual=np.asarray(texture_image(target).transpose(Image.Transpose.FLIP_TOP_BOTTOM))[:4,:4,3]
        self.assertLessEqual(int(np.abs(actual.astype(int)-alpha.astype(int)).max()),1)
        self.assertTrue(np.all(actual[alpha==0]==0))

    def test_no_edit_is_byte_identical_for_each_admitted_format(self):
        for fmt in ['DXT1','DXT5','ARGB8']:
            with self.subTest(fmt=fmt):
                source=fixture(Image.new('RGBA',(8,8),(60,90,120,255)),fmt)
                image=texture_image(source).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                target,audit=graphics.rebuild_texture(source,image)
                self.assertEqual(target,source);self.assertEqual(audit['changed_blocks'],0)

    def test_bc3_alpha_change_preserves_every_color_word_and_other_blocks(self):
        source=fixture(Image.new('RGBA',(8,8),(32,196,140,255)),'DXT5')
        desired=texture_image(source).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        desired.putpixel((1,1),(*desired.getpixel((1,1))[:3],0))
        target,audit=graphics.rebuild_texture(source,desired);layout=phyre_layout(source)
        self.assertEqual(target[:layout.pixels],source[:layout.pixels])
        for off in range(layout.pixels,len(source),16):self.assertEqual(target[off+8:off+16],source[off+8:off+16])
        self.assertEqual(audit['changed_blocks'],1);self.assertEqual(audit['outside_changed_blocks'],0)
        self.assertEqual(texture_image(target).getpixel((1,6))[3],0)

    def test_uncompressed_pixels_roundtrip_exactly(self):
        source=fixture(Image.new('RGBA',(8,8),(7,8,9,10)),'ARGB8')
        desired=texture_image(source).transpose(Image.Transpose.FLIP_TOP_BOTTOM);desired.putpixel((2,3),(90,80,70,60))
        target,audit=graphics.rebuild_texture(source,desired)
        self.assertEqual(texture_image(target).transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes(),desired.tobytes())
        self.assertEqual(len(target),len(source));self.assertEqual(audit['outside_changed_blocks'],0)

    def test_dimension_and_unexamined_tail_changes_are_rejected(self):
        source=fixture(Image.new('RGBA',(8,8),(1,2,3,255)),'DXT5')
        with self.assertRaises(AssetError):graphics.rebuild_texture(source,Image.new('RGBA',(12,8)))
        with self.assertRaises(AssetError):graphics.rebuild_texture(source+b'opaque',Image.new('RGBA',(8,8)))

if __name__=='__main__':unittest.main()
