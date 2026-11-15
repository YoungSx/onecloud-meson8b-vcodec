# Changelog

Versions follow SemVer and Conventional Commits. Until 1.0, releases are
pre-releases tied to the recorded OneCloud kernel and device-tree ABI.

## Unreleased

No changes yet.

## v0.2.0 — 2026-11-15 (pre-release)

Encoder controls, input formats, DMA-BUF input, multi-slice, the NEON output
conversion and the MPEG/VC-1 drain fix. Deployed to the production OneCloud
on 2026-11-15: MPEG-2 and VC-1 tail rechecks, H.264/HEVC decode and
H.264/JPEG encode all passed. Hardware coverage per item is in [validation](docs/validation.md);
the 2-hour and overnight soaks are still outstanding and this draft makes no
indefinite-soak claim.

- Fix the MPEG-1/2, MPEG-4 and VC-1 drain writing no end code at all: the
  EOS buffer was allocated for `len + 512` while the padding helper fills
  `max(len, 4096) + 512`, so every sequence end was rejected and the final
  picture of those streams never completed.
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
- Convert decoder output with kernel-mode NEON (1080p copy 15 ms to 7–8 ms),
  with a scalar fallback outside SIMD contexts and bounded per-row window
  copies for HEVC conformance cropping.
- Size capture planes by the whole 32-byte aligned canvas, fixing out-of-range
  writes for 1360x768 and 1600x900, and reset HEVC crop state when a picture's
  SPS does not match.
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
