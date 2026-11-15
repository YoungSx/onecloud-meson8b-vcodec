#!/bin/bash
# P5: decoder output conversion cost per path, before/after the row swap.
set -u
cd /root/venc-deploy/p5 || exit 1
FF=/opt/meson8b-vcodec/current/tools/ffmpeg
H264FF=/usr/local/bin/ffmpeg
D=/sys/kernel/debug/meson-vdec
installed=/opt/meson8b-vcodec/current/modules/meson-vdec.ko
fails=0
since=$(dmesg | tail -1 | sed -n 's/^\[ *\([0-9.]*\)\].*/\1/p')
swap() { # decoder only; no clients allowed
    [[ -z $(fuser /dev/video0 2>/dev/null) ]] || { echo "decoder busy"; return 1; }
    rmmod meson_vdec && insmod "$1" || { insmod $installed; return 1; }
    for n in /sys/class/video4linux/video*; do
        [[ $(cat "$n/name") == meson-video-decoder ]] && echo "decoder /dev/${n##*/} from $(sha256sum "$1" | cut -c1-16)"
    done
    return 0
}
restore() {
    swap $installed
    dmesg | awk -v t="$since" -F'[][]' '$2 + 0 > t' | grep -v usbfs > convert-dmesg.log
    echo "dmesg warnings $(grep -ciE 'warn|bug|oops|refcount' convert-dmesg.log)"
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
for build in instrumented neon; do
    swap /root/venc-deploy/p5/meson-vdec-$build.ko || exit 1
    for i in 1 2; do
        run "$build-hevc-crop-$i" $FF hevc_v4l2m2m crop.mp4 software-crop.md5
        run "$build-hevc-1080-$i" $FF hevc_v4l2m2m 1080p.mp4 software-1080.md5
        run "$build-h264-1080-$i" $H264FF h264_v4l2m2m h264-1080p-baseline.mp4 h264-1080p-baseline.md5
    done
done
echo DONE
