#!/bin/bash
# P5: final module after moving the window copy into a host-testable header.
set -u
cd /root/venc-deploy/p5 || exit 1
FF=/opt/meson8b-vcodec/current/tools/ffmpeg
SW=/usr/local/bin/ffmpeg
D=/sys/kernel/debug/meson-vdec
installed=/opt/meson8b-vcodec/current/modules/meson-vdec.ko
fails=0
since=$(dmesg | tail -1 | sed -n 's/^\[ *\([0-9.]*\)\].*/\1/p')
swap() { # decoder only; no clients allowed
    [[ -z $(fuser /dev/video0 2>/dev/null) ]] || { echo "decoder busy"; return 1; }
    rmmod meson_vdec && insmod "$1" || { insmod $installed; return 1; }
    echo "decoder from $(sha256sum "$1" | cut -c1-16)"
}
restore() {
    swap $installed
    dmesg | awk -v t="$since" -F'[][]' '$2 + 0 > t' | grep -v usbfs > window-dmesg.log
    echo "dmesg warnings $(grep -ciE 'warn|bug|oops|refcount' window-dmesg.log)"
    echo "FAILS=$fails"
}
trap restore EXIT
counters() { echo "$(cat $D/converted_frames) $(cat $D/cropped_frames) $(cat $D/convert_ns)"; }
run() { # LABEL FFMPEG CODEC SAMPLE REFERENCE
    local c0 c1 f0 k0 n0 f1 k1 n1
    c0=$(counters)
    timeout -k 5 120 "$2" -nostdin -v error -c:v "$3" -num_capture_buffers 10 -i "$4" \
        -pix_fmt nv12 -f framemd5 -y "out-$1.md5"
    c1=$(counters)
    read -r f0 k0 n0 <<< "$c0"; read -r f1 k1 n1 <<< "$c1"
    local frames=$((f1 - f0))
    echo "$1: frames $frames cropped $((k1 - k0)) convert_us/frame $(( frames ? (n1 - n0) / frames / 1000 : 0 ))"
    if python3 verify-hashes.py "$5" "out-$1.md5"; then echo "PASS $1 hashes"; else echo "FAIL $1 hashes"; fails=$((fails + 1)); fi
}
hevc() { run "$1-$2" $FF hevc_v4l2m2m "$2.mp4" "software-$2.md5"; }
h264() { run "$1-$2" $SW h264_v4l2m2m "$2.mp4" "software-$2.md5"; }
swap /root/venc-deploy/p5/meson-vdec-window.ko || exit 1
for i in 1 2; do
    run "window$i-hevc-crop" $FF hevc_v4l2m2m crop.mp4 software-crop.md5
    run "window$i-hevc-1080" $FF hevc_v4l2m2m 1080p.mp4 software-1080.md5
    run "window$i-h264-1080" $SW h264_v4l2m2m h264-1080p-baseline.mp4 h264-1080p-baseline.md5
    hevc "window$i" hevc-left64
    hevc "window$i" hevc-left6
    hevc "window$i" hevc-left16
    hevc "window$i" hevc-left30
    h264 "window$i" h264-1360x768
    h264 "window$i" h264-1600x900
done
echo DONE
