#!/bin/sh
# P1 tests: resolution switch and corrupt input behavior.
# Usage: res-switch-test.sh    (run on target, unattended via nohup)
R=/tmp/meson8b-vcodec-runtime
fail=0

echo "=== resolution switch 320x240 -> 1080p in one session ==="
insmod $R/meson8b-runtime.ko || exit 1
insmod $R/meson-vdec.ko || { rmmod meson8b_runtime; exit 1; }
ffmpeg -hide_banner -loglevel error -c:v h264_v4l2m2m -i $R/res-switch.h264 -f framemd5 - > /tmp/res-switch.md5
echo "res-switch rc=$?"
grep -c '^0,' /tmp/res-switch.md5 | sed 's/^/frames=/'
# frames 320x240 are small (460800B) then large (3110400B); count each
grep '^0,' /tmp/res-switch.md5 | awk '{print $NF}' | sort -u | sed 's/^/md5-samples: /' | head -3
rmmod meson_vdec || fail=1
rmmod meson8b_runtime || fail=1

echo "=== corrupt/truncated input ==="
insmod $R/meson8b-runtime.ko || exit 1
insmod $R/meson-vdec.ko || { rmmod meson8b_runtime; exit 1; }
timeout 120 ffmpeg -hide_banner -loglevel error -c:v h264_v4l2m2m -i $R/corrupt.h264 -f null - ; echo "corrupt rc=$?"
rmmod meson_vdec || fail=1
rmmod meson8b_runtime || fail=1
dmesg > /tmp/res-switch.dmesg
grep -cE "BUG|WARNING|Oops|Call trace" /tmp/res-switch.dmesg
echo "fail=$fail"
