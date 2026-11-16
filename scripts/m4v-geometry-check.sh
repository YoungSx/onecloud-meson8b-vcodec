#!/bin/sh
# Is the MPEG-4 B-picture band defect tied to the bottom of the picture, or to
# whatever region the encoder happened not to code?
#
# The B captures lose rows 192-239 and part of 176-191. Both hypotheses
# explain that: the loss is at the bottom, or the loss follows the static
# region, which in testsrc happens to be at the bottom. vflip separates them.
#
# B-flip is A-up flipped, so its moving strip sits at the TOP. If the loss
# stays at the bottom, it is geometric; if it follows the strip to the top,
# it is the coding pattern. D-t2flip flips the fully moving stream that was
# exact, as a control against the flip itself.
#
# Streams and their software references are built on the analysis host (this
# box's ffmpeg has no lavfi sources) and copied in with their md5s checked.
#
# Needs $BIN/res-switch-test and nothing else holding /dev/video0.
set -u
D=/root/venc-deploy/p6
BIN=/root/venc-deploy
OUT=$D/geo
LOG=$D/m4v-geo.log
FRAME=196608
: > "$LOG"
mkdir -p "$OUT"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

# name md5-of-stream
run() {
	name=$1
	have=$(md5sum "$D/$name.m4v" | cut -d' ' -f1)
	if [ "$have" != "$2" ]; then
		say "!! $name md5 $have != $2, skipped"
		return 1
	fi
	dump=$OUT/$name.nv12
	rm -f "$dump"
	FRAME_DUMP=$dump timeout -k 5 120 "$BIN/res-switch-test" /dev/video0 \
		"$D/$name.m4v" MPG4 320 240 >> "$LOG" 2>&1
	say "   $name frames=$(($(stat -c %s "$dump" 2>/dev/null || echo 0) / FRAME))"
	gzip -1 -c "$dump" > "$OUT/$name.nv12.gz" 2>/dev/null \
		|| say "   $name gzip failed"
	rm -f "$dump"
}

run A-up     886166e24ea0cab05338548d4099b982
run B-flip   0be7db45fa1665c6d5cb2e2649028f39
run D-t2flip 217cb96ff30179872c112494cb3c74b3
say M4V_GEO_DONE
