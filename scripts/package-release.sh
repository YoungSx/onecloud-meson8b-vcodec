#!/usr/bin/env bash
# Build inputs must already be prepared; releases never deploy to a device.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
version=${1:?Usage: package-release.sh VERSION NEW_DIRECTORY}
dest=${2:?Usage: package-release.sh VERSION NEW_DIRECTORY}
[[ "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]] || {
    echo 'Version must be a v-prefixed SemVer tag' >&2; exit 1;
}
[[ ! -e "$dest" ]] || { echo 'Refusing existing release directory' >&2; exit 1; }
[[ -z $(git status --porcelain --untracked-files=normal) ]] || {
    echo 'Release requires a clean committed source tree' >&2; exit 1;
}
for file in build/ffmpeg-test/ffmpeg build/ffmpeg-test/ffprobe build/v4l2-tools/v4l2-ctl; do
    [[ -x "$file" ]] || { echo "Missing release tool: $file" >&2; exit 1; }
done
mkdir -p "$dest"
bash scripts/package-runtime.sh "$dest/runtime"
python3 scripts/verify-package.py "$dest/runtime"
bash scripts/package-source.sh "$dest/source"
(cd "$dest/source"; sha256sum -c SHA256SUMS)
commit=$(git rev-parse HEAD)
epoch=$(git show -s --format=%ct HEAD)
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
  -C "$dest/runtime" -cf - . | gzip -n > "$dest/onecloud-meson8b-runtime-$version.tar.gz"
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
  -C "$dest/source" -cf - . | gzip -n > "$dest/onecloud-meson8b-source-$version.tar.gz"
python3 - "$dest" "$version" "$commit" <<'PYCODE'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
dest, version, commit = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
data = {
    'version': version, 'source_commit': commit, 'kernel_release': '5.15.79-meson',
    'compiler': subprocess.check_output(['arm-linux-gnueabihf-gcc-11', '--version'], text=True).splitlines()[0],
    'hardware_validation': 'Hardware acceptance is version-specific. See '
                           'docs/validation.md and dated evidence '
                           'for tested module identities and unresolved gates. '
                           'CI does not test S805 hardware.',
    'artifacts': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.glob('*.tar.gz')},
}
(dest / 'PROVENANCE.json').write_text(json.dumps(data, indent=2) + '\n')
PYCODE
(cd "$dest"; sha256sum ./*.tar.gz PROVENANCE.json > SHA256SUMS; sha256sum -c SHA256SUMS)
printf 'Checked release assets: %s\n' "$dest"
