"""Source exports omit old reports and still carry verified rebuild inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SourcePackageTests(unittest.TestCase):
    def test_old_commit_reports_are_excluded_and_manifest_is_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'repo'
            root.mkdir()
            script = (ROOT / 'scripts/package-source.sh').read_text()
            # Tiny pinned input substitutes exercise the actual packaging path.
            archives = {
                'sources/linux-5.15.79.tar.xz':
                    'cba39031dbc0eed0785b8afdc8c58cf23df83e47001b2354fa44486ae699c154',
                'sources/ffmpeg/ffmpeg-7.0.2.tar.xz':
                    '8646515b638a3ad303e23af6a3587734447cb8fc0a0c064ecdb8e95c4fd8b389',
                'sources/v4l-utils/v4l-utils-1.22.1.tar.bz2':
                    '65c6fbe830a44ca105c443b027182c1b2c9053a91d1e72ad849dfab388b94e31',
            }
            for name, digest in archives.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
                self.assertIn(digest, script)
                script = script.replace(digest, hashlib.sha256(path.read_bytes()).hexdigest())
            (root / 'scripts').mkdir()
            (root / 'scripts/package-source.sh').write_text(script)
            vendor = root / 'sources/vendor/microcode.c'
            vendor.parent.mkdir(parents=True)
            vendor.write_text('const unsigned microcode[] = { 1 };\n')
            (root / 'firmware').mkdir()
            (root / 'firmware/sources.json').write_text(json.dumps({
                'codec': {'path': 'microcode.c',
                          'sha256': hashlib.sha256(vendor.read_bytes()).hexdigest()}}))
            for name in ('evidence/old.md', 'private/run.py', 'build/result.json',
                         'driver.c'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('historical fixture')
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'user.name=Test',
                            '-c', 'user.email=test@example.invalid', 'commit', '-qm',
                            'historical source fixture'], check=True)
            output = Path(directory) / 'package'
            subprocess.run(['bash', str(root / 'scripts/package-source.sh'), str(output)],
                           check=True, capture_output=True, text=True)
            self.assertTrue((output / 'source/driver.c').is_file())
            self.assertTrue((output / 'sources/vendor/microcode.c').is_file())
            for name in ('evidence', 'private', 'build', 'sources'):
                self.assertFalse((output / 'source' / name).exists())
            subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=output,
                           check=True, capture_output=True)
