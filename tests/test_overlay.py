import importlib.util
import pathlib
import struct
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
            self.assertIn('clocks = <1 54>, <1 17>, <1 196>;', text)
            self.assertIn('amlogic,canvas = <2>;', text)
            self.assertIn('amlogic,ao-sysctrl = <3>;', text)
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


if __name__ == '__main__':
    unittest.main()
