#!/bin/sh
# Repeat the 300-frame H.264 1080p decode and characterise the occasional
# short output: which frame is missing, and does it ever differ in content.
#
# The overnight soak ended on a 299-of-300 frame decode. This repeats that
# exact input N times so a recurrence is caught with its first differing frame
# rather than only as a count.
set -u
D=/root/venc-deploy
REF=$D/p5/h264-1080p-baseline.md5
LOG=$D/p6/h264-tail-race.log
N=${1:-120}
: > "$LOG"
grep "^0," "$REF" | cut -d, -f2- > /tmp/race-ref.txt
for i in $(seq 1 "$N"); do
    timeout -k 5 90 "$D/ffmpeg" -nostdin -v error -c:v h264_v4l2m2m \
        -num_capture_buffers 10 -i "$D/p5/h264-1080p-baseline.mp4" \
        -pix_fmt nv12 -f framemd5 -y /tmp/race.md5 2>/dev/null
    rc=$?
    n=$(grep -c "^0," /tmp/race.md5)
    if [ "$n" -eq 300 ]; then
        if python3 "$D/p5/verify-hashes.py" "$REF" /tmp/race.md5 >/dev/null 2>&1; then
            echo "$i ok 300" >> "$LOG"
        else
            echo "$i HASHMISMATCH n=$n" >> "$LOG"
            cp /tmp/race.md5 "$D/p6/race-bad-$i.md5"
        fi
    else
        grep "^0," /tmp/race.md5 | cut -d, -f2- > /tmp/race-hw.txt
        # First differing output line: means a frame was dropped, not altered.
        first=$(diff /tmp/race-ref.txt /tmp/race-hw.txt | grep -m1 "^[<>]")
        echo "$i SHORT rc=$rc n=$n first_diff=$first" >> "$LOG"
        cp /tmp/race.md5 "$D/p6/race-short-$i.md5"
        # The persistent log keeps the timeline a dmesg tail can miss.
        LC_ALL=C grep "$(LC_ALL=C date +'%b %e %H')" /var/log/kern.log 2>/dev/null \
            | tail -200 > "$D/p6/race-short-$i.kern.log" || true
    fi
done
echo "ok=$(grep -c ' ok ' "$LOG") short=$(grep -c SHORT "$LOG") mismatch=$(grep -c HASHMISMATCH "$LOG")" >> "$LOG"
echo RACE_DONE >> "$LOG"