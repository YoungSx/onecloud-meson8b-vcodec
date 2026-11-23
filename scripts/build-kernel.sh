#!/usr/bin/env bash
# Matching kernel symbols, external codecs and the additive patch; no install.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
jobs=${JOBS:-4}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 1; }
archive="$root/sources/linux-5.15.79.tar.xz"
kernel="$root/build/kernel/linux-5.15.79"
mkdir -p "$root/sources" "$root/build/kernel"
if [[ ! -f "$archive" ]]; then
    curl -fsSL --retry 3 --max-time 180 \
      https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.15.79.tar.xz -o "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' cba39031dbc0eed0785b8afdc8c58cf23df83e47001b2354fa44486ae699c154 "$archive" | sha256sum -c -
key=$(sha256sum baseline/0001-armbian-onecloud-5.15.79.patch \
  config/target-5.15.79-meson.config patches/0001-meson8b-runtime-codecs.patch | sha256sum | cut -d' ' -f1)
if [[ ! -d "$kernel" ]]; then
    tar -xJf "$archive" -C "$root/build/kernel"
    patch -d "$kernel" -p1 --batch --forward --fuzz=0 < baseline/0001-armbian-onecloud-5.15.79.patch
    python3 scripts/export-kernel-patch.py "$kernel" "$root/build/kernel/exported.patch"
    cmp patches/0001-meson8b-runtime-codecs.patch "$root/build/kernel/exported.patch"
    patch -d "$kernel" -p1 --batch --forward --fuzz=0 < patches/0001-meson8b-runtime-codecs.patch
    cp config/target-5.15.79-meson.config "$kernel/.config"
    printf '%s\n' "$key" > "$root/build/kernel/INPUTS_SHA256"
else
    [[ $(cat "$root/build/kernel/INPUTS_SHA256") == "$key" ]] || {
        echo 'Kernel inputs changed; remove only build/kernel and rebuild' >&2; exit 1;
    }
fi
make -C "$kernel" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  CC=arm-linux-gnueabihf-gcc-11 LOCALVERSION=-meson olddefconfig
[[ $(make -s -C "$kernel" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  CC=arm-linux-gnueabihf-gcc-11 LOCALVERSION=-meson kernelrelease) == 5.15.79-meson ]]
make -C "$kernel" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  CC=arm-linux-gnueabihf-gcc-11 LOCALVERSION=-meson -j"$jobs" vmlinux modules
make KDIR="$kernel" modules -j"$jobs"
make -C "$kernel" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
  CC=arm-linux-gnueabihf-gcc-11 LOCALVERSION=-meson \
  M="$kernel/drivers/media/platform/meson/meson8b-vcodec" -j"$jobs" modules
for directory in runtime vdec venc ge2d; do
    for source in "$root/$directory/"*.c "$root/$directory/"*.h "$root/$directory/Makefile"; do
        [[ "$source" != *.mod.c ]] || continue
        cmp "$source" "$kernel/drivers/media/platform/meson/meson8b-vcodec/$directory/$(basename "$source")"
    done
done
for module in runtime/meson8b-runtime.ko vdec/meson-vdec.ko venc/meson-venc.ko ge2d/meson8b-ge2d.ko; do
    [[ $(modinfo -F vermagic "$module") == '5.15.79-meson '* ]]
done
[[ $(modinfo -F depends vdec/meson-vdec.ko) == *meson8b-runtime* ]]
[[ $(modinfo -F depends venc/meson-venc.ko) == *meson8b-runtime* ]]
make dtbo
printf 'PASS: original kernel symbols, both codec builds, patch/source and vermagic\n'
