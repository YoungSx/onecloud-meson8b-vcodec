#!/bin/sh
# P1 stress: 10 rounds of insmod -> hw decode -> rmmod, watching memory and dmesg.
# Usage: stress-cycle.sh <rounds>   (run on target, unattended via nohup)
R=/tmp/meson8b-vcodec-runtime
ROUNDS=${1:-10}
fail=0
mem_start=$(awk '/MemFree/ {print $2}' /proc/meminfo)
slab_start=$(awk '/Slab/ {print $2}' /proc/meminfo)
for i in $(seq 1 $ROUNDS); do
  insmod $R/meson8b-runtime.ko || { echo "round $i insmod runtime FAILED"; exit 1; }
  insmod $R/meson-vdec.ko || { echo "round $i insmod vdec FAILED"; rmmod meson8b_runtime; exit 1; }
  ffmpeg -hide_banner -loglevel error -c:v h264_v4l2m2m -i $R/baseline-1080p.mp4 -f framemd5 - > /tmp/stress-hw.md5
  rc=$?
  frames=$(grep -c '^0,' /tmp/stress-hw.md5)
  rmmod meson_vdec || { echo "round $i rmmod vdec FAILED"; exit 1; }
  rmmod meson8b_runtime || { echo "round $i rmmod runtime FAILED"; exit 1; }
  if [ $rc -ne 0 ] || [ "$frames" != 300 ]; then
    echo "round $i: rc=$rc frames=$frames MISMATCH"
    fail=$((fail+1))
  else
    echo "round $i: ok (300 frames, rc 0)"
  fi
  if dmesg | tail -50 | grep -qE "BUG|WARNING|Oops|Call trace"; then
    echo "round $i: KERNEL WARNINGS DETECTED"
    fail=$((fail+1))
  fi
done
mem_end=$(awk '/MemFree/ {print $2}' /proc/meminfo)
slab_end=$(awk '/Slab/ {print $2}' /proc/meminfo)
echo "memfree_delta=$((mem_end-mem_start))kB slab_delta=$((slab_end-slab_start))kB"
echo "failures=$fail/$ROUNDS"
