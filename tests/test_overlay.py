import importlib.util
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('overlay', ROOT / 'scripts/make-overlay.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class OverlayTests(unittest.TestCase):
    def fixture(self, root):
        (root / 'compatible').write_bytes(b'xunlei,onecloud\0amlogic,meson8b\0')
        (root / 'soc').mkdir()
        for name in ['#address-cells', '#size-cells']:
            (root / 'soc' / name).write_bytes(struct.pack('>I', 1))
        for i, name in enumerate(['clkc', 'canvas', 'pmu', 'reset'], 1):
            node = root / 'soc' / name
            node.mkdir()
            (node / 'compatible').write_bytes(('amlogic,meson8b-' + name).encode() + b'\0')
            (node / 'phandle').write_bytes(struct.pack('>I', i))

    def test_resolves_live_provider_handles(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.fixture(root)
            text = overlay.render(root)
            self.assertIn('clocks = <1 54>, <1 17>, <1 196>, <1 206>, <1 8>, <1 192>, <1 9>;', text)
            self.assertIn('amlogic,canvas = <2>;', text)
            self.assertIn('amlogic,ao-sysctrl = <3>;', text)
            self.assertEqual(text.count('amlogic,ao-sysctrl = <3>;'), 2)
            self.assertIn('resets = <4 40>;', text)
            self.assertIn('0xd0050000 0x10000', text)

    def test_rejects_wrong_board_duplicate_decoder_and_provider(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.fixture(root)
            (root / 'compatible').write_bytes(b'other,board\0')
            with self.assertRaises(ValueError):
                overlay.render(root)
            (root / 'compatible').write_bytes(b'xunlei,onecloud\0amlogic,meson8b\0')
            duplicate = root / 'soc' / 'decoder'
            duplicate.mkdir()
            (duplicate / 'compatible').write_bytes(b'amlogic,meson8b-vdec\0')
            with self.assertRaises(ValueError):
                overlay.render(root)
            (duplicate / 'compatible').write_bytes(b'amlogic,meson8b-canvas\0')
            with self.assertRaises(ValueError):
                overlay.render(root)

    def test_ge2d_compiles_with_local_references_without_exported_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.fixture(root)
            source = root / 'overlay.dts'
            output = root / 'overlay.dtbo'
            source.write_text(overlay.render(root, ge2d=True))
            # Use the project's exact dtbo recipe with its output path redirected.
            recipe = next(line.strip() for line in (ROOT / 'Makefile').read_text().splitlines()
                          if line.startswith('\t$(DTC) '))
            args = recipe.replace('$(DTC)', 'dtc').replace(
                'firmware/meson/vdec/meson8b-runtime.dtbo', str(output)).replace('$<', str(source))
            subprocess.run(args.split(), check=True, capture_output=True)
            dumped = subprocess.check_output(['dtc', '-I', 'dtb', '-O', 'dts', str(output)],
                                             text=True, stderr=subprocess.DEVNULL)
            self.assertNotIn('__symbols__', dumped)
            self.assertIn('__local_fixups__', dumped)
            self.assertNotIn('__fixups__', dumped)
            self.assertIn('amlogic,ge2d', dumped)

    def test_ge2d_is_explicit_and_uses_the_existing_clock_provider(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.fixture(root)
            self.assertNotIn('blitter@', overlay.render(root))
            text = overlay.render(root, ge2d=True)
            self.assertIn('reg = <0xc1106280 0x12c>;', text)
            self.assertIn('clocks = <1 49>;', text)
            self.assertIn('resets = <4 70>;', text)
            self.assertIn('ge2d: blitter@c1106280', text)
            self.assertIn('amlogic,ge2d = <&ge2d>;', text)
            self.assertNotIn('amlogic,ge2d =', overlay.render(root))


if __name__ == '__main__':
    unittest.main()
