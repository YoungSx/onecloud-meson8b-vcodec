# OneCloud Meson8b V4L2 codecs

[English](README.md) · [简体中文](README.zh-CN.md)

Hardware video decoding and H.264/JPEG encoding for the OneCloud S805
(Meson8b), running WanOS/HiNAS on Linux **5.15.79-meson**. Three loadable
modules expose standard V4L2 Memory-to-Memory devices using a removable
runtime device-tree overlay. Build externally for ARMv7 hard-float.

Production was restored to the installed v0.4.0 stack after two outages during
the latest development pass. The new candidate has not passed stability
acceptance; sustained production tests are paused. See
[current results and recovery](docs/next-development-status.md).

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
byte-order conversion and copies. Autoload was accepted on a cold boot on
2026-11-16: the overlay applies 29 s into kernel boot and both V4L2 nodes
appear, with the original kernel untouched; `systemctl restart` also brings
the stack back in 5 s without a reboot. The
[mainline port](evidence/mainline-2026-11-16/PORT.md) is
compile-level only — the research series applies to v7.3.4 and builds the
module and every ARM DTB, but has not run on S805 hardware — and the
[linux-media findings](docs/linux-media-rfc.md) are recorded in
[Discussions](https://github.com/YoungSx/onecloud-meson8b-vcodec/discussions/2) and are not submitted upstream.
See [capabilities and limits](docs/validation.md).
The encoder extensions and the MPEG/VC-1 drain fix were validated on
2026-11-15 and remain included in the installed v0.4.0 release package.
The v0.4.0 frame pool passed its default-buffer regression and a 30-minute
load with all five services present; see the [deployment record](evidence/p6-2026-11-20/v040-deploy.md).
S805's documented encoder formats are H.264 and JPEG; HEVC is decode-only.
[Encoding feasibility](docs/encoding-feasibility.md) records the hardware
evidence and further H.264/JPEG development paths.
The [portability note](docs/portability.md) separates the verified runtime
stack from the older research port and the OpenList integration.

The [rate-control update](docs/rate-control.md) fixes all-IDR feedback,
picture-boundary controls and bounded overflow recovery, with S805 regression
evidence. Opt-in peak/CPB controls now enforce a media-time VBR envelope;
oversized access units fail without being published. Official FFmpeg remains the compatibility target; standard `-b:v`, QP and GOP
options work without a fork. Peak/CPB controls are available to direct V4L2
clients; official wrappers do not forward those two controls.
Complex scenes can still make a target unreachable. This is not strict CBR,
HRD or a network bandwidth cap; the production installation remains separate
from the candidate's hardware acceptance.

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

The original production FFmpeg supports H.264 M2M. The decoder holds its own
frame pool, so the number of CAPTURE buffers the application asks for no
longer changes hardware memory; the default works:

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -i input.mp4 \
  -an -c:v h264_v4l2m2m -b:v 0 -fps_mode passthrough output.mp4
```

The independent package `tools/ffmpeg` builds unmodified upstream FFmpeg with
H.264/HEVC M2M, AAC and HLS/MPEG-TS test support. It supports
files and HTTP; it lacks HTTPS/TLS and libx264/libx265. Keep production FFmpeg
for its broader formats and software encoders. See [rate limits](docs/rate-control.md)
for the boundary between target tracking and direct V4L2 controls.
JPEG requires a V4L2 client because FFmpeg 7.0.2 has no JPEG M2M encoder wrapper.
[Commands and buffer sizing](docs/usage.md) explain these differences.

## Documentation and development

[Documentation index](docs/README.md) · [Architecture](docs/architecture.md) ·
[Build](docs/build.md) · [Validation](docs/validation.md) ·
[CI and releases](docs/release.md) · [Contributing](CONTRIBUTING.md) ·
[Roadmap](ROADMAP.md) · [Current development results](docs/next-development-status.md) ·
[Changelog](CHANGELOG.md)

CI runs host tests and sanitizers, reproduces pinned firmware, cross-builds
against the matching kernel, checks the additive patch and verifies packages.
Version tags run the same checks and create a GitHub Release **draft** with
runtime/source archives, checksums and build provenance; the publisher then
reviews it, verifies the checksums and publishes it as a formal release. See
the release guide before installing a new build.

Code is GPL-2.0/GPL-2.0+ according to individual SPDX headers; vendor
microcode is extracted from pinned GPL sources. [Firmware provenance and
licenses](docs/firmware.md) include the independent tools' licenses.
