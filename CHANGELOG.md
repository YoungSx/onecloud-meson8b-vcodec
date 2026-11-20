# Changelog

Versions follow SemVer and Conventional Commits. Releases are tied to the
recorded OneCloud kernel and device-tree ABI. A patch release may ship the
same modules as its predecessor when only documentation, evidence or tooling
changed.

## v0.4.0 — 2026-11-20

The decoder no longer sizes its hardware memory by the CAPTURE count. Each
CAPTURE buffer used to carry its own CMA shadow in firmware byte order, so
FFmpeg's default of 20 buffers (clamped to 12) needed 36 MiB of CMA at
1080p, and decode plus encode could not both start on the 64 MiB device.
The decoder now holds a frame pool sized by the stream, the decoded picture
is converted into whichever CAPTURE buffer the application queued, and
applications no longer pass `-num_capture_buffers`. `meson-vdec.ko` differs
from v0.3.0; the other two modules do not. `ffmpeg` and the device tree are
unchanged.

- Decode into a driver-owned frame pool: H.264 gets the reference count plus
  two spare slots (`h264_extra_buffers` 5 → 2), HEVC the DPB size, MPEG-1/2
  eight and MPEG-4/VC-1/MJPEG four. A 1080p session now holds 9 MiB (one
  reference) to 18 MiB (four references) instead of 36 MiB, and CAPTURE
  buffers are ordinary vmalloc memory for any count from 1 to 32. Finished
  pictures wait in firmware order and are copied into the next queued
  buffer; EOS and SOURCE_CHANGE are signalled only after they are delivered.
- Deliver HEVC pictures at the geometry they were decoded at, not the
  session's current size, so a resolution switch cannot re-copy an old tail
  picture at the new size; pending pictures are delivered before the session
  adopts the new size and the last one ends the old stream.
- Return CAPTURE buffers under the frame pool lock at STREAMOFF. The v0.3.0
  shadow path dropped the waiting pictures, released the lock and only then
  handed the buffers back, so a conversion could write into a buffer vb2 had
  already freed — the "stop_streaming operation is leaving buf in active
  state" warning before its source-change oops.
- Stop feeding the ESPARSER once the session has ended, so OUTPUT buffers
  queued after a STREAMOFF cannot be written into the freed VIFIFO.
- Keep HEVC sessions alive while the client stops feeding: the stall
  watchdog now needs the client to be blocked on the decoder with input
  queued that the VIFIFO cannot take, or a drain requested, for a whole
  period.
- Observe the pool in debugfs: `pool_slots`, `pool_bytes`,
  `pool_waiting_peak`, `converted_frames`, `cropped_frames`, `convert_ns`.
- Accept the v0.4.0 candidate on hardware: per-frame hashes against the
  software reference for H.264 (1080p, 1360x768, 1600x900, four-reference
  B frames), HEVC, MPEG-2/4, VC-1 and MJPEG at CAPTURE 1, 2, 4, 20, 32 and
  at FFmpeg's default, resolution switches both codecs at 1–32, 1080p decode
  together with 1080p encode under the same CMA, 300 mixed-load rounds, and
  a 30-minute soak with the five production services probed throughout
  (439 iterations, 107 samples, 0 failures). See
  [Phase A](evidence/phase-a-2026-11-19/README.md).
- Evaluate a 2 MiB VIFIFO (4 → 2 MiB): it passed the resolution-switch
  matrix but is not adopted. VIFIFO does not split packets, the largest
  packet in the test streams is 59468 B, and 2 MiB per session does not
  justify the reduced headroom.
- Leave the transcode performance work and the OpenList end-to-end
  integration to a later release: one 60 s 1080p chunk is still slower than
  real time on this board, and OpenList's hardware transcoding waits for the
  v4l2m2m PR to be merged upstream.
- Record the H.264 1080p short output seen once in 453 runs at the reduced
  pool: it matches the existing one-in-1700 behaviour of v0.3.0, 300
  targeted rounds and the 30-minute soak did not reproduce it, and a failing
  run now keeps ffmpeg's stderr and the pool state.

## v0.3.0 — 2026-11-17

First payload change since v0.2.1: `esparser_queue_eos()` now reports a drain
terminator that times out instead of returning 0. The driver does not retry
and still does not change the drain; `meson-vdec.ko` differs from v0.2.1,
the other two modules do not. The package built from `0a0542b` was deployed
to the production box on 2026-11-17 and passed the regression set and a
30-minute soak (525 iterations, 0 failures); see
[v0.3.0 deploy](evidence/p6-2026-11-17/v030-deploy.md). Commits after
`0a0542b` in this release change only documentation, evidence and scripts.

- Make the EOS drain timeout observable: `esparser_write_data()` returns the
  remaining wait count, so a 200 ms search-interrupt timeout used to come back
  as 0, indistinguishable from success, with all four callers ignoring it.
  `esparser_queue_eos()` now logs `drain terminator not delivered` and returns
  `-ETIMEDOUT` in that case. The driver still does not retry.
