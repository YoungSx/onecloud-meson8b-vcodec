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
variant=upstream
case ${1:-} in
    '') ;;
    --bugfixes) variant=bugfixes ;;
    *) echo 'Usage: build-ffmpeg-test.sh [--bugfixes]' >&2; exit 2 ;;
esac
# Re-extract pinned sources so stale local patches cannot enter an upstream build.
source_dir="$cache/ffmpeg-7.0.2-$variant"
rm -rf "$source_dir"
mkdir "$source_dir"
tar -xJf "$archive" --strip-components=1 -C "$source_dir"
cd "$source_dir"
if [[ "$variant" == bugfixes ]]; then
    patch -p1 --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-drain.patch"
    patch -p1 --fuzz=0 < "$root/patches/ffmpeg-7.0.2-v4l2-bugs.patch"
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
install -m 644 COPYING.LGPLv2.1 "$root/build/ffmpeg-test/"
printf '%s\n' "$variant" > "$root/build/ffmpeg-test/BUILD_VARIANT"
printf 'Independent armhf test binaries: %s\n' "$root/build/ffmpeg-test"
