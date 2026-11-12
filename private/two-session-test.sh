#!/bin/sh
# Two hw sessions back to back without unloading modules, then a third after
# a full stop; capture dmesg markers around each.
cd /tmp/meson8b-vcodec-runtime
echo "M8DBG TEST start $(date +%T)" > /dev/kmsg
insmod meson8b-runtime.ko
insmod meson-vdec.ko
for i in 1 2 3; do
  echo "M8DBG SESSION $i begin $(date +%T)" > /dev/kmsg
  timeout -s INT -k 5s 60s /usr/local/bin/ffmpeg -nostdin -hide_banner -loglevel error \
    -c:v h264_v4l2m2m -num_output_buffers 4 -num_capture_buffers 8 \
    -i baseline-1080p.mp4 -an -pix_fmt nv12 -f framemd5 -y /tmp/two-$i.md5
  rc=$?
  echo "M8DBG SESSION $i end rc=$rc $(date +%T)" > /dev/kmsg
done
rmmod meson_vdec
rmmod meson8b_runtime
echo "M8DBG TEST done $(date +%T)" > /dev/kmsg
