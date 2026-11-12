import struct,unittest,zlib
from test_native_qrc import fixture
from native_qrc import unpack,index_files,replace_data
from native_textures import jpeg_info,replace_jpeg_fixed,compare


def jpeg(width=512,channels=3,padding=16):
    # Header-only synthetic input, not a decodable photograph.
    components=b''.join(bytes([i+1,0x11,0]) for i in range(channels))
    frame=struct.pack('>BHHB',8,512,width,channels)+components
    scan=bytes([channels])+b''.join(bytes([i+1,0]) for i in range(channels))+b'\x00\x3f\x00'
    return (b'\xff\xd8\xff\xc0'+struct.pack('>H',len(frame)+2)+frame+
            b'\xff\xda'+struct.pack('>H',len(scan)+2)+scan+b'x'*padding+b'\xff\xd9')


class TextureTests(unittest.TestCase):
    def base(self):return replace_data(fixture('tile.jpg'),{'tile.jpg':jpeg(padding=40)})
    def test_inspect_dimensions_and_components(self):
        info=jpeg_info(jpeg());self.assertEqual(info['width'],512);self.assertEqual(len(info['components']),3)
    def test_truncated_segment_rejected(self):
        with self.assertRaises(ValueError):jpeg_info(b'\xff\xd8\xff\xc0\xff\xff\xff\xd9')
    def test_scan_before_frame_rejected(self):
        with self.assertRaises(ValueError):jpeg_info(b'\xff\xd8\xff\xda\x00\x02\xff\xd9')
    def test_smaller_image_preserves_layout(self):
        base=self.base();candidate=replace_jpeg_fixed(base,'tile.jpg',jpeg())
        old,tables=index_files(unpack(base));new,new_tables=index_files(unpack(candidate))
        self.assertEqual(len(unpack(base)),len(unpack(candidate)));self.assertEqual(tables,new_tables)
        self.assertEqual(new['tile.jpg']['data'],jpeg())
        a=struct.unpack_from('>I',unpack(base),old['tile.jpg']['reference'])[0]
        b=struct.unpack_from('>I',unpack(candidate),new['tile.jpg']['reference'])[0]
        self.assertEqual(a,b)
    def test_larger_image_refused(self):
        with self.assertRaisesRegex(ValueError,'exceeds'):replace_jpeg_fixed(self.base(),'tile.jpg',jpeg(padding=60))
    def test_changed_dimensions_refused(self):
        with self.assertRaisesRegex(ValueError,'profile'):replace_jpeg_fixed(self.base(),'tile.jpg',jpeg(width=256))
    def test_changed_component_count_refused(self):
        with self.assertRaisesRegex(ValueError,'profile'):replace_jpeg_fixed(self.base(),'tile.jpg',jpeg(channels=1))
    def test_comparison_does_not_claim_hardware(self):
        base=self.base();report=compare(base,replace_jpeg_fixed(base,'tile.jpg',jpeg()))
        self.assertEqual(report['raw_growth_bytes'],0);self.assertEqual(report['changed'],['tile.jpg'])
        self.assertFalse(report['hardware_compatibility_verified'])
    def test_overlapping_tables_refused(self):
        raw=bytearray(unpack(self.base()));tables=index_files(raw)[1]
        struct.pack_into('>II',raw,40,*tables[5])
        malformed=b'QRCC'+struct.pack('>I',len(raw))+zlib.compress(raw)
        with self.assertRaisesRegex(ValueError,'Overlapping QRC tables'):
            replace_jpeg_fixed(malformed,'tile.jpg',jpeg())
    def test_nonimage_replacement_refused(self):
        with self.assertRaisesRegex(ValueError,'Existing JPEG'):
            replace_jpeg_fixed(fixture('camera.path'),'camera.path',jpeg())


if __name__=='__main__':unittest.main()
