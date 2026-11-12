from pathlib import Path
import struct,sys,unittest,zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_qrc import index_files,unpack,replace_data

def fixture(name='demo.mnu'):
    strings=b'qrc\0file\0id\0src\0';ids=bytes(4)+name.encode()+b'\0'
    tree=struct.pack('>7i',0,0,-1,-1,-1,28,28)
    tree+=struct.pack('>7i',4,2,0,-1,-1,-1,-1)+struct.pack('>4I',9,7,0,0)+struct.pack('>4I',12,6,0,3)
    raw=bytearray(64);tables=[]
    for data in [tree,ids,strings,b'',b'',b'old']:
        raw.extend(bytes((-len(raw))%16));tables.extend([len(raw),len(data)]);raw.extend(data)
    raw.extend(bytes((-len(raw))%16));struct.pack_into('>4s13I',raw,0,b'QRCF',0x110,*tables)
    return b'QRCC'+struct.pack('>I',len(raw))+zlib.compress(raw)

class NativeQrcTests(unittest.TestCase):
    def test_append_preserves_original_container(self):
        before=fixture();after=replace_data(before,{'demo.mnu':b'new longer data'})
        files,_=index_files(unpack(after));self.assertEqual(files['demo.mnu']['data'],b'new longer data')
        self.assertEqual(index_files(unpack(before))[0]['demo.mnu']['data'],b'old')
    def test_unknown_path_refused(self):
        with self.assertRaises(ValueError):replace_data(fixture(),{'absent.mnu':b'new'})
    def test_shader_replacement_refused(self):
        with self.assertRaises(ValueError):replace_data(fixture('demo.fpo'),{'demo.fpo':b'new'})
    def test_empty_data_refused(self):
        with self.assertRaises(ValueError):replace_data(fixture(),{'demo.mnu':b''})
    def test_no_change_set_refused(self):
        with self.assertRaises(ValueError):replace_data(fixture(),{})
    def test_byte_identical_repeat(self):
        self.assertEqual(replace_data(fixture(),{'demo.mnu':b'new'}),replace_data(fixture(),{'demo.mnu':b'new'}))

if __name__=='__main__':unittest.main()
