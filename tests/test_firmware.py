import importlib.util
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('firmware', ROOT / 'scripts/extract-firmware.py')
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)


class FirmwareTests(unittest.TestCase):
    def test_words_are_little_endian_and_sentinel_is_not_copied(self):
        text = 'const u32 MicroCode[] __attribute__ ((aligned (8))) = {0x12345678,0x0};'
        self.assertEqual(fw.parse_words(text, 1), b'\x78\x56\x34\x12')

    def test_rejects_truncated_array_and_nonzero_sentinel(self):
        for words in ['0x1', '0x1,0x2', '0x1,arbitrary()', '0x100000000,0x0']:
            text = f'const u32 MicroCode[] __attribute__ ((aligned (8))) = {{{words}}};'
            with self.assertRaises(ValueError):
                fw.parse_words(text, 1)

    def test_matches_vendor_c_copy_sizes(self):
        # Independently compile the vendor arrays, then copy the sizes used by
        # amvdec_loadmc() and vh264_init(), instead of relying on our parser.
        code = '#include <stdint.h>\n#include <stdio.h>\ntypedef uint32_t u32;\n'
        arrays = [('c',4096),('header',1024),('data',1024),('mmc',1024),('list',1024),('slice',1024)]
        for name, _ in arrays:
            p = ROOT / f'sources/vendor/drivers/amlogic/amports/m8/ucode/h264/h264{name}_linux.h'
            code += f'#define MicroCode mc_{name}\n#include "{p}"\n#undef MicroCode\n'
        code += 'int main(void) {\n'
        for name, length in arrays:
            code += f'for (int i=0; i<{length}; ++i) for(int b=0; b<4; ++b) putchar((mc_{name}[i]>>(8*b))&255);\n'
        code += 'return ferror(stdout); }\n'
        with tempfile.TemporaryDirectory() as d:
            c = pathlib.Path(d) / 'check.c'
            c.write_text(code)
            binary = pathlib.Path(d) / 'check'
            subprocess.run(['cc','-Wall','-Werror',str(c),'-o',str(binary)],check=True)
            data = subprocess.check_output([str(binary)])
        self.assertEqual(data, (ROOT / 'firmware/meson/vdec/meson8b_h264.bin').read_bytes())
        self.assertEqual(len(data), 36864)


if __name__ == '__main__':
    unittest.main()
