#!/bin/sh
# Two questions about the MPEG-4 part 2 tail, one run.
#
# 1. Which reference picture does each captured frame line up with? The drain
#    leaves the last pictures uncompleted; adding a coded VOP to the stream
#    moves the delivered count by one, but a count alone does not say whether
#    that extra frame is the missing picture or a repeat of the previous one.
#    Hardware and software MPEG-4 output are not bit-identical, so this prints
#    an 8x8 grid of mean luma per captured picture and m4v-frame-compare.py
#    matches those against a software decode made on the analysis host, whose
#    ffmpeg has the MPEG-4 decoder this box's build lacks.
#
# 2. Is the number of VOPs a drain would have to replay a constant? Repeats the
#    reference stream's final VOP 2..5 times, then sweeps 0..4 on streams
#    encoded here. A fixed count only works if the picture count comes out the
#    same for every stream. The sweep also keeps each run's luma grid under
#    $D/sig/, because a count alone cannot say whether a replayed picture is a
#    real one or a stale duplicate of the last.
#
# Needs base.aa/base.ab (the 90-VOP reference, split for transfer), dup.aa/
# dup.ab, lastvop.00 and res-switch-test under $BIN, an ffmpeg with the mpeg4
# encoder, and nothing else holding /dev/video0.
set -u
D=/root/venc-deploy/p6
BIN=/root/venc-deploy
LOG=$D/m4v-tail-hash.log
VOS='\000\000\001\261'
: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }
# One captured picture, canvas-aligned: ALIGN(ALIGN(320,64)*ALIGN(240,64),64K)
# luma plus half that for chroma.
FRAME=196608

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
for k in 3 5; do
	cp "$D/base.m4v" "$D/last$k.m4v"
	i=0; while [ $i -lt $k ]; do cat "$D/lastvop.bin" >> "$D/last$k.m4v"; i=$((i+1)); done
	printf '%b' "$VOS" >> "$D/last$k.m4v"
done

say "## tail variants"
for name in base last1 last2 last3 last5 last1vos last2vos dup; do
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

# 2 s of testsrc at 25 fps is 50 VOPs, short enough to sweep and long enough to
# see the tail. Only 320x240: the client is told the stream's size on the
# command line, so another size would just decode as garbage.
say "## replay count across streams"
enc() { # name extra-encoder-args...
	name=$1; shift
	ffmpeg -y -hide_banner -loglevel error -f lavfi \
		-i "testsrc=size=320x240:rate=25" -t 2 -c:v mpeg4 -q:v 5 "$@" \
		-f m4v "$D/$name.m4v" || return 1
}
lastvop() { # stream vop-file -> prints VOP count
	python3 - "$1" "$2" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
pos = [i for i in range(len(d) - 3)
       if d[i:i+3] == b'\x00\x00\x01' and d[i+3] == 0xB6]
print(len(pos))
if pos:
    open(sys.argv[2], 'wb').write(d[pos[-1]:])
PY
}
enc s-ip
enc s-bf2 -bf 2
enc s-bf3 -bf 3 -g 12
for n in s-ip s-bf2 s-bf3; do
	nvop=$(lastvop "$D/$n.m4v" "$D/$n.vop")
	say "== $n vops=$nvop bytes=$(stat -c %s "$D/$n.m4v") vopbytes=$(stat -c %s "$D/$n.vop")"
	# Three sweeps: each line labels its own k, and a picture count that
	# moves between sweeps would mean none of this is worth reading.
	for round in 1 2 3; do
		line="  round=$round"
		for k in 0 1 2 3 4; do
			cp "$D/$n.m4v" "$D/$n.k.m4v"
			i=0; while [ $i -lt $k ]; do cat "$D/$n.vop" >> "$D/$n.k.m4v"; i=$((i+1)); done
			printf '%b' "$VOS" >> "$D/$n.k.m4v"
			dump=$D/$n.k.nv12
			rm -f "$dump"
			FRAME_DUMP=$dump timeout -k 5 120 "$BIN/res-switch-test" /dev/video0 \
				"$D/$n.k.m4v" MPG4 320 240 >> "$LOG" 2>&1
			line="$line k$k=$(($(stat -c %s "$dump" 2>/dev/null || echo 0) / FRAME))"
			mkdir -p "$D/sig"
			python3 "$D/m4v-frame-compare.py" --signature --canvas "$dump" \
				--size 320x240 > "$D/sig/$n-r$round-k$k.txt" 2>/dev/null
			rm -f "$dump" "$D/$n.k.m4v"
		done
		say "$line"
	done
done
# s-ip.m4v / s-bf2.m4v / s-bf3.m4v stay behind: the analysis host needs them
# for the software reference each kept signature is matched against.
say M4V_HASH_DONE
