"""Legacy codec acceptance must check layout, sequence and all visible planes."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/m4v-frame-compare.py'
spec = importlib.util.spec_from_file_location('legacy_compare', SCRIPT)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class LegacyFrameComparisonTests(unittest.TestCase):
    def run_comparison(self, hardware, reference, *arguments):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'hardware').write_bytes(hardware)
            (base / 'reference').write_bytes(reference)
            return subprocess.run([sys.executable, str(SCRIPT), str(base / 'hardware'),
                                   str(base / 'reference'), *arguments], capture_output=True, text=True)

    def signatures(self, values):
        return ''.join(' '.join([str(value)] * 64) + '\n' for value in values).encode()

    def picture(self, width, height, identity=0, alignment=32):
        luma, chroma = compare.canvas_planes(width, height, alignment)
        stride = compare.align(width, alignment)
        hardware = bytearray([0xa5] * (luma + chroma))
        y = bytes((x * 7 + row * 3 + identity * 31) % 256
                  for row in range(height) for x in range(width))
        u, v = bytes([53 + identity]) * (width * height // 4), \
            bytes([183 - identity]) * (width * height // 4)
        for row in range(height):
            hardware[row * stride:row * stride + width] = y[row * width:(row + 1) * width]
        uv = bytes(value for pair in zip(u, v) for value in pair)
        for row in range(height // 2):
            hardware[luma + row * stride:luma + row * stride + width] = \
                uv[row * width:(row + 1) * width]
        return bytes(hardware), y + u + v, y + uv

    def test_reordered_signatures_and_empty_inputs_fail(self):
        result = self.run_comparison(self.signatures([150, 80, 10]), self.signatures([10, 80, 150]))
        self.assertEqual(result.returncode, 1)
        self.assertIn('out_of_order=[1, 2]', result.stdout)
        for hardware, reference in ((b'', b''), (b'', self.signatures([10]))):
            self.assertEqual(self.run_comparison(hardware, reference).returncode, 1)

    def test_current_and_historical_stride_match_their_own_visible_planes(self):
        for alignment in (32, 64):
            hardware, reference, nv12 = self.picture(854, 480, alignment=alignment)
            arguments = ('--canvas', '--size', '854x480', '--canvas-align', str(alignment))
            result = self.run_comparison(hardware, reference, *arguments)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn('pixel_checked=True', result.stdout)
            result = self.run_comparison(hardware, nv12, *arguments, '--ref-format', 'nv12')
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertEqual(compare.visible_planes(hardware, 854, 480, True, alignment),
                             compare.visible_planes(reference, 854, 480, False))

    def test_corrupt_chroma_or_equal_mean_luma_cannot_pass_grid_signature(self):
        hardware, reference, _ = self.picture(32, 16)
        bad = bytearray(hardware)
        luma = compare.canvas_planes(32, 16)[0]
        for offset in range(luma, luma + 32 * 8, 2):
            bad[offset], bad[offset + 1] = bad[offset + 1], bad[offset]
        result = self.run_comparison(bytes(bad), reference, '--canvas', '--size', '32x16')
        self.assertEqual(result.returncode, 1)
        self.assertIn('pixel_errors=[0]', result.stdout)
        constant_reference = bytes([128]) * (32 * 16) + reference[32 * 16:]
        for row in range(16):
            bad[row * 32:(row + 1) * 32] = bytes([88, 168]) * 16
        result = self.run_comparison(bytes(bad), constant_reference, '--canvas', '--size', '32x16')
        self.assertEqual(result.returncode, 1)
        self.assertIn('delta 0', result.stdout)
        self.assertIn('pixel_errors=[0]', result.stdout)

    def test_truncated_raw_or_malformed_signature_fails(self):
        hardware, reference, _ = self.picture(32, 16)
        for hw, ref in ((hardware[:-1], reference), (hardware, reference[:-1])):
            result = self.run_comparison(hw, ref, '--canvas', '--size', '32x16')
            self.assertEqual(result.returncode, 1)
            self.assertIn('truncated raw', result.stderr)
        malformed = self.signatures([10]) + b'malformed\n'
        result = self.run_comparison(malformed, self.signatures([10]))
        self.assertEqual(result.returncode, 1)
        self.assertIn('malformed signature', result.stderr)

    def test_complete_prefix_and_identical_static_frames(self):
        a, ra, _ = self.picture(32, 16, 0)
        b, rb, _ = self.picture(32, 16, 1)
        args = ('--canvas', '--size', '32x16')
        self.assertEqual(self.run_comparison(a + b, ra + rb, *args).returncode, 0)
        self.assertEqual(self.run_comparison(a, ra + rb, *args).returncode, 1)
        self.assertEqual(self.run_comparison(a + a, ra + ra, *args).returncode, 0)

    def test_mpeg2_order_rejects_empty_and_truncated_but_accepts_static_duplicates(self):
        a, ra, _ = self.picture(32, 16)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for hardware, reference, expected in ((a + a, ra + ra, 0), (b'', b'', 1),
                                                   (a[:-1], ra, 1), (a, ra[:-1], 1),
                                                   (a + a + a + a, ra, 1)):
                (root / 'hw').write_bytes(hardware)
                (root / 'ref').write_bytes(reference)
                result = subprocess.run([sys.executable, str(ROOT / 'scripts/m2v-frame-order.py'),
                                         str(root / 'hw'), str(root / 'ref'), '--size', '32x16'],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                self.assertNotIn('Traceback', result.stderr)
