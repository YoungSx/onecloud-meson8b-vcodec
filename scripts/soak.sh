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
# The failing framemd5 and the persistent kern.log window are the only
# attribution evidence for a short output; dmesg alone has none of the
# timeline and the framemd5 was overwritten last time.
keep_failure() {
    cp /tmp/soak-hevc.md5 "$D/soak-fail-hevc.md5" 2>/dev/null || true
    cp /tmp/soak-h264.md5 "$D/soak-fail-h264.md5" 2>/dev/null || true
    # -a: the log holds NUL bytes, and grep would then print only
    # "Binary file matches" and lose the timeline.
    LC_ALL=C grep -a "$(LC_ALL=C date +'%b %e %H')" /var/log/kern.log 2>/dev/null \
        | tail -200 > "$D/soak-fail-kern.log" || true
    # ffmpeg reports why it gave up on stderr; without it the log says only
    # that the decode failed, and a zero-frame output cannot be attributed.
    cp /tmp/soak-hevc.log "$D/soak-fail-hevc.log" 2>/dev/null || true
    cp /tmp/soak-h264.log "$D/soak-fail-h264.log" 2>/dev/null || true
    for f in pool_slots pool_bytes pool_waiting_peak; do
        printf '%s=%s ' "$f" "$(cat "/sys/kernel/debug/meson-vdec/$f" 2>/dev/null)" \
            >> "$D/soak-fail-kern.log"
    done
    echo >> "$D/soak-fail-kern.log"
}

say "== soak start minutes=$MINUTES"
start=$(date +%s)
end=$((start + MINUTES * 60))
iter=0
while [ "$(date +%s)" -lt "$end" ]; do
    iter=$((iter + 1))
    rm -f /tmp/soak-hevc.md5 /tmp/soak-h264.md5
    if ! timeout -k 5 90 "$FF" -nostdin -v error -c:v hevc_v4l2m2m \
        -i "$D/1080p.mp4" -pix_fmt nv12 \
        -f framemd5 -y /tmp/soak-hevc.md5 > /tmp/soak-hevc.log 2>&1; then
        say "iter=$iter FAIL hevc decode"; FAILS=$((FAILS + 1)); keep_failure; break
    fi
    if ! python3 "$D/p5/verify-hashes.py" "$D/p5/software-1080.md5" \
        /tmp/soak-hevc.md5 >> "$LOG" 2>&1; then
        say "iter=$iter FAIL hevc hashes"; FAILS=$((FAILS + 1)); keep_failure; break
    fi
    if [ $((iter % 5)) -eq 0 ]; then
        if ! timeout -k 5 90 "$FF" -nostdin -v error -c:v h264_v4l2m2m \
            -i "$D/p5/h264-1080p-baseline.mp4" \
            -pix_fmt nv12 -f framemd5 -y /tmp/soak-h264.md5 \
            > /tmp/soak-h264.log 2>&1; then
            say "iter=$iter FAIL h264 decode"; FAILS=$((FAILS + 1)); keep_failure; break
        fi
        if ! python3 "$D/p5/verify-hashes.py" \
            "$D/p5/h264-1080p-baseline.md5" /tmp/soak-h264.md5 >> "$LOG" 2>&1; then
            say "iter=$iter FAIL h264 hashes"; FAILS=$((FAILS + 1)); keep_failure; break
        fi
    fi
    if ! timeout -k 5 60 "$ET" /dev/video1 /tmp/soak.h264 /tmp/soak.nv12 \
        320 240 10 NV12 90 H264 >> "$LOG" 2>&1; then
        say "iter=$iter FAIL h264 encode"; FAILS=$((FAILS + 1)); keep_failure; break
    fi
    if ! timeout -k 5 60 "$ET" /dev/video1 /tmp/soak.mjpg /tmp/soak.nv12 \
        320 240 3 NV12 90 JPEG >> "$LOG" 2>&1; then
        say "iter=$iter FAIL jpeg encode"; FAILS=$((FAILS + 1)); keep_failure; break
    fi
    temp=$(cat /sys/class/thermal/thermal_zone0/temp)
    mem=$(awk '/MemFree|MemAvailable/ {printf "%s=%d ", $1, $2/1024}' /proc/meminfo)
    cma=$(grep -E '^Cma' /proc/meminfo | awk '{printf "%s=%d ", $1, $2/1024}')
    say "iter=$iter temp=$((temp / 1000))C ${mem}${cma}video0=$(test -e /dev/video0 && echo up) video1=$(test -e /dev/video1 && echo up)"
done
say "SOAK_DONE iter=$iter FAILS=$FAILS elapsed=$(( $(date +%s) - start ))s"
