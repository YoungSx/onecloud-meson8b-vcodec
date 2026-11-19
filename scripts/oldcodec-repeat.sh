#!/bin/sh
# Repeat the short old-codec samples to measure how many pictures each run
# delivers.  VC-1 and MPEG-4 delivery is known to vary between runs; compare
# the distribution on two modules instead of trusting a single decode-compare
# verdict.
#
#   oldcodec-repeat.sh            # REPEAT=8, COUNTS="8 4 2"
#   REPEAT=3 COUNTS="8" oldcodec-repeat.sh
set -u
D=/root/venc-deploy/dev
P=/root/venc-deploy/p6
REPEAT=${REPEAT:-8}
COUNTS=${COUNTS:-"8 4 2"}

run_case() { # tag file fourcc width height
	tag=$1 file=$2 fourcc=$3 w=$4 h=$5
	for n in $COUNTS; do
		i=0
		while [ "$i" -lt "$REPEAT" ]; do
			i=$((i + 1))
			rm -f /tmp/f.h
			CAPTURE_BUFFERS=$n FRAME_HASH=/tmp/f.h timeout -k 5 120 \
				"$D/res-switch-test" /dev/video0 "$P/$file" "$fourcc" "$w" "$h" \
				> /tmp/f.log 2>&1
			pics=0
			[ -s /tmp/f.h ] && pics=$(wc -l < /tmp/f.h)
			printf '%s n=%-2s run=%s pics=%s %s\n' "$tag" "$n" "$i" "$pics" \
				"$(tail -1 /tmp/f.log)"
		done
	done
}

run_case vc1-320x240 SA00050.vc1 VC1G 320 240
run_case vc1-176x144 SA00040.vc1 VC1G 176 144
run_case mpeg4 m4v-repro.m4v MPG4 320 240