# Meson8b codec: upstream findings

Upstream submission is out of scope for this project, by decision. The driver
targets the original OneCloud 5.15.79 kernel through a temporary runtime
overlay, and a mainline submission would need a static device-tree topology
with proper shared power-domain ownership that cannot be validated on this
board. The findings below are kept because they are reproducible and useful
to anyone who does pursue it; they are recorded in the repository's
[Discussions](https://github.com/YoungSx/onecloud-meson8b-vcodec/discussions/2) and are not emailed or submitted to
linux-media.

Compile-side port, 2026-11-16: `patches/research/` is now based on pristine
v7.3.4 and applies with `patch -p1 -F0`. It builds `meson-vdec.ko`
(vermagic 7.3.4), all arm DTBs, and passes `dtbs_check` against
`amlogic,gx-vdec.yaml`. The DT patch targets `meson8b.dtsi` plus
`meson8b-odroidc1.dts` because mainline has no OneCloud board file. See
[port record](../evidence/mainline-2026-11-16/PORT.md). Nothing here ran on
S805 hardware. The series no longer refuses to stream — `0013` implements
the capture conversion — but the path is unproven on the silicon.
Patches0016–0026 subsequently ported the independent frame pool, existing
decode formats and IRQ/EOS/DMA/clock protections. See [portability](portability.md)
for the current research scope; runtime acceptance is still missing.

Patches 0004–0008 add the Meson8b VDEC power domain: the AO sleep and
isolation bits move to genpd, the decoder takes a runtime reference instead of
writing them, `amlogic,ao-sysctrl` becomes conditional on the absence of a
domain, and the DT node carries the phandle. Patches 0009–0012 finish the
other half: `amlogic,meson8b-dos` is a syscon, the decoder reaches the
aperture through `amlogic,dos` and `regmap_mmio`, and the node keeps its own
`ioremap` only when no syscon is described. Together these are the answer to
review point "shared DOS/power-domain ownership"; neither half has run on
hardware, and nothing yet consumes the shared aperture because no encoder
driver exists upstream.

Suggested series split:

1. media: meson: vdec: pad the ESPARSER end-of-stream buffer
   (`patches/research/0000-esparser-eos-padding.patch`, independent of the rest)
2. media: meson: preserve shared DOS clock/control fields on engine startup
3. media: meson: vdec: validate parser padding before writes and fetch DMA
4. media: meson: vdec: reuse compatible capture queues after source change
5. media: meson: vdec: add Meson8b platform and NV12 shadow conversion
6. media: meson: vdec: add Meson8b vendor HEVC Main 8-bit protocol
7. media: meson: add Meson8b HCODEC AVC/JPEG V4L2 encoder
8. dt-bindings/media: describe shared Meson8b DOS engines and firmware

The additive compatibility patch is patches/0001-meson8b-runtime-codecs.patch.
The runtime overlay/helper is for the production board's unchanged kernel,
not an upstream DT binding proposal. Its hardcoded board identity and
phandles must be replaced for a static upstream deployment. No publication
or upstream acceptance is claimed.

Review points to carry forward:

- `esparser_queue_eos()` is still broken in v7.3.4: it allocates
  `len + SEARCH_PATTERN_LEN`, copies the payload and writes `len` bytes with
  no `00 00 01 ff` terminator and no 4 KiB minimum packet.
  `esparser_pad_start_code()` pads only regular input buffers, so the 4–12 byte
  MPEG-1/2, MPEG-4 and VC-1 sequence-end codes never reliably reach the
  firmware and the last pictures stay in the decoder. `vdec_stop_streaming()`
  ignores the return value, so nothing is logged. The standalone fix is
  `patches/research/0000-esparser-eos-padding.patch`: it factors the padding
  into `esparser_pad_payload()` and has the EOS path allocate
  `max(len, 4 KiB) + SEARCH_PATTERN_LEN`, pad, and write `len + pad_size`.
  It applies to pristine v7.3.4 by itself and affects every platform with an
  `eos_sequence`, not just Meson8b; MPEG-2 recovers its last two pictures
  (88 → 90 frames). The 150-frame B-frame stream is not deterministic even
  with the fix: repeated runs on the Meson8b branch delivered 149 or 150
  pictures, and a short run loses the last picture in decode order, so the
  padding fix is necessary but the final picture's release is still
  unreliable. That branch also logs the EOS write timeout in
  `esparser_queue_eos()`; the standalone patch does not.
- S805 lacks canvas endianness controls, so reference buffers stay separate
  from CPU-visible output; a cached alias plus explicit synchronization makes
  the byte-order conversion practical. `0013` does exactly that: one private
  canvas block per capture buffer, converted into the dequeued buffer. It
  costs a copy per picture and 1.5x the picture size in CMA per buffer, which
  is why Meson8b also drops to twelve capture buffers and a 4 MiB FIFO.
  `0014` moves the swap itself to NEON in its own FPU-compiled unit, which
  arm32 requires, and keeps the scalar loop for contexts where
  `may_use_simd()` is false; the saving is unmeasured in the driver.
- DOS control bits and PMU fields are shared by VDEC1 and HCODEC. Masked
  writes require one owner/lock; no whole-register overwrite across engines.
  [Shared ownership design](power-domain-design.md) proposes two Meson8b genpd
  domains plus a DOS syscon. Both halves are implemented (`0004..0008` and
  `0009..0012`) and build; neither has run on hardware.
- Even with the EOS padding fixed, MPEG-4 needs more than the sequence end
  code: the firmware holds several pictures and completes them one per coded
  VOP replayed, and the count varies by stream (2 for the 90-VOP reference,
  3 for a 50-VOP stream encoded on the box). Replaying one more than the
  firmware held emits a duplicate of a picture already delivered, so a fixed
  number cannot be right for every stream, and no register exposes the depth
  (`MREG_BUFFEROUT` is per-picture completion, `MP4_NOT_CODED_CNT` counts
  not-coded repeats). A fix needs a firmware-visible depth or an EOS flush.
  Details in [the MPEG-4 record](../evidence/p6-2026-11-16/m4v-tail.md).
- No frame counter pairs firmware completion with delivery to userspace, so a
  short output cannot be attributed to the driver or the client. One H.264
  1080p decode in 7620 overnight iterations returned 299 of 300 frames, and
  targeted repeats did not reproduce it; a debugfs counter is the
  prerequisite for diagnosing that class of report, before any drain change.
- Vendor HEVC RPM lacks the conformance window. The bounded SPS prefix
  parser supplies geometry; HEVC Main10 and tiled streams are rejected.
- On this 64 MiB CMA device the encoder raw input must match negotiated
  plane geometry instead of allocating the vendor private MB input area.
- H.264 M8 EOS batches can contain reordered pictures; use queued PTS for
  presentation order and retain packet offsets for timestamp lookup.
- Acceptance evidence must include complete frame counts, picture hashes,
  malformed input recovery and module/overlay rollback with all services.
