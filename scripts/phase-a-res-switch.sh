#!/bin/sh
# Phase A: resolution switching on both codecs, several CAPTURE counts.
set -u
D=/root/venc-deploy/dev
S=$D/switch-in
CAPTURE_COUNTS=${CAPTURE_COUNTS:-"1 4 8 20"}

echo "== H264 320x240 -> 1600x900 -> 320x240  counts=$CAPTURE_COUNTS"
CAPTURE_COUNTS=$CAPTURE_COUNTS sh "$D/res-switch-check.sh" H264 320 240 \
	"$S/h264-320x240.h264" "$S/h264-1600x900.h264" "$S/h264-320x240.h264"
echo "rc=$?"
echo "== HEVC 320x240 -> 1080p -> 320x240  counts=$CAPTURE_COUNTS"
CAPTURE_COUNTS=$CAPTURE_COUNTS sh "$D/res-switch-check.sh" HEVC 320 240 \
	"$S/hevc-320x240.hevc" "$S/hevc-1080p.hevc" "$S/hevc-320x240.hevc"
echo "rc=$?"