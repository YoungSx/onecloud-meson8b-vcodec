#!/bin/sh
# Check resolution switches on the target: decode each segment alone, then
# the segments back to back as one stream, and require the joined run to
# return exactly the segments' pictures in order and end in a clean EOS.
#
#   res-switch-check.sh FOURCC WIDTH HEIGHT SEGMENT...
#
# Segments are elementary streams (Annex B for H.264/HEVC) that each start
# with their own sequence headers; neighbours should differ in resolution.
# WIDTH and HEIGHT are the initial OUTPUT format. CAPTURE_COUNTS lists the
# CAPTURE buffer counts to try (default "1 4 8").
set -u
RST=${RST:-/root/venc-deploy/dev/res-switch-test}
fourcc=${1:?usage: res-switch-check.sh FOURCC WIDTH HEIGHT SEGMENT...}
width=${2:?} height=${3:?}
shift 3
[ $# -ge 2 ] || { echo 'need at least two segments' >&2; exit 2; }
work=$(mktemp -d /root/venc-deploy/dev/switch.XXXXXX)
trap 'rm -rf "$work"' EXIT

# file count; writes file.hash and file.log
run() {
	CAPTURE_BUFFERS=$2 FRAME_HASH=$1.hash timeout -k 5 180 \
		"$RST" /dev/video0 "$1" "$fourcc" "$width" "$height" > "$1.log" 2>&1
}

status=0
for count in ${CAPTURE_COUNTS:-1 4 8}; do
	: > "$work/joined"
	: > "$work/want"
	n=0
	for segment do
		n=$((n + 1))
		cp "$segment" "$work/$n"
		run "$work/$n" "$count"
		cut -d' ' -f2- "$work/$n.hash" >> "$work/want"
		cat "$segment" >> "$work/joined"
	done
	run "$work/joined" "$count"
	cut -d' ' -f2- "$work/joined.hash" > "$work/got"
	end=$(tail -1 "$work/joined.log")
	if cmp -s "$work/want" "$work/got" && [ "${end%% *}" = EOS ]; then
		echo "PASS $fourcc count=$count pictures=$(wc -l < "$work/got") $end"
	else
		status=1
		echo "FAIL $fourcc count=$count pictures=$(wc -l < "$work/got")/$(wc -l < "$work/want") end=$end"
		grep '^SRC_CHANGE' "$work/joined.log" | tr '\n' ' '
		diff "$work/want" "$work/got" | grep -m 1 '^[0-9]'
	fi
done
exit $status
