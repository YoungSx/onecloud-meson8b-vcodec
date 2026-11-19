#!/bin/sh
# Isolate resolution switching: up (small -> big) and down (big -> small),
# one codec at a time. res-switch-check.sh exercises a -> b -> a in one run,
# which never distinguishes the two directions.
set -u
D=/root/venc-deploy/dev
S=$D/switch-in
COUNT=${COUNT:-8}

echo "== up   H264 320x240 -> 1600x900"
CAPTURE_COUNTS=$COUNT sh "$D/res-switch-check.sh" H264 320 240 \
	"$S/h264-320x240.h264" "$S/h264-1600x900.h264"
echo "rc=$?"
echo "== down H264 1600x900 -> 320x240"
CAPTURE_COUNTS=$COUNT sh "$D/res-switch-check.sh" H264 1600 900 \
	"$S/h264-1600x900.h264" "$S/h264-320x240.h264"
echo "rc=$?"
echo "== up   HEVC 320x240 -> 1080p"
CAPTURE_COUNTS=$COUNT sh "$D/res-switch-check.sh" HEVC 320 240 \
	"$S/hevc-320x240.hevc" "$S/hevc-1080p.hevc"
echo "rc=$?"
echo "== down HEVC 1080p -> 320x240"
CAPTURE_COUNTS=$COUNT sh "$D/res-switch-check.sh" HEVC 1920 1080 \
	"$S/hevc-1080p.hevc" "$S/hevc-320x240.hevc"
echo "rc=$?"