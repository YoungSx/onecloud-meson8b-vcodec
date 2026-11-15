#!/bin/sh
# Bounded soak on the live box: HEVC 1080p decode against the software
# reference every round, H.264 1080p decode every fifth round, and H.264 +
# JPEG hardware encode every round. Records temperature, memory, CMA and
# service state. Stops after MINUTES or on the first failure.
set -u
D=/root/venc-deploy
FF=$D/ffmpeg
ET=$D/encode-test
MINUTES=${1:-30}
LOG=$D/soak.log
FAILS=0
: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

say "== soak start minutes=$MINUTES"
start=$(date +%s)
end=$((start + MINUTES * 60))
iter=0
while [ "$(date +%s)" -lt "$end" ]; do
    iter=$((iter + 1))
    rm -f /tmp/soak-hevc.md5 /tmp/soak-h264.md5
    if ! timeout -k 5 90 "$FF" -nostdin -v error -c:v hevc_v4l2m2m \
        -num_capture_buffers 8 -i "$D/1080p.mp4" -pix_fmt nv12 \
        -f framemd5 -y /tmp/soak-hevc.md5; then
        say "iter=$iter FAIL hevc decode"; FAILS=$((FAILS + 1)); break
    fi
    if ! python3 "$D/p5/verify-hashes.py" "$D/p5/software-1080.md5" \
        /tmp/soak-hevc.md5 >> "$LOG" 2>&1; then
        say "iter=$iter FAIL hevc hashes"; FAILS=$((FAILS + 1)); break
    fi
    if [ $((iter % 5)) -eq 0 ]; then
        if ! timeout -k 5 90 "$FF" -nostdin -v error -c:v h264_v4l2m2m \
            -num_capture_buffers 10 -i "$D/p5/h264-1080p-baseline.mp4" \
            -pix_fmt nv12 -f framemd5 -y /tmp/soak-h264.md5; then
            say "iter=$iter FAIL h264 decode"; FAILS=$((FAILS + 1)); break
        fi
        if ! python3 "$D/p5/verify-hashes.py" \
            "$D/p5/h264-1080p-baseline.md5" /tmp/soak-h264.md5 >> "$LOG" 2>&1; then
            say "iter=$iter FAIL h264 hashes"; FAILS=$((FAILS + 1)); break
        fi
    fi
    if ! timeout -k 5 60 "$ET" /dev/video1 /tmp/soak.h264 /tmp/soak.nv12 \
        320 240 10 NV12 90 H264 >> "$LOG" 2>&1; then
        say "iter=$iter FAIL h264 encode"; FAILS=$((FAILS + 1)); break
    fi
    if ! timeout -k 5 60 "$ET" /dev/video1 /tmp/soak.mjpg /tmp/soak.nv12 \
        320 240 3 NV12 90 JPEG >> "$LOG" 2>&1; then
        say "iter=$iter FAIL jpeg encode"; FAILS=$((FAILS + 1)); break
    fi
    temp=$(cat /sys/class/thermal/thermal_zone0/temp)
    mem=$(awk '/MemFree|MemAvailable/ {printf "%s=%d ", $1, $2/1024}' /proc/meminfo)
    cma=$(grep -E '^Cma' /proc/meminfo | awk '{printf "%s=%d ", $1, $2/1024}')
    say "iter=$iter temp=$((temp / 1000))C ${mem}${cma}video0=$(test -e /dev/video0 && echo up) video1=$(test -e /dev/video1 && echo up)"
done
say "SOAK_DONE iter=$iter FAILS=$FAILS elapsed=$(( $(date +%s) - start ))s"
