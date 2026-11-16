#!/bin/sh
# Reduce each captured MPEG-4 picture to a luma grid so the dump can be lined
# up frame by frame.
#
# The drain leaves the last pictures of MPEG-4 part 2 uncompleted; adding a
# coded VOP to the drain stream moves the delivered count by one, but a count
# alone does not say whether that extra frame is the missing picture or a
# repeat of the previous one. Hardware and software MPEG-4 output are not
# bit-identical, so this prints an 8x8 grid of mean luma per captured picture
# and m4v-frame-compare.py matches those against a software decode made on the
# analysis host, whose ffmpeg has the MPEG-4 decoder this box's build lacks.
#
# Needs base.aa/base.ab (the 90-VOP reference, split for transfer), dup.aa/
# dup.ab, lastvop.00 and res-switch-test under $BIN, and nothing
# else holding /dev/video0.
set -u
D=/root/venc-deploy/p6
BIN=/root/venc-deploy
LOG=$D/m4v-tail-hash.log
VOS='\000\000\001\261'
: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

cat "$D/base.aa" "$D/base.ab" > "$D/base.m4v" || exit 1
cat "$D/dup.aa" "$D/dup.ab" > "$D/dup.m4v" || exit 1
# lastvop.bin is the reference stream's final coded VOP, transferred as its
# own chunk: a drain that repeats it hands the firmware a real picture start.
cat "$D"/lastvop.?? > "$D/lastvop.bin"
cp "$D/base.m4v" "$D/last1.m4v";    cat "$D/lastvop.bin" >> "$D/last1.m4v"
cp "$D/base.m4v" "$D/last2.m4v";    cat "$D/lastvop.bin" "$D/lastvop.bin" >> "$D/last2.m4v"
# Sequence end last, so the drain still ends the visual object sequence.
cp "$D/base.m4v" "$D/last1vos.m4v"; cat "$D/lastvop.bin" >> "$D/last1vos.m4v"
printf '%b' "$VOS" >> "$D/last1vos.m4v"
cp "$D/base.m4v" "$D/last2vos.m4v"; cat "$D/lastvop.bin" "$D/lastvop.bin" >> "$D/last2vos.m4v"
printf '%b' "$VOS" >> "$D/last2vos.m4v"

for name in base last1 last2 last1vos last2vos dup; do
	dump=$D/$name.nv12
	rm -f "$dump"
	FRAME_DUMP=$dump timeout -k 5 180 "$BIN/res-switch-test" /dev/video0 \
		"$D/$name.m4v" MPG4 320 240 >> "$LOG" 2>&1
	say "== $name stream_md5=$(md5sum "$D/$name.m4v" | cut -d' ' -f1) dump=$(stat -c %s "$dump" 2>/dev/null || echo 0)"
	python3 "$D/m4v-frame-compare.py" --signature --canvas "$dump" \
		--size 320x240 >> "$LOG" 2>&1
	rm -f "$dump"
done
rm -f "$D"/base.m4v "$D"/last?.m4v "$D"/last?vos.m4v "$D"/lastvop.bin "$D"/dup.m4v
say "M4V_HASH_DONE"