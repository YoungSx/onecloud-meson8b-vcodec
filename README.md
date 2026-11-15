# OneCloud Meson8b V4L2 codecs

[English](README.md) · [简体中文](README.zh-CN.md)

Hardware video decoding and H.264/JPEG encoding for the OneCloud S805
(Meson8b), running WanOS/HiNAS on Linux **5.15.79-meson**. Three loadable
modules expose standard V4L2 Memory-to-Memory devices using a removable
runtime device-tree overlay. Build externally for ARMv7 hard-float.

The [2026-11-14 hardware acceptance](docs/validation.md) covers H.264 and
HEVC picture comparisons, ten 1080p B-frame transcode sessions, 900 JPEG
images, firmware error recovery and reversible installation. All five
existing services were running at the final check. The original boot
kernel, device tree and production FFmpeg were preserved.

## Supported paths

| Path | Supported and tested scope |
| --- | --- |
| Decode | H.264, HEVC Main 8-bit, MPEG-1/2, MPEG-4 Part 2, VC-1 advanced, MJPEG |
| H.264 encode | 4:2:0, 4:2:2, 4:4:4 and RGB input; fixed I/P QP or rate control; multi-slice; DMA-BUF input; no B-frame encoding |
| JPEG encode | Quality 1–100; full-range YCbCr input; standard V4L2 client |
| Concurrent transcode | One decoder plus one encoder; 1080p H.264 B-frame input within the existing 64 MiB CMA budget |

HEVC Main10, tiles and long-term references are unsupported. Legacy MPEG,
VC-1 and MJPEG have documented drain/chroma limits. Output still uses CPU
byte-order conversion and copies. Autoload start/stop was tested without
rebooting; there is no current-mainline port or published linux-media RFC.
See [capabilities and limits](docs/validation.md).
The encoder extensions above were validated on 2026-11-15 by swapping in
candidate modules; the installed `bd1cb79` package predates them.
S805's documented encoder formats are H.264 and JPEG; HEVC is decode-only.
[Encoding feasibility](docs/encoding-feasibility.md) records the hardware
evidence and further H.264/JPEG development paths.

## Start here

1. Read [installation and rollback](docs/installation.md). The package must
   match the kernel ABI, board and live device-tree provider phandles.
2. Obtain build artifacts from a [release](https://github.com/YoungSx/onecloud-meson8b-vcodec/releases)
   or [cross-build from source](docs/build.md). CI artifacts are build checks;
   hardware acceptance belongs to the recorded source version.
3. Verify the archive checksums, then install with a new backup directory:

   ```sh
   sudo python3 runtime-package/scripts/runtime-device.py check runtime-package
   sudo bash runtime-package/scripts/install-autoload.sh runtime-package /root/new-codec-state
   ```

After codec clients exit, remove autoload, unload the modules and restore
pre-install firmware with:

```sh
sudo bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh
```

## Use

The original production FFmpeg supports H.264 M2M. For the verified 1080p
B-frame input with four references, use ten capture buffers:

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -num_capture_buffers 10 -i input.mp4 \
  -an -c:v h264_v4l2m2m -b:v 0 -fps_mode passthrough output.mp4
```

The independent package `tools/ffmpeg` adds HEVC M2M and V4L2 drain fixes.
It is a narrow file-only test tool; use production FFmpeg for H.264 encoding.
JPEG requires a V4L2 client because FFmpeg 7.0.2 has no JPEG M2M encoder wrapper.
[Commands and buffer sizing](docs/usage.md) explain these differences.

## Documentation and development

[Documentation index](docs/README.md) · [Architecture](docs/architecture.md) ·
[Build](docs/build.md) · [Validation](docs/validation.md) ·
[CI and releases](docs/release.md) · [Contributing](CONTRIBUTING.md) ·
[Roadmap](ROADMAP.md) · [Changelog](CHANGELOG.md)

CI runs host tests and sanitizers, reproduces pinned firmware, cross-builds
against the matching kernel, checks the additive patch and verifies packages.
Version tags run the same checks and create a GitHub Release **draft** with
runtime/source archives, checksums and build provenance. See the release guide
before publishing a draft or installing a new build.

Code is GPL-2.0/GPL-2.0+ according to individual SPDX headers; vendor
microcode is extracted from pinned GPL sources. [Firmware provenance and
licenses](docs/firmware.md) include the independent tools' licenses.
