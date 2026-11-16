# Changelog

Versions follow SemVer and Conventional Commits. Until 1.0, releases are
pre-releases tied to the recorded OneCloud kernel and device-tree ABI.

## Unreleased

Tooling only; the installed payload is unchanged from v0.2.0.

- Add `scripts/soak.sh`, the bounded decode/encode soak used for the sustained
  load runs, and document it in the validation guide.
- Add `scripts/service-coexist.sh`, which runs that soak while probing the
  box's production services, and `scripts/h264-tail-race.sh`, which repeats
  the 1080p decode that came up one frame short in the overnight run. Record
  both results: the overnight window ends on that single failure, and 200
  targeted repeats did not reproduce it.
- Authenticate the pinned vendor source fetch with `GITHUB_TOKEN` and back off
  across the 429s shared CI runners get, instead of failing the host job.
- Rebase the research series onto mainline v7.3.4: it applies with no
  rejects, builds `meson-vdec.ko`, every ARM DTB and passes the
  `amlogic,gx-vdec.yaml` schema check. The DT patch targets `meson8b.dtsi`
  and `meson8b-odroidc1.dts` since mainline has no OneCloud board file.
  At that point Meson8b still rejected STREAMON; no hardware run.
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
  domain. Compile-level only: the nine patches then in the series applied in
  order, the module and the pwrc driver built with no warning, and all six
  Meson8/8b/8m2 DTBs validated.
- Make the DOS aperture shareable in the research series (`0009`–`0012`): a
  `amlogic,meson8b-dos` syscon owns the window, the decoder takes it through
  `amlogic,dos` and reaches it through `regmap_mmio` instead of its own
  `ioremap`, and the binding requires both regions exactly when the phandle is
  absent. Compile-level only: thirteen patches apply in order, the module
  builds with no warning, the ODROID-C1 DTB carries the split nodes, and the
  GXBB/GXL/G12A/SM1 DTBs still validate against the changed binding.
- Convert the Meson8b capture output in software (`0013`), which is what the
  STREAMON rejection was waiting for: each capture buffer gets a private
  canvas block in the firmware's byte order, converted into the dequeued
  buffer through a cached alias. Meson8b drops to twelve capture buffers and
  a 4 MiB bitstream FIFO so the blocks fit its 64 MiB CMA. Compile-level
  only: fourteen patches apply in order, the module builds with no warning
  and reproduces from a pristine tree, and a host test pins the plane split.
  No hardware run, so the cost and the buffer budget are unproven.
- Swap the capture words with NEON (`0014`) where the caller allows it: the
  intrinsic body moves to its own unit built with `CC_FLAGS_FPU`, because arm32
  rejects `kernel_neon_begin()` and NEON code in one file, and the caller keeps
  the scalar loop for contexts where `may_use_simd()` is false. Compile-level
  only: fifteen patches apply in order and pass `checkpatch.pl --strict`, the
  module builds with no warning, the NEON instructions land only in the unit
  that was compiled for them, and a host check pins the body against
  `__builtin_bswap64`. The saving itself is unmeasured in the driver.
- Stop treating two replayed VOPs as the MPEG-4 drain constant. Replaying the
  final coded VOP k times before the visual object sequence end yields
  `88 + k` pictures: two is right for the reference stream in all three
  rounds, three for a 50-VOP I/P stream encoded on the box itself (whose k=2
  wobbles 49/49/50 across rounds), and one extra replay in either case shows
  up as a repeat of the last real picture, not a new one. The required count
  changes with the stream and is not readable from the driver, so the drain
  keeps sending only the sequence end code and the limitation now reads
  "1–3 tail pictures, stream dependent" instead of "the last two".
- Record that MPEG-4 B pictures depend on content: on a static-gradient
  stream every B capture comes back with the bottom 48 rows unwritten, at
  both quality settings, while the all-I stream of the same content and the
  B frames of a fully moving stream are exact. Earlier MPEG-4 acceptance used
  all-I streams only, so this had not been covered. No driver change: the
  decode is firmware-side and the root cause is not established.

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
