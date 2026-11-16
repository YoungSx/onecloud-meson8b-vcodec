#!/bin/sh
# Does MPEG-4 B-picture decoding depend on content, or on quality, or not at
# all on B pictures?
#
# testsrc captured B pictures come back with rows 192-239 zeroed and rows
# 0-175 copied from the previous anchor, while testsrc2 B pictures come back
# correct; the anchors of both streams are exact. That was one content at one
# quality, so this crosses content (testsrc / testsrc2) with quality (-q:v 2 /
# 5) at a fixed GOP, and adds an all-I stream of each content as a control: if
# the all-I stream is clean, B pictures are what breaks.
#
# testsrc changes only a fade strip for the first second, so its B pictures
# have almost nothing to predict and a wrong one still lands near its
# neighbours; testsrc2 changes everywhere.
#
# Raw canvas dumps, not 8x8 signatures: "how wrong" needs pixels. They are
# gzipped because eight 4.2 MB dumps plus their streams do not fit the 1.8 GB
# free on this box.
#
# Needs an ffmpeg with the mpeg4 encoder and res-switch-test under $BIN, and
# nothing else holding /dev/video0.
set -u
D=/root/venc-deploy/p6
BIN=/root/venc-deploy
OUT=$D/bq
LOG=$D/m4v-bq.log
# One captured picture, canvas aligned: ALIGN(ALIGN(320,64)*ALIGN(240,64),64K)
# luma plus half that for chroma.
FRAME=196608
: > "$LOG"
mkdir -p "$OUT"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

# name source extra-encoder-args...
enc() {
	name=$1; src=$2; shift 2
	ffmpeg -y -hide_banner -loglevel error -f lavfi \
		-i "$src=size=320x240:rate=25" -frames:v 24 -c:v mpeg4 "$@" \
		-f m4v "$OUT/$name.m4v" || return 1
	say "== $name bytes=$(stat -c %s "$OUT/$name.m4v")"
}

# name
run() {
	name=$1
	dump=$OUT/$name.nv12
	rm -f "$dump"
	FRAME_DUMP=$dump timeout -k 5 120 "$BIN/res-switch-test" /dev/video0 \
		"$OUT/$name.m4v" MPG4 320 240 >> "$LOG" 2>&1
	say "   $name frames=$(($(stat -c %s "$dump" 2>/dev/null || echo 0) / FRAME))"
	gzip -1 -c "$dump" > "$OUT/$name.nv12.gz" 2>/dev/null \
		|| say "   $name gzip failed"
	rm -f "$dump"
}

# The all-I rows are the control: same content and quality, no B pictures.
enc q2-allI     testsrc  -q:v 2 -g 1
enc q5-allI     testsrc  -q:v 5 -g 1
enc q2-g12-bf2  testsrc  -q:v 2 -g 12 -bf 2
enc q5-g12-bf2  testsrc  -q:v 5 -g 12 -bf 2
enc q2-g12-bf3  testsrc  -q:v 2 -g 12 -bf 3
enc q5-g12-bf3  testsrc  -q:v 5 -g 12 -bf 3
enc t2-q2-bf2   testsrc2 -q:v 2 -g 12 -bf 2
enc t2-q5-bf2   testsrc2 -q:v 5 -g 12 -bf 2

for name in q2-allI q5-allI q2-g12-bf2 q5-g12-bf2 q2-g12-bf3 q5-g12-bf3 \
	t2-q2-bf2 t2-q5-bf2; do
	run "$name"
done
# $OUT/<name>.m4v and .nv12.gz stay behind: the analysis host has the MPEG-4
# decoder this box's build lacks, so every reference is built there.
say M4V_BQ_DONE
