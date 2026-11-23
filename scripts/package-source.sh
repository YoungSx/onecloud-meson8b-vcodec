#!/usr/bin/env bash
# Committed build inputs plus pinned upstream archives for offline rebuilds.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
dest=${1:?Usage: package-source.sh NEW_SOURCE_DIRECTORY [SOURCE_COMMIT]}
[[ ! -e "$dest" ]] || { echo 'Refusing existing source directory' >&2; exit 1; }
payload_commit=$(git -C "$root" rev-parse "${2:-HEAD}^{commit}")
[[ -f "$root/sources/linux-5.15.79.tar.xz" ]]
[[ -f "$root/sources/ffmpeg/ffmpeg-7.0.2.tar.xz" ]]
[[ -f "$root/sources/v4l-utils/v4l-utils-1.22.1.tar.bz2" ]]
mkdir -p "$dest/source" "$dest/sources/ffmpeg" "$dest/sources/vendor" "$dest/sources/v4l-utils"
git -C "$root" archive "$payload_commit" -- . ':!evidence' ':!private' ':!build' ':!sources' \
  | tar -xf - -C "$dest/source"
printf '%s\n' "$payload_commit" > "$dest/source/SOURCE_COMMIT"
printf '%s\n' "$payload_commit" > "$dest/SOURCE_COMMIT"
cp "$root/sources/linux-5.15.79.tar.xz" "$dest/sources/"
cp "$root/sources/ffmpeg/ffmpeg-7.0.2.tar.xz" "$dest/sources/ffmpeg/"
cp "$root/sources/v4l-utils/v4l-utils-1.22.1.tar.bz2" "$dest/sources/v4l-utils/"
python3 - "$root" "$dest" <<'PYCODE'
import hashlib
import json
from pathlib import Path
import shutil
import sys
root, dest = map(Path, sys.argv[1:])
metadata = json.loads((dest / 'source/firmware/sources.json').read_text())
for entry in metadata.values():
    if not isinstance(entry, dict):
        continue
    for item in entry.get('files', [entry]):
        source = root / 'sources/vendor' / item['path']
        if hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Vendor source checksum mismatch: ' + str(source))
        target = dest / 'sources/vendor' / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
for relative, digest in {
    'sources/linux-5.15.79.tar.xz': 'cba39031dbc0eed0785b8afdc8c58cf23df83e47001b2354fa44486ae699c154',
    'sources/ffmpeg/ffmpeg-7.0.2.tar.xz': '8646515b638a3ad303e23af6a3587734447cb8fc0a0c064ecdb8e95c4fd8b389',
    'sources/v4l-utils/v4l-utils-1.22.1.tar.bz2': '65c6fbe830a44ca105c443b027182c1b2c9053a91d1e72ad849dfab388b94e31',
}.items():
    if hashlib.sha256((dest / relative).read_bytes()).hexdigest() != digest:
        raise ValueError('Upstream archive checksum mismatch: ' + relative)
PYCODE
cat > "$dest/README.txt" <<'DOC'
source/ contains committed sources identified by SOURCE_COMMIT, excluding
retired report, private and generated directories.
sources/ contains original Linux, FFmpeg and v4l-utils archives and the
pinned vendor microcode sources. Copy sources/ into source/ for offline
builds, then follow source/docs/build.md. Source archives need no Git
checkout to build and package a runtime. docs/ contains concise,
version-specific validation records. Raw reports and private archives
are excluded from new source trees; see source/docs/repository-policy.md.
DOC
manifest=$(mktemp)
trap 'rm -f "$manifest"' EXIT
(cd "$dest"; find . -type f -printf '%P\0' | sort -z | xargs -0 sha256sum) > "$manifest"
install -m 644 "$manifest" "$dest/SHA256SUMS"
printf 'Source package: %s\n' "$dest"
