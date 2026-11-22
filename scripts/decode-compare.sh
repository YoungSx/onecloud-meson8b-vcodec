#!/bin/sh
# Compare hardware decode output of a candidate decoder module against a
# reference module on the target.
#
#   decode-compare.sh ref DIR          record references with the loaded module
#   decode-compare.sh check DIR [N...] decode again, once per CAPTURE buffer
#                                      count N ("default" = the client's own)
#
# FFmpeg compares framemd5 (timestamps and hashes of every picture);
# res-switch-test compares the hash of every picture and its final status
# line without the buffer count, which includes empty buffers. The reference run uses the CAPTURE counts the v0.3.0 decoder needed
# (H.264 10, HEVC 8). For streams whose last pictures vary from run to run,
# the optional last column limits the comparison to that many leading
# pictures and drops the status line.
set -u
D=/root/venc-deploy
FF=$D/ffmpeg	# independent tool with the HEVC drain fix
RST=$D/dev/res-switch-test
mode=${1:?usage: decode-compare.sh ref|check DIR [N...]}
out=${2:?usage: decode-compare.sh ref|check DIR [N...]}
shift 2

# name client format input [prefix]; rst takes FOURCC[:WIDTH:HEIGHT]
# p6/B-flip.m4v stays out: its pictures hit the MPEG-4 B-frame defect in
# docs/validation.md and differ between runs on v0.3.0 as well.
samples() {
	cat <<SAMPLES
h264-1080p ff h264 $D/p5/h264-1080p-baseline.mp4
h264-1360x768 ff h264 $D/p5/h264-1360x768.mp4
h264-1600x900 ff h264 $D/p5/h264-1600x900.mp4
h264-bframes ff h264 $D/source.mp4
hevc-1080p ff hevc $D/p5/1080p.mp4
hevc-crop ff hevc $D/p5/crop.mp4
hevc-left6 ff hevc $D/p5/hevc-left6.mp4
hevc-left64 ff hevc $D/p5/hevc-left64.mp4
rst-h264 rst H264:1360:768 $D/dev/h264-1360x768.h264 30
mpeg2 rst MPG2:320:240 $D/p6/regress.m2v 88
mpeg2-bframes rst MPG2:320:240 $D/p6/m2v-bf.m2v 148
mpeg4 rst MPG4:320:240 $D/p6/m4v-repro.m4v
vc1 rst VC1G:320:240 $D/p6/SA00050.vc1 13
vc1-small rst VC1G:176:144 $D/p6/SA00040.vc1 5
SAMPLES
}

# client format input count result [prefix]
decode() {
	# sh has no locals: these names must not clash with the callers' loops
	client=$1 format=$2 input=$3 buffers=$4 result=$5 prefix=${6:-}
	if [ "$client" = rst ]; then
		[ "$buffers" = default ] && buffers=8
		rm -f "$result.hash"
		# A clean EOS is not required: the status line is compared too.
		# shellcheck disable=SC2046 # FOURCC WIDTH HEIGHT are separate arguments
		CAPTURE_BUFFERS=$buffers FRAME_HASH=$result.hash timeout -k 5 120 \
			"$RST" /dev/video0 "$input" $(echo "$format" | tr : ' ') \
			> "$result.log" 2>&1
		if [ -n "$prefix" ]; then
			head -n "$prefix" "$result.hash"
		else
			cat "$result.hash"
			tail -1 "$result.log" | sed 's/ *frames=[0-9]*//'
		fi > "$result"
		rm -f "$result.hash" "$result.log"
		return 0
	fi
	opt=
	[ "$buffers" = default ] || opt="-num_capture_buffers $buffers"
	# shellcheck disable=SC2086 # the count option is split on purpose
	timeout -k 5 120 "$FF" -nostdin -v error -c:v "${format}_v4l2m2m" $opt -i "$input" \
		-pix_fmt nv12 -filter_threads 1 -threads:v 1 -f framemd5 -y "$result.raw" || { rm -f "$result.raw"; return 1; }
	if [ -n "$prefix" ]; then
		grep -v '^#' "$result.raw" | head -n "$prefix"
	else
		grep -v '^#' "$result.raw"
	fi > "$result"
	rm -f "$result.raw"
}

mkdir -p "$out"
# res-switch-test reads Annex B elementary streams.
[ -s "$D/dev/h264-1360x768.h264" ] ||
	"$FF" -nostdin -v error -i "$D/p5/h264-1360x768.mp4" -c copy \
		-bsf:v h264_mp4toannexb -f h264 -y "$D/dev/h264-1360x768.h264"
case $mode in
ref)
	samples | while read -r name client format input prefix; do
		case $format in h264) n=10 ;; hevc) n=8 ;; *) n=default ;; esac
		if decode "$client" "$format" "$input" "$n" "$out/$name.md5" "$prefix"; then
			echo "ref $name lines=$(wc -l < "$out/$name.md5") $(head -1 "$out/$name.md5" | cut -c1-60)"
		else
			echo "ref $name FAILED"
		fi
	done
	;;
check)
	[ $# -gt 0 ] || set -- default 1 2 3 4 32
	for count do
		samples | while read -r name client format input prefix; do
			# FFmpeg takes at least 2 CAPTURE buffers.
			[ "$count" = 1 ] && [ "$client" = ff ] && continue
			got=$out/$name.$count.md5
			if ! decode "$client" "$format" "$input" "$count" "$got" "$prefix"; then
				echo "FAIL $name count=$count decode error"
			elif cmp -s "$out/$name.md5" "$got"; then
				echo "PASS $name count=$count $(head -1 "$got" | cut -c1-48)"
				rm -f "$got"
			else
				echo "FAIL $name count=$count lines=$(wc -l < "$got")/$(wc -l < "$out/$name.md5") $(head -1 "$got" | cut -c1-48)"
			fi
		done
	done
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
dmesg | tail -8 | grep -iE 'vdec|venc|cma|oops|bug|warn' || true
