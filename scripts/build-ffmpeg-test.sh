#!/usr/bin/env bash
# Independent static test tool; never replaces the WanOS ffmpeg binary.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cache="$root/sources/ffmpeg"
mkdir -p "$cache"
archive="$cache/ffmpeg-7.0.2.tar.xz"
if [[ ! -f "$archive" ]]; then
    curl -fsSL --max-time 120 https://ffmpeg.org/releases/ffmpeg-7.0.2.tar.xz -o "$archive"
fi
printf '%s  %s\n' 8646515b638a3ad303e23af6a3587734447cb8fc0a0c064ecdb8e95c4fd8b389 "$archive" | sha256sum -c -
if [[ ! -d "$cache/ffmpeg-7.0.2" ]]; then tar -xJf "$archive" -C "$cache"; fi
cd "$cache/ffmpeg-7.0.2"
if patch -p1 --dry-run --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-drain.patch" >/dev/null 2>&1; then
    patch -p1 --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-drain.patch"
elif ! patch -R -p1 --dry-run --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-drain.patch" >/dev/null 2>&1; then
    echo 'FFmpeg drain patch does not apply to this source' >&2
    exit 1
fi
if patch -p1 --dry-run --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-rate-limits.patch" >/dev/null 2>&1; then
    patch -p1 --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-rate-limits.patch"
elif ! patch -R -p1 --dry-run --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-rate-limits.patch" >/dev/null 2>&1; then
    echo 'FFmpeg rate/input patch does not apply to this source' >&2
    exit 1
fi
./configure --arch=arm --cpu=cortex-a5 --target-os=linux \
  --cross-prefix=arm-linux-gnueabihf- --cc=arm-linux-gnueabihf-gcc-11 \
  --enable-cross-compile --disable-shared --enable-static --extra-ldflags=-static \
  --disable-doc --disable-debug --disable-autodetect --disable-everything \
  --enable-ffmpeg --enable-ffprobe --enable-avformat --enable-avcodec --enable-avfilter \
  --enable-swscale --enable-swresample --enable-protocol=file,pipe,http,tcp \
  --enable-demuxer=mov,hevc,h264,rawvideo,mjpeg,mpegts,aac \
  --enable-muxer=framehash,framemd5,null,rawvideo,hevc,h264,mjpeg,mpegts,hls \
  --enable-parser=hevc,h264,mjpeg,aac \
  --enable-decoder=hevc,hevc_v4l2m2m,h264,h264_v4l2m2m,mjpeg,rawvideo,aac \
  --enable-encoder=rawvideo,wrapped_avframe,h264_v4l2m2m,aac \
  --enable-bsf=hevc_mp4toannexb,h264_mp4toannexb \
  --enable-filter=buffer,buffersink,format,scale,null,setpts,asetpts,aresample,anull,psnr --enable-v4l2-m2m
make -j"${JOBS:-4}"
mkdir -p "$root/build/ffmpeg-test"
install -m 755 ffmpeg ffprobe "$root/build/ffmpeg-test/"
printf 'Independent armhf test binaries: %s\n' "$root/build/ffmpeg-test"
