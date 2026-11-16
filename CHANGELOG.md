# Changelog

Versions follow SemVer and Conventional Commits. Until 1.0, releases are
pre-releases tied to the recorded OneCloud kernel and device-tree ABI.

## Unreleased

Tooling only; the installed payload is unchanged from v0.2.0.

- Add `scripts/soak.sh`, the bounded decode/encode soak used for the sustained
  load runs, and document it in the validation guide.
- Authenticate the pinned vendor source fetch with `GITHUB_TOKEN` and back off
  across the 429s shared CI runners get, instead of failing the host job.
- Rebase the research series onto mainline v7.3.4: it applies with no
  rejects, builds `meson-vdec.ko`, every ARM DTB and passes the
  `amlogic,gx-vdec.yaml` schema check. The DT patch targets `meson8b.dtsi`
  and `meson8b-odroidc1.dts` since mainline has no OneCloud board file.
  Meson8b streaming still returns `-EOPNOTSUPP`; no hardware run.
- Add `patches/research/0000-esparser-eos-padding.patch`, the upstream fix for
  the end-of-stream buffer that is missing the 4 KiB packet and the
  `000001ff` terminator; it applies to pristine v7.3.4 alone and affects every
  platform with an `eos_sequence`.
- Add `scripts/check-mainline-port.sh`, which applies the research series to a
  temporary copy of a pristine tree and runs `checkpatch.pl --strict` on each
  patch, so the port claim is reproducible instead of transcribed.
- Complete the mainline build: `vmlinux` plus all 938 modules link with no
  modpost warnings and no unresolved symbols, so the port record no longer
  carries a partial-link caveat.
- Record the vendor power/clock ordering in the shared-DOS design note: both
  `vdec_poweron()` and `avc_poweron()` clear the AO sleep bits before enabling
  the engine clock, which is what a genpd conversion would do.
- Implement the Meson8b VDEC power domain in the research series
  (`0004`–`0008`): the AO sleep and isolation bits move to genpd, the decoder
  takes a runtime reference instead of writing them and falls back to the
  previous direct writes where no domain exists, `amlogic,ao-sysctrl` becomes
  conditional on the absence of `power-domains`, and the Meson8b node uses the
  domain. Compile-level only: nine patches apply in order, the module and the
  pwrc driver build with no warning, and all six Meson8/8b/8m2 DTBs validate.
- Make the DOS aperture shareable in the research series (`0009`–`0012`): a
  `amlogic,meson8b-dos` syscon owns the window, the decoder takes it through
  `amlogic,dos` and reaches it through `regmap_mmio` instead of its own
  `ioremap`, and the binding requires both regions exactly when the phandle is
  absent. Compile-level only: thirteen patches apply in order, the module
  builds with no warning, and the ODROID-C1 DTB carries the split nodes.

## v0.2.0 — 2026-11-15 (pre-release)

Encoder controls, input formats, DMA-BUF input, multi-slice, the NEON output
conversion and the MPEG/VC-1 drain fix. Deployed to the production OneCloud
on 2026-11-15: MPEG-2 and VC-1 tail rechecks, H.264/HEVC decode and
H.264/JPEG encode all passed, and soaks of 30 minutes (545 iterations) and
2 hours (2205 iterations) ran with no failures and no frame-hash mismatch.
Hardware coverage per item is in [validation](docs/validation.md); the
overnight soak is still outstanding and this draft makes no indefinite-soak
claim.

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
