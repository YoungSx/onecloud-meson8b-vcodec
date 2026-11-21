#!/usr/bin/env bash
# Collect already built modules; never compiles on the production target.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
dest=${1:?Usage: package-runtime.sh NEW_PACKAGE_DIRECTORY}
[[ ! -e "$dest" ]] || { echo 'Refusing existing package directory' >&2; exit 1; }
for module in runtime/meson8b-runtime.ko vdec/meson-vdec.ko venc/meson-venc.ko; do
    [[ $(modinfo -F vermagic "$root/$module") == '5.15.79-meson '* ]] || {
        echo "Module does not match target kernel: $module" >&2; exit 1;
    }
done
[[ -s "$root/firmware/meson/vdec/meson8b-runtime.dtbo" ]]
if [[ -f "$root/SOURCE_COMMIT" ]]; then
    source_commit=$(cat "$root/SOURCE_COMMIT")
else
    source_commit=$(git -C "$root" rev-parse HEAD)
fi
[[ "$source_commit" =~ ^[0-9a-f]{40}$ ]] || { echo 'Invalid source commit' >&2; exit 1; }
mkdir -p "$dest/modules" "$dest/firmware" "$dest/scripts"
for module in runtime/meson8b-runtime.ko vdec/meson-vdec.ko venc/meson-venc.ko; do
    install -m 644 "$root/$module" "$dest/modules/"
done
cp -a "$root/firmware/meson" "$dest/firmware/"
install -m 755 "$root/scripts/runtime-device.py" "$root/scripts/install-autoload.sh" "$root/scripts/remove-autoload.sh" "$dest/scripts/"
cp -a "$root/systemd" "$dest/systemd"
install -m 644 "$root/README.md" "$root/README.zh-CN.md" "$root/CHANGELOG.md" "$dest/"
cp -a "$root/docs" "$dest/docs"
install -m 644 "$root/LICENSE" "$dest/LICENSE"
mkdir "$dest/patches"
install -m 644 "$root/patches/0001-meson8b-runtime-codecs.patch" \
  "$root/patches/ffmpeg-7.0.2-v4l2-drain.patch" \
  "$root/patches/ffmpeg-7.0.2-v4l2-bugs.patch" "$root/patches/series" "$dest/patches/"
cp -a "$root/config" "$dest/config"
if [[ -x "$root/build/ffmpeg-test/ffmpeg" && -x "$root/build/ffmpeg-test/ffprobe" ]]; then
    [[ -f "$root/build/ffmpeg-test/BUILD_VARIANT" ]] || {
        echo 'Rebuild FFmpeg with the current script before packaging' >&2; exit 1;
    }
    install -m 644 "$root/build/ffmpeg-test/BUILD_VARIANT" "$dest/FFMPEG_BUILD_VARIANT"
    mkdir "$dest/tools"
    install -m 755 "$root/build/ffmpeg-test/ffmpeg" "$root/build/ffmpeg-test/ffprobe" "$dest/tools/"
    install -m 644 "$root/build/ffmpeg-test/COPYING.LGPLv2.1" "$dest/tools/"
fi
if [[ -x "$root/build/v4l2-tools/v4l2-ctl" ]]; then
    mkdir -p "$dest/tools"
    install -m 755 "$root/build/v4l2-tools/v4l2-ctl" "$dest/tools/"
    install -m 644 "$root/sources/v4l-utils/v4l-utils-1.22.1/COPYING" "$dest/tools/COPYING.v4l-utils"
fi
python3 - "$root/meson8b-runtime.dts" "$dest/TARGET_PROVIDERS.json" <<'PYCODE'
import json,re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
patterns={'clkc':r'clocks = <(\d+) ', 'canvas':r'amlogic,canvas = <(\d+)>',
          'pmu':r'amlogic,ao-sysctrl = <(\d+)>', 'reset':r'resets = <(\d+) '}
values={k:int(re.search(p,s)[1]) for k,p in patterns.items()}
Path(sys.argv[2]).write_text(json.dumps(values,indent=2)+'\n')
PYCODE
printf '%s\n' 5.15.79-meson > "$dest/KERNEL_RELEASE"
printf '%s\n' "$source_commit" > "$dest/SOURCE_COMMIT"
python3 - "$dest" "$source_commit" <<'PYCODE'
import re
from pathlib import Path
import sys
root, commit = Path(sys.argv[1]).resolve(), sys.argv[2]
for path in [*root.glob('*.md'), *root.glob('docs/**/*.md')]:
    def resolve_link(match):
        target = match[2]
        if '://' in target or target.startswith('#') or (path.parent / target).exists():
            return match[0]
        relative = (path.parent / target).resolve().relative_to(root)
        return match[1] + f'https://github.com/YoungSx/onecloud-meson8b-vcodec/blob/{commit}/{relative}' + ')'
    path.write_text(re.sub(r'(\[[^\]]*\]\()([^\s)]+)\)', resolve_link, path.read_text()))
PYCODE
manifest=$(mktemp)
trap 'rm -f "$manifest"' EXIT
(cd "$dest"; find . -type f -printf '%P\0' | sort -z | xargs -0 sha256sum) > "$manifest"
install -m 644 "$manifest" "$dest/SHA256SUMS"
printf 'Runtime package: %s\n' "$dest"