- Report where a hash mismatch diverges: `scripts/verify-hashes.py` says
  whether the hardware output is a prefix of the reference (a lost tail, the
  EOS/DPB family) or names the first differing frame (interior loss), so the
  next short output is classified instead of only counted. Host tests cover
  all four divergence shapes.
- Keep the failure artifacts: `soak.sh`, `h264-shortload.sh` and
  `h264-tail-race.sh` preserve the failing framemd5 and the persistent
  `/var/log/kern.log` window, because the one real failure left no dmesg
  trace and the framemd5 was overwritten.
- Add `scripts/m2v-bf-delivery.sh`, which counts MPEG-2 B-frame delivery per
  run, and `scripts/m2v-frame-order.py`, which places each captured picture
  by its luma. The 8x8 grid signature of `m4v-frame-compare.py` cannot tell
  two adjacent pictures of that stream apart and reported the wrong one.
- Correct the MPEG-2 B-frame claim: the 150-picture stream delivers 149 or
  150 frames, not always 150; 14 of 20 runs were full. A short run loses the
  last picture in decode order, the B picture shown at position 148, and
  every other picture stays in place. The new EOS log stayed silent, so the
  end code was delivered. Appending a picture header recovers the picture in
  16 of 16 runs but adds a duplicate of the last one in 8, so the drain is
  left unchanged.
- Check the shipped NEON body rather than a copy of it: `tests/test_swap_neon.c`
  includes `vdec/meson8b-swap-neon.c` and compares it with
  `__builtin_bswap64`, so `make test` fails if the two ever disagree.
- Locate the MPEG-4 B-picture loss by content instead of by geometry: flipping
  the same testsrc with `vflip` moves the moving strip to the top, the bottom
  recovers, and 21 of 22 pictures come back with no zero region, so the defect
  is neither "the bottom of the frame" nor "B pictures" but the combination of
  a B picture with content in the lower part of the frame. Flipping does not
  merely move the defect either: the unflipped stream loses all 14 B pictures
  to a stable 48-row band, while the flipped one loses a single picture to 176
  zero rows.
- Repeat the short H.264 decode under the load that actually preceded the one
  real failure, instead of on an idle box: 400 rounds of HEVC decode, then the
  300-frame H.264 decode, then H.264 and JPEG hardware encodes, all through
  the same CMA, still returned 300 frames with matching hashes every time.
  At the observed rate that clean result was the likely one (about 0.79), so
  it narrows the condition rather than clearing it; 600 targeted repeats now
  exist and none reproduces it, and the cause stays unattributed.
- Accept the graceful-restart path on the production box: `systemctl restart`
  withdraws the overlay and unloads three modules, then loads them again in one
  transaction in 5 s, both nodes return, and a decode smoke test still produces
  22 frames. Only the cold boot after a reset had been accepted before.

## v0.2.1 — 2026-11-16

No driver change; the modules and device tree are identical to v0.2.0. This
release publishes the sustained-load, service-coexistence, reboot and MPEG-4
evidence, and drops the pre-release default from the release flow.

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
  that was compiled for them. The saving itself is unmeasured in the driver.
- Stop treating two replayed VOPs as the MPEG-4 drain constant. Replaying the
  final coded VOP k times before the visual object sequence end yields
  `88 + k` pictures: two is right for the reference stream in all three
  rounds, three for a 50-VOP I/P stream encoded on the box itself (whose k=2
  wobbles 49/49/50 across rounds), and one extra replay in either case shows
  up as a repeat of the last real picture, not a new one. The required count
  changes with the stream and is not readable from the driver, so the drain
  keeps sending only the sequence end code and the limitation now reads
  "1–3 tail pictures, stream dependent" instead of "the last two".
- Accept boot autoload on the production OneCloud: after an unplanned reset
  the overlay applied 29 s into kernel boot with no manual step, both V4L2
  nodes registered, the original 5.15.79-meson kernel untouched, and all four
  production service ports answered. Only a decode smoke test followed the
  reboot, and the graceful-restart path was not exercised on its own.
- Record that MPEG-4 B pictures depend on content: on a static-gradient
  stream every B capture comes back with the bottom 48 rows unwritten, at
  both quality settings, while the all-I stream of the same content and the
  B frames of a fully moving stream are exact. Earlier MPEG-4 acceptance used
  all-I streams only, so this had not been covered. No driver change: the
  decode is firmware-side and the root cause is not established.

## v0.2.0 — 2026-11-15

Encoder controls, input formats, DMA-BUF input, multi-slice, the NEON output
conversion and the MPEG/VC-1 drain fix. Deployed to the production OneCloud
on 2026-11-15: MPEG-2 and VC-1 tail rechecks, H.264/HEVC decode and
H.264/JPEG encode all passed, and soaks of 30 minutes (545 iterations) and
2 hours (2205 iterations) ran with no failures and no frame-hash mismatch.
Hardware coverage per item is in [validation](docs/validation.md); the
overnight soak was still outstanding when this tag was cut, so no
indefinite-soak claim is made. Published on 2026-11-16 once later evidence
arrived; the payload is identical to v0.2.1.

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

## v0.1.0 — 2026-11-14

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
