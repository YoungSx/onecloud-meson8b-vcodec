# Changelog

Versions follow SemVer and Conventional Commits. Until 1.0, releases are
pre-releases tied to the recorded OneCloud kernel and device-tree ABI.

## Unreleased

- Fix H.264 rate-controlled streams decoding at the wrong QP (constant
  pic_init_qp), a self-deadlock when MIN/MAX_QP change, a 5 s stall on
  pictures larger than the 1 MiB bitstream buffer, and widths the microcode
  cannot code or crop (narrow, 65-macroblock and non-multiple-of-16 widths).
- Add I/P frame QP, frame rate-control enable and GOP 0 controls, and a
  bounded one-second leaky-bucket rate control capped at QP 32.
- Add YUV444M, YUYV, UYVY, RGB24 and 32-bit RGB inputs with BT.601/BT.709
  conversion, direct MFDIN reads of contiguous DMA-BUF NV12/NV21 frames, and
  whole-row multi-slice encoding.
- Add the encode-test controls, synthetic sources, DMA-BUF input and per-frame
  latency, and the encode-baseline report with PSNR and bitrate windows.
- Plan staged encoder controls, raw formats, DMA input, multi-slice and
  measured pipeline improvements; defer HEVC encoding research.
- Document the S805 HEVC encoding hardware boundary and evidence-backed
  H.264/JPEG extension paths; preserve both README languages and source provenance.

## v0.1.0 — 2026-11-14 (pre-release)

- Reorganize current installation, usage, architecture, build and validation
  guides; provide equivalent English and Simplified Chinese READMEs.
- Add host tests, firmware reproduction, ARM cross-build and package validation
  to CI; version tags prepare checked release drafts and source companions.
- Preserve the 2026-11-14 hardware acceptance and installed `bd1cb79` provenance.

## Initial hardware implementation — 2026-11-14

- Stateful H.264, HEVC Main 8-bit and legacy MPEG/VC-1/MJPEG decoding.
- HCODEC H.264/JPEG encoding, quality/keyframe controls and software CBR.
- 1080p H.264 B-frame input concurrent transcoding under 64 MiB CMA.
- Reversible runtime overlay installation and systemd autoload.
- Independent FFmpeg drain fixes and static v4l2-ctl; original boot files preserved.

See [validation](docs/validation.md) for codec limits and exact evidence.
