# OneCloud Meson8b V4L2 codecs

[English](README.md) · [简体中文](README.zh-CN.md)

Hardware video decoding and H.264/JPEG encoding for the Amlogic S805
(Meson8b) in the OneCloud. Three external modules expose standard V4L2
Memory-to-Memory devices on WanOS/HiNAS **Linux 5.15.79-meson**, using a
removable runtime device-tree overlay. No replacement boot kernel is needed.

## What works

| Path | Scope |
| --- | --- |
| Decode | H.264 and HEVC Main 8-bit have picture-content checks; MPEG-1/2, MPEG-4 Part 2, VC-1 advanced and MJPEG have known tail/content limits. |
| H.264 encode | Baseline/Level 4.0, QP or VBR target control, YUV/RGB inputs, DMA-BUF input, multi-slice, SPS crop/VUI. |
| JPEG encode | Quality 1–100, full-range YCbCr input, direct V4L2 client. |
| Transcode | One decoder plus one encoder; validated 1080p samples fit the original 64 MiB CMA. |

The target is **official unmodified FFmpeg**. The OpenList fork discovers V4L2
formats and serializes hardware work; Docker still needs device mappings.
No HEVC encoding, Main10, H.264 Main/High/B-frame encoding, end-to-end zero-copy
or realtime throughput guarantee is provided. See
[capabilities, evidence and remaining work](docs/validation.md).

Latest stable release: **v0.4.0**. The current production device uses a newer
development codec build (`3c7d785`), retains the existing runtime and keeps
the previous installation plus v0.4.0 recovery materials. Bounded checks
pass; two network outages were investigated and sustained load tests remain
paused. Do not treat the development deployment or CI as a stability release.

## Quick start

1. Obtain a [release](https://github.com/YoungSx/onecloud-meson8b-vcodec/releases)
   or [cross-build](docs/build.md) on an external machine.
2. Follow [installation and rollback](docs/installation.md). The package must
   match the kernel ABI, board and live device-tree phandles. The guide covers
   download/transfer, preflight, content checks, removal and version rollback.
3. With codec clients stopped and a fresh backup path:

   ```sh
   sudo python3 runtime-package/scripts/runtime-device.py check runtime-package
   sudo bash runtime-package/scripts/install-autoload.sh runtime-package /root/new-codec-state
   ```

Use an FFmpeg build containing the corresponding wrappers:

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -i input.mp4 -an \
  -c:v h264_v4l2m2m -pix_fmt nv12 -b:v 2M -g 30 output.mp4
```

Default CAPTURE counts work without board-specific buffer tuning. The bitrate
is a VBR target; low targets may be unreachable. Standard FFmpeg does not forward
peak/CPB controls. [Usage](docs/usage.md) covers discovery, JPEG and other inputs;
[FFmpeg compatibility](docs/ffmpeg-compatibility.md) explains standard controls.

## Develop and operate

[Documentation](docs/README.md) · [Architecture](docs/architecture.md) ·
[Build](docs/build.md) · [Troubleshooting](docs/troubleshooting.md) ·
[Roadmap](ROADMAP.md) · [CI/releases](docs/release.md) ·
[Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

A fresh clone contains the source, firmware, samples and public evidence.
[Workspace migration](docs/workspace-migration.md) identifies private backups
and deployed artifacts that Git cannot restore. The Linux 7.3.4 research series
is build-checked only and has not booted on a board.

Code licenses follow individual SPDX headers (GPL-2.0/GPL-2.0+).
[Firmware provenance and licenses](docs/firmware.md).
