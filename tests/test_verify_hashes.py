import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'verify_hashes', Path(__file__).resolve().parent.parent / 'scripts/verify-hashes.py')
verify_hashes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_hashes)


def framemd5(values):
    return '\n'.join(f'0,{i},0,0,0,0,0,0,{v}' for i, v in enumerate(values)) + '\n'


class HashComparisonTests(unittest.TestCase):
    def run_script(self, reference, output):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'sw.md5').write_text(framemd5(reference))
            (base / 'hw.md5').write_text(framemd5(output))
            script = Path(__file__).resolve().parent.parent / 'scripts/verify-hashes.py'
            result = subprocess.run(
                [sys.executable, str(script), str(base / 'sw.md5'), str(base / 'hw.md5')],
                capture_output=True, text=True)
        return result

    def test_pass_reports_frame_count(self):
        result = self.run_script(['aa'] * 3, ['aa'] * 3)
        self.assertEqual(result.returncode, 0)
        self.assertIn('PASS: 3/3', result.stdout)

    def test_short_output_reports_prefix(self):
        result = self.run_script(['aa'] * 3, ['aa'] * 2)
        self.assertEqual(result.returncode, 1)
        self.assertIn('hardware 2 frames', result.stderr)
        self.assertIn('prefix of the reference', result.stderr)

    def test_mismatch_reports_first_frame(self):
        result = self.run_script(['aa', 'bb', 'cc'], ['aa', 'dd', 'cc'])
        self.assertEqual(result.returncode, 1)
        self.assertIn('first differing frame 1', result.stderr)

    def test_extra_output_reports_prefix_inverse(self):
        result = self.run_script(['aa', 'bb'], ['aa', 'bb', 'cc'])
        self.assertEqual(result.returncode, 1)
        self.assertIn('reference is a prefix', result.stderr)

    def test_empty_output_fails_without_divergence_detail(self):
        result = self.run_script(['aa'], [])
        self.assertEqual(result.returncode, 1)
        self.assertIn('hardware 0 frames', result.stderr)
        self.assertNotIn('prefix', result.stderr)

    def test_divergence_function(self):
        self.assertIn('first differing frame 1',
                      verify_hashes.divergence(['aa', 'bb'], ['aa', 'cc']))
        self.assertIn('prefix of the reference',
                      verify_hashes.divergence(['aa', 'bb'], ['aa']))


if __name__ == '__main__':
    unittest.main()
