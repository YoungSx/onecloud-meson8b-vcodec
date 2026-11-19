#!/bin/sh
# Keep the artefacts of one resolution switch and show, side by side, which
# pictures of each segment the joined run delivers.
#
#   switch-diag.sh FOURCC WIDTH HEIGHT A B
#
# WIDTH/HEIGHT is the initial OUTPUT format. Artifacts go to
# /root/venc-deploy/dev/switch-diag/ (hash lists, logs, numbered dumps).
set -u
D=/root/venc-deploy/dev
W=$D/switch-diag
COUNT=${COUNT:-8}
fourcc=${1:?usage: switch-diag.sh FOURCC WIDTH HEIGHT A B}
width=${2:?} height=${3:?}
a=${4:?} b=${5:?}
rm -rf "$W"
mkdir -p "$W"

run() { # name file
	CAPTURE_BUFFERS=$COUNT FRAME_HASH=$W/$1.hash timeout -k 5 180 \
		"$D/res-switch-test" /dev/video0 "$2" "$fourcc" "$width" "$height" \
		> "$W/$1.log" 2>&1
	printf '%s rc=%s\n' "$1" "$?"
}

cat "$a" > "$W/a"
cat "$b" > "$W/b"
cat "$W/a" "$W/b" > "$W/ab"

for n in a b ab; do
	run "$n" "$W/$n"
	echo "== $n pics=$(wc -l < "$W/$n.hash") $(tail -1 "$W/$n.log")"
done

cat "$W/a.hash" "$W/b.hash" > "$W/want"
cut -d' ' -f2- "$W/ab.hash" > "$W/got"
awk 'NR <= 50 { printf "want%02d %s\n", NR, $2 }' "$W/want" > "$W/want.num"
awk 'NR <= 50 { printf "got%02d  %s\n", NR, $2 }' "$W/got" > "$W/got.num"
echo "== joined against the two runs"
paste "$W/want.num" "$W/got.num"
echo "== joined log"
cat "$W/ab.log"
echo "== dmesg"
dmesg | tail -20
