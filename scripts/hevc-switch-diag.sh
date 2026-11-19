#!/bin/sh
# Keep the artefacts of one HEVC resolution switch: decode each segment
# alone, then a -> b -> a joined, and print where the pictures stop.
set -u
D=/root/venc-deploy/dev
S=$D/switch-in
W=$D/switch-diag
COUNT=${COUNT:-8}
rm -rf "$W"
mkdir -p "$W"

run() { # name
	CAPTURE_BUFFERS=$COUNT FRAME_HASH=$W/$1.hash timeout -k 5 180 \
		"$D/res-switch-test" /dev/video0 "$W/$1" HEVC 320 240 \
		> "$W/$1.log" 2>&1
}

cp "$S/hevc-320x240.hevc" "$W/a"
cp "$S/hevc-1080p.hevc" "$W/b"
cat "$W/a" "$W/b" "$W/a" > "$W/aba"

for n in a b aba; do
	run "$n"
	echo "== $n pics=$(wc -l < "$W/$n.hash") $(tail -1 "$W/$n.log")"
done
echo "== hashes"
for n in a b; do
	echo "-- $n"; cut -d' ' -f2- "$W/$n.hash"
done
echo "-- aba"; cut -d' ' -f2- "$W/aba.hash"
echo "== aba log"
cat "$W/aba.log"