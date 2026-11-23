#!/usr/bin/env python3
"""Check documentation links and reject tracked development artifacts."""
import re
import subprocess
from pathlib import Path
from urllib.parse import unquote, urlsplit


GENERATED_DIRECTORIES = {'evidence', 'private', 'build', 'sources'}


def check_tracked(root):
    # Source archives have no index; packaging already excludes local artifacts.
    if not (root / '.git').exists():
        return
    paths = subprocess.check_output(
        ['git', '-C', str(root), 'ls-files', '-z'], text=True).split('\0')
    forbidden = [name for name in paths if name and (
        Path(name).parts[0] in GENERATED_DIRECTORIES or Path(name).suffix == '.log')]
    if forbidden:
        raise ValueError('Development artifacts must stay outside Git:\n' +
                         '\n'.join(forbidden))


def check(root):
    documents = [*root.glob('*.md'), *root.glob('docs/**/*.md'),
                 *root.glob('patches/**/*.md')]
    failures = []
    for path in documents:
        source = re.sub(r'```.*?```', '', path.read_text(), flags=re.S)
        for link in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', source):
            target = link.split(' "', 1)[0].strip('<>')
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path:
                continue
            resolved = (path.parent / unquote(url.path)).resolve()
            if not resolved.is_relative_to(root.resolve()):
                failures.append(f'{path.relative_to(root)}: outside repository: {target}')
                continue
            relative = resolved.relative_to(root.resolve())
            if relative.parts and relative.parts[0] in GENERATED_DIRECTORIES:
                failures.append(f'{path.relative_to(root)}: local artifact: {target}')
            elif resolved.suffix == '.log':
                failures.append(f'{path.relative_to(root)}: local artifact: {target}')
            elif not resolved.exists():
                failures.append(f'{path.relative_to(root)}: {target}')
    if failures:
        raise ValueError('Broken documentation links:\n' + '\n'.join(failures))
    return len(documents)


if __name__ == '__main__':
    root = Path(__file__).resolve().parent.parent
    check_tracked(root)
    print(f'PASS: repository content policy and local links in {check(root)} Markdown files')
