#!/bin/sh
# Count how many pictures the MPEG-2 B-frame stream delivers per run.
#
# drain-eos.md records "MPEG-2 B-frame stream (150 pictures): 150 after the
# fix". A rerun on 2026-11-17 delivered 149 twice, then 150, then 149, so the
# count is not fixed. This prints one line per run so the spread is visible
# instead of a single sample being read as the answer.
#
# Dumps go under /root, never /tmp: /tmp is a 496 MiB tmpfs and a truncated
# dump reads exactly like a decoder that dropped frames.
set -u
D=/root/venc-deploy
OUT=$D/p6/bf-delivery
N=${1:-12}
mkdir -p "$OUT"
: > "$OUT/bf-delivery.log"
for i in $(seq 1 "$N"); do
    rm -f "$OUT/run.nv12"
    FRAME_DUMP="$OUT/run.nv12" timeout -k 5 60 $D/res-switch-test /dev/video0 \
        $D/p6/m2v-bf.m2v MPG2 320 240 > "$OUT/run.out" 2>&1
    bytes=$(wc -c < "$OUT/run.nv12" 2>/dev/null || echo 0)
    echo "$i pictures=$((bytes / 196608)) $(cat "$OUT/run.out")" >> "$OUT/bf-delivery.log"
done
echo "p150=$(grep -c 'pictures=150' "$OUT/bf-delivery.log") p149=$(grep -c 'pictures=149' "$OUT/bf-delivery.log")" >> "$OUT/bf-delivery.log"
cat "$OUT/bf-delivery.log"
