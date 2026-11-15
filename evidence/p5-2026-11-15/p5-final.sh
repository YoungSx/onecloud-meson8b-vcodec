#!/bin/bash
# P5: output conversion cost and correctness, old module vs final module.
# Samples marked unsafe overrun buffers on the old sizing; never run them there.
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
    dmesg | awk -v t="$since" -F'[][]' '$2 + 0 > t' | grep -v usbfs > final-dmesg.log
    echo "dmesg warnings $(grep -ciE 'warn|bug|oops|refcount' final-dmesg.log)"
    echo "FAILS=$fails"
}
trap restore EXIT
mk() { # NAME HEVC_METADATA_OPTIONS; exact software crop needs -flags unaligned
    [[ -s $1.mp4 ]] || nice -n 19 $SW -nostdin -v error -y -i 1080p.mp4 -c copy \
        -bsf:v "hevc_metadata=$2" "$1.mp4"
    [[ -s software-$1.md5 ]] || nice -n 19 $SW -nostdin -v error -y -flags:v unaligned \
        -i "$1.mp4" -pix_fmt nv12 -f framemd5 "software-$1.md5"
}
mk hevc-left64 crop_left=64:crop_top=32
mk hevc-left6 crop_left=6:crop_top=2
mk hevc-left16 crop_left=16:crop_right=40
mk hevc-left30 crop_left=30:crop_top=18
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
safe() { # BUILD
    run "$1-hevc-crop" $FF hevc_v4l2m2m crop.mp4 software-crop.md5
    run "$1-hevc-1080" $FF hevc_v4l2m2m 1080p.mp4 software-1080.md5
    run "$1-h264-1080" $SW h264_v4l2m2m h264-1080p-baseline.mp4 h264-1080p-baseline.md5
    hevc "$1" hevc-left64
    hevc "$1" hevc-left6
}
swap /root/venc-deploy/p5/meson-vdec-instrumented.ko || exit 1
safe old
swap /root/venc-deploy/p5/meson-vdec-final.ko || exit 1
for i in 1 2; do
    safe "final$i"
    hevc "final$i" hevc-left16
    hevc "final$i" hevc-left30
    h264 "final$i" h264-1360x768
    h264 "final$i" h264-1600x900
done
echo DONE
