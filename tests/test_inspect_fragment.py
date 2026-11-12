import struct,unittest
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inspect_fragment import inspect_fragment


def fixture(opcode=0x17,constant=False,end=True):
    dest=(opcode<<24)|(3<<17)|int(end)
    words=(dest,2 if constant else 1,0,0)
    code=struct.pack('>4I',*[((w<<16)&0xffffffff)|(w>>16) for w in words])
    if constant:code+=bytes(16)
    return struct.pack('>8I',7004,6,64+len(code),0,32,32,len(code),64)+bytes(32)+code


class FragmentTests(unittest.TestCase):
    def test_texture_sampler(self):
        result=inspect_fragment(fixture());self.assertEqual(result['texture_read_instructions'],1)
        self.assertEqual(result['sampler_indices'],[3]);self.assertFalse(result['runtime_binding_verified'])
    def test_inline_constant_is_not_an_instruction(self):
        result=inspect_fragment(fixture(1,constant=True))
        self.assertEqual(result['instructions'],1);self.assertEqual(result['texture_read_instructions'],0)
    def test_inline_constant_truncation(self):
        data=bytearray(fixture(1,constant=True));data=data[:-16]
        struct.pack_into('>I',data,8,len(data));struct.pack_into('>I',data,24,16)
        with self.assertRaisesRegex(ValueError,'Inline constant'):inspect_fragment(data)
    def test_missing_end(self):
        with self.assertRaisesRegex(ValueError,'No end'):inspect_fragment(fixture(end=False))
    def test_wrong_profile(self):
        data=bytearray(fixture());struct.pack_into('>I',data,0,7003)
        with self.assertRaisesRegex(ValueError,'profile'):inspect_fragment(data)
    def test_declared_size(self):
        with self.assertRaisesRegex(ValueError,'size differs'):inspect_fragment(fixture()+bytes(16))
    def test_code_outside_file(self):
        data=bytearray(fixture());struct.pack_into('>I',data,24,32)
        with self.assertRaisesRegex(ValueError,'Microcode bounds'):inspect_fragment(data)
    def test_uninspected_trailing_instructions(self):
        data=bytearray(fixture());data+=bytes(16)
        struct.pack_into('>I',data,8,len(data));struct.pack_into('>I',data,24,32)
        with self.assertRaisesRegex(ValueError,'after end'):inspect_fragment(data)


if __name__=='__main__':unittest.main()
