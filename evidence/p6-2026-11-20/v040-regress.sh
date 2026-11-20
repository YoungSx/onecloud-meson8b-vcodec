#!/bin/sh
# Post-install regression for the v0.4.0 runtime package on the live box.
# Mirrors the v0.3.0 regression set so the two deploy records are comparable.
set -u
D=/root/venc-deploy
P=$D/p6
FF=$D/ffmpeg
RST=$D/dev/res-switch-test
ET=$D/encode-test
OUT=$P/v040-regress
LOG=$OUT/regress.log
mkdir -p "$OUT"
: > "$LOG"
say() { echo "$*" | tee -a "$LOG"; }

before=$(dmesg | wc -l)

say "== package"
say "source $(cat /opt/meson8b-vcodec/current/SOURCE_COMMIT)"
say "kernel $(cat /opt/meson8b-vcodec/current/KERNEL_RELEASE) $(uname -r)"
say "unit $(systemctl is-active meson8b-vcodec.service) $(systemctl is-enabled meson8b-vcodec.service)"
say "modules $(md5sum /opt/meson8b-vcodec/current/modules/*.ko | awk '{print $1}' | tr '\n' ' ')"
say "nodes $(ls /dev/video* | tr '\n' ' ')"

say "== mpeg2 90-picture reference"
rm -f "$OUT/regress.nv12" "$OUT/regress.tight.nv12"
FRAME_DUMP=$OUT/regress.nv12 timeout -k 5 180 "$RST" /dev/video0 \
	"$P/regress.m2v" MPG2 320 240 > "$OUT/regress.m2v.log" 2>&1
say "$(tail -1 "$OUT/regress.m2v.log")"
python3 "$P/tight.py" "$OUT/regress.nv12" "$OUT/regress.tight.nv12" | tee -a "$LOG"
# 90 pictures of 320x240 NV12; the constant is the v0.2.0 record carried
# forward by the v0.3.0 deploy. The software reference (sw-m2v.nv12) is a
# different decode path and is not byte-identical, so only the hash is
# compared, as in both earlier records.
got=$(md5sum "$OUT/regress.tight.nv12" | cut -d' ' -f1)
[ "$got" = f555b2b0373a7a052455c08d24c59391 ] &&
	say "tight md5 matches the recorded value" ||
	say "FAIL mpeg2 tight md5 $got"
rm -f "$OUT/regress.nv12" "$OUT/regress.tight.nv12"

say "== h264 1080p"
if timeout -k 5 180 "$FF" -nostdin -v error -c:v h264_v4l2m2m \
	-i "$D/p5/h264-1080p-baseline.mp4" -pix_fmt nv12 \
	-f framemd5 -y "$OUT/h264-1080p.md5" >> "$LOG" 2>&1; then
	python3 "$D/p5/verify-hashes.py" "$D/p5/h264-1080p-baseline.md5" \
		"$OUT/h264-1080p.md5" 2>&1 | tee -a "$LOG"
else
	say "FAIL h264 decode error"
fi

say "== hevc 1080p"
if timeout -k 5 180 "$FF" -nostdin -v error -c:v hevc_v4l2m2m \
	-i "$D/p5/1080p.mp4" -pix_fmt nv12 \
	-f framemd5 -y "$OUT/hevc-1080p.md5" >> "$LOG" 2>&1; then
	python3 "$D/p5/verify-hashes.py" "$D/p5/software-1080.md5" \
		"$OUT/hevc-1080p.md5" 2>&1 | tee -a "$LOG"
else
	say "FAIL hevc decode error"
fi

say "== encode"
timeout -k 5 120 "$ET" /dev/video1 "$OUT/enc.h264" "$OUT/enc.nv12" 320 240 10 \
	NV12 90 H264 2>&1 | tail -1 | tee -a "$LOG"
timeout -k 5 120 "$ET" /dev/video1 "$OUT/enc.jpg" "$OUT/enc.jpg.nv12" 320 240 3 \
	NV12 90 JPEG 2>&1 | tail -1 | tee -a "$LOG"
rm -f "$OUT"/enc.*

say "== services"
for s in "OpenList 5245" "SublinkPro 8000" "VoCat 7575" "HA 8123"; do
	set -- $s
	code=$(curl -s -o /dev/null -m 10 -w '%{http_code}' "http://127.0.0.1:$2/" || true)
	say "$1 $2 $code"
done
say "containers $(docker ps --format '{{.Names}}' | sort | tr '\n' ' ')"

say "== kernel"
new=$(dmesg | tail -n +$((before + 1)))
say "warn/oops/error count $(printf '%s\n' "$new" | grep -icE 'warn|oops|error')"
printf '%s\n' "$new" | grep -iE 'warn|oops|error' | tail -10 | tee -a "$LOG"
say "FAILS $(grep -c FAIL "$LOG")"
say "REGRESS_DONE"