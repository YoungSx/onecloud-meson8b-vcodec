#!/bin/sh
cd /tmp/meson8b-vcodec-runtime
insmod meson8b-runtime.ko
insmod meson-vdec.ko
sleep 2
for dec in h264_v4l2m2m h264; do
  t0=$(date +%s.%N)
  if [ "$dec" = h264_v4l2m2m ]; then
    timeout -s INT -k 5s 90s /usr/local/bin/ffmpeg -nostdin -hide_banner -loglevel error -c:v $dec -num_output_buffers 4 -num_capture_buffers 8 -i baseline-1080p.mp4 -an -pix_fmt nv12 -fps_mode passthrough -f framemd5 -y /tmp/ab-$dec.md5
  else
    timeout -s INT -k 5s 90s /usr/local/bin/ffmpeg -nostdin -hide_banner -loglevel error -c:v $dec -i baseline-1080p.mp4 -an -pix_fmt nv12 -fps_mode passthrough -f framemd5 -y /tmp/ab-$dec.md5
  fi
  t1=$(date +%s.%N)
  echo "$dec $(echo "$t1-$t0" | bc)" >> /tmp/ab-results.txt
done
rmmod meson_vdec
rmmod meson8b_runtime
echo DONE >> /tmp/ab-results.txt
