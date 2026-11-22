#!/bin/sh
# Phase A: 1080p decode alongside a 1080p encode, decoder at FFmpeg's default
# CAPTURE count, encoder on the largest input it takes. Records the lowest
# CmaFree seen while both sessions were open.
#
#   phase-a-concurrent.sh [FRAMES]
set -u
D=/root/venc-deploy
FF=$D/ffmpeg
PROD=/usr/local/bin/ffmpeg
ET=$D/encode-test
FRAMES=${1:-120}
LOG=$D/phase-a-concurrent.log
: > "$LOG"

# name codec input reference
case_run() {
	label=$1 codec=$2 input=$3 ref=$4
	samples=$(mktemp)
	rm -f "/tmp/pa-$label.md5"
	while :; do
		awk '/^CmaFree/ {print $2}' /proc/meminfo >> "$samples"
		sleep 1
	done &
	sampler=$!
	(
		timeout -k 5 900 "$FF" -nostdin -v error -c:v "${codec}_v4l2m2m" \
			-i "$input" -map 0:v -pix_fmt nv12 \
			-filter_threads 1 -threads:v 1 -f framemd5 -y "/tmp/pa-$label.md5"
		echo "decode rc=$?"
	) > "/tmp/pa-$label.log" 2>&1 &
	decode=$!
	timeout -k 5 900 "$ET" /dev/video1 /tmp/pa-encode.h264 /dev/null \
		1920 1080 "$FRAMES" NV12 90 H264 > /tmp/pa-encode.log 2>&1
	encode_rc=$?
	wait "$decode"
	kill "$sampler" 2>/dev/null
	wait "$sampler" 2>/dev/null

	printf '%s %s cma_free_min=%s now=%s encode=%s\n' "$label" \
		"$(tail -1 "/tmp/pa-$label.log")" \
		"$(sort -n "$samples" | head -1)" \
		"$(awk '/^CmaFree/ {print $2}' /proc/meminfo)" "$encode_rc" >> "$LOG"
	python3 "$D/p5/verify-hashes.py" "$ref" "/tmp/pa-$label.md5" >> "$LOG" 2>&1
	if timeout -k 5 120 "$PROD" -nostdin -v error -i /tmp/pa-encode.h264 \
		-f null - 2>> "$LOG"; then
		echo "$label: encoded stream software-decodes" >> "$LOG"
	else
		echo "$label: encoded stream FAIL" >> "$LOG"
	fi
	rm -f "$samples"
}

echo "== HEVC 1080p decode + H.264 1080p encode" >> "$LOG"
case_run hevc-1080p hevc "$D/1080p.mp4" "$D/p5/software-1080.md5"

echo "== H.264 1080p 1-reference decode + H.264 1080p encode" >> "$LOG"
case_run h264-1080p h264 "$D/p5/h264-1080p-baseline.mp4" \
	"$D/p5/h264-1080p-baseline.md5"

echo "== H.264 1080p 4-reference B-frame decode + H.264 1080p encode" >> "$LOG"
case_run h264-4ref h264 "$D/p5/h264-4ref-1080p.mp4" \
	"$D/p5/software-h264-4ref-1080p.md5"

cat "$LOG"
