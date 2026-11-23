import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'check_docs', Path(__file__).resolve().parent.parent / 'scripts/check-docs.py')
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)


class DocumentationTests(unittest.TestCase):
    def test_relative_links_and_missing_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            (root / 'docs/with space.md').write_text('content')
            readme = root / 'README.md'
            readme.write_text('[local](docs/with%20space.md) [web](https://example.com)')
            self.assertEqual(docs.check(root), 2)
            readme.write_text('[missing](docs/missing.md)')
            with self.assertRaisesRegex(ValueError, 'docs/missing.md'):
                docs.check(root)

    def test_local_artifact_link_is_rejected_even_when_present(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in docs.GENERATED_DIRECTORIES:
                (root / folder).mkdir()
                (root / folder / 'report.md').write_text('local report')
                (root / 'README.md').write_text(f'[report]({folder}/report.md)')
                with self.assertRaisesRegex(ValueError, 'local artifact'):
                    docs.check(root)
            (root / 'docs').mkdir()
            (root / 'docs/run.log').write_text('transcript')
            (root / 'README.md').write_text('[report](docs/run.log)')
            with self.assertRaisesRegex(ValueError, 'local artifact'):
                docs.check(root)

    def test_tracked_artifacts_are_rejected_but_local_reports_and_fixtures_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'tests/fixtures').mkdir(parents=True)
            (root / 'tests/fixtures/reference.json').write_text('{}')
            subprocess.run(['git', '-C', str(root), 'add', 'tests'], check=True)
            for name in ('evidence/record.md', 'private/run.py', 'build/report.json',
                         'sources/input.txt', 'docs/run.log'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('temporary')
                docs.check_tracked(root)
                subprocess.run(['git', '-C', str(root), 'add', name], check=True)
                with self.assertRaisesRegex(ValueError, 'Development artifacts'):
                    docs.check_tracked(root)
                subprocess.run(['git', '-C', str(root), 'rm', '--cached', '-q', name],
                               check=True)

    def test_source_archive_needs_no_git_index(self):
        with tempfile.TemporaryDirectory() as directory:
            docs.check_tracked(Path(directory))
