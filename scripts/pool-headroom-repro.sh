#!/bin/sh
# Repeat the soak's mixed load around an H.264 1080p decode and keep the
# evidence when one fails, to tell a pool that is too small from a decode
# defect: a stalled decoder leaves pictures waiting for CAPTURE buffers.
#
#   pool-headroom-repro.sh ROUNDS
set -u
D=/root/venc-deploy
FF=$D/ffmpeg
ET=$D/encode-test
ROUNDS=${1:-200}
LOG=$D/dev/headroom.log
REF=$D/p5/h264-1080p-baseline.md5
DBG=/sys/kernel/debug/meson-vdec
: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

say "== start rounds=$ROUNDS extra_buffers=$(cat /sys/module/meson_vdec/parameters/h264_extra_buffers)"
for i in $(seq 1 "$ROUNDS"); do
	timeout -k 5 90 "$FF" -nostdin -v error -c:v hevc_v4l2m2m \
		-i "$D/1080p.mp4" -pix_fmt nv12 -filter_threads 1 -threads:v 1 -f framemd5 -y /tmp/hr-hevc.md5 \
		> /dev/null 2>&1
	rm -f /tmp/hr-h264.md5
	timeout -k 5 90 "$FF" -nostdin -v error -c:v h264_v4l2m2m \
		-i "$D/p5/h264-1080p-baseline.mp4" -pix_fmt nv12 \
		-filter_threads 1 -threads:v 1 -f framemd5 -y /tmp/hr-h264.md5 > /tmp/hr-h264.log 2>&1
	rc=$?
	n=$(grep -c '^0,' /tmp/hr-h264.md5 2>/dev/null)
	if [ "$rc" -ne 0 ] || [ "$n" -ne 300 ]; then
		say "round=$i FAIL rc=$rc frames=$n"
		cp /tmp/hr-h264.log "$D/dev/headroom-fail.log"
		for f in pool_slots pool_bytes pool_waiting_peak converted_frames; do
			say "  $f=$(cat "$DBG/$f" 2>/dev/null)"
		done
		say "  $(grep '^CmaFree' /proc/meminfo)"
		dmesg | tail -30 > "$D/dev/headroom-fail.dmesg"
		continue
	fi
	python3 "$D/p5/verify-hashes.py" "$REF" /tmp/hr-h264.md5 > /dev/null 2>&1 ||
		say "round=$i HASHMISMATCH frames=$n"
	timeout -k 5 60 "$ET" /dev/video1 /tmp/hr.h264 /tmp/hr.nv12 \
		320 240 10 NV12 90 H264 > /dev/null 2>&1
	timeout -k 5 60 "$ET" /dev/video1 /tmp/hr.mjpg /tmp/hr.nv12 \
		320 240 3 NV12 90 JPEG > /dev/null 2>&1
	[ $((i % 25)) -eq 0 ] &&
		say "round=$i ok peak_waiting=$(cat "$DBG/pool_waiting_peak")"
done
say "HEADROOM_DONE rounds=$ROUNDS"
