#!/bin/sh
# Repeat the 300-frame H.264 1080p decode the way the one real failure
# happened: under mixed load, not alone.
#
# The overnight soak lost a frame on one H.264 run in 1524. The follow-up
# repeat ran that same decode 200 times on an idle box and never reproduced
# it. The repeat therefore tested a different condition from the failure: in
# the soak, each H.264 decode runs immediately after an HEVC decode and is
# interleaved with H.264 and JPEG hardware encodes, all through the same
# 64 MiB CMA.
#
# This keeps that ordering and counts frames on every run, stopping at the
# first short output with its framemd5 and dmesg tail kept. It does not stop
# on a hash mismatch -- that is a different failure and the earlier runs
# already show it does not happen -- so a mismatch is only logged.
#
# Usage: h264-shortload.sh [ROUNDS]   (default 120; ~40 s each with load)
set -u
D=/root/venc-deploy
FF=$D/ffmpeg
ET=$D/encode-test
ROUNDS=${1:-120}
LOG=$D/p6/h264-shortload.log
REF=$D/p5/h264-1080p-baseline.md5
: > "$LOG"
ok=0
short=0
mismatch=0
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

say "== start rounds=$ROUNDS"
for i in $(seq 1 "$ROUNDS"); do
    # Same order as the soak: HEVC first, then the H.264 decode under test.
    timeout -k 5 90 "$FF" -nostdin -v error -c:v hevc_v4l2m2m \
        -num_capture_buffers 8 -i "$D/1080p.mp4" -pix_fmt nv12 \
        -f framemd5 -y /tmp/sl-hevc.md5 > /dev/null 2>&1

    rm -f /tmp/sl-h264.md5
    timeout -k 5 90 "$FF" -nostdin -v error -c:v h264_v4l2m2m \
        -num_capture_buffers 10 -i "$D/p5/h264-1080p-baseline.mp4" \
        -pix_fmt nv12 -f framemd5 -y /tmp/sl-h264.md5 2>/dev/null
    n=$(grep -c '^0,' /tmp/sl-h264.md5)

    if [ "$n" -ne 300 ]; then
        short=$((short + 1))
        say "round=$i SHORT n=$n"
        cp /tmp/sl-h264.md5 "$D/p6/short-$i.md5" 2>/dev/null
        tail -40 /dev/kmsg > "$D/p6/short-$i.dmesg" 2>/dev/null \
            || dmesg | tail -40 > "$D/p6/short-$i.dmesg"
        break
    fi
    if python3 "$D/p5/verify-hashes.py" "$REF" /tmp/sl-h264.md5 \
        > /dev/null 2>&1; then
        ok=$((ok + 1))
    else
        mismatch=$((mismatch + 1))
        say "round=$i HASHMISMATCH n=$n"
    fi

    # The rest of the soak's per-round work, so the CMA and encoder load match.
    timeout -k 5 60 "$ET" /dev/video1 /tmp/sl.h264 /tmp/sl.nv12 \
        320 240 10 NV12 90 H264 > /dev/null 2>&1
    timeout -k 5 60 "$ET" /dev/video1 /tmp/sl.mjpg /tmp/sl.nv12 \
        320 240 3 NV12 90 JPEG > /dev/null 2>&1

    if [ $((i % 10)) -eq 0 ]; then
        say "round=$i ok=$ok short=$short mismatch=$mismatch $(grep '^CmaFree' /proc/meminfo)"
    fi
done
say "SHORTLOAD_DONE rounds=$ROUNDS ok=$ok short=$short mismatch=$mismatch"
