# Meson8b codec RFC submission draft

This local draft documents the reviewable patch and its limits; it has not
been emailed or submitted to linux-media. The driver targets the original
OneCloud 5.15.79 kernel and uses a temporary runtime overlay. A mainline
submission needs a static device-tree topology with proper shared
power-domain ownership before it can be accepted.

Compile-side port, 2026-11-16: `patches/research/` is now based on pristine
v7.3.4 and applies with `patch -p1 -F0`. It builds `meson-vdec.ko`
(vermagic 7.3.4), all arm DTBs, and passes `dtbs_check` against
`amlogic,gx-vdec.yaml`. The DT patch targets `meson8b.dtsi` plus
`meson8b-odroidc1.dts` because mainline has no OneCloud board file. See
[port record](../evidence/mainline-2026-11-16/PORT.md). Nothing here ran on
S805 hardware, and Meson8b streaming still returns `-EOPNOTSUPP`.

Suggested series split:

1. media: meson: preserve shared DOS clock/control fields on engine startup
2. media: meson: vdec: validate parser padding before writes and fetch DMA
3. media: meson: vdec: reuse compatible capture queues after source change
4. media: meson: vdec: add Meson8b platform and NV12 shadow conversion
5. media: meson: vdec: add Meson8b vendor HEVC Main 8-bit protocol
6. media: meson: add Meson8b HCODEC AVC/JPEG V4L2 encoder
7. dt-bindings/media: describe shared Meson8b DOS engines and firmware

The additive compatibility patch is patches/0001-meson8b-runtime-codecs.patch.
The runtime overlay/helper is for the production board's unchanged kernel,
not an upstream DT binding proposal. Its hardcoded board identity and
phandles must be replaced for a static upstream deployment. No RFC
publication or upstream acceptance is claimed.

Review points to carry forward:

- `esparser_queue_eos()` is still broken in v7.3.4 and worth a standalone
  fix: it allocates `len + SEARCH_PATTERN_LEN`, copies the payload and writes
  `len` bytes with no `00 00 01 ff` terminator. `esparser_pad_start_code()`
  pads only regular input buffers, so a 4-byte sequence-end code is written
  without the padding that raises the parser search interrupt. Callers ignore
  the return value, so MPEG-1/2, MPEG-4 and VC-1 drains silently deliver no
  end code. Our fix allocates `ESPARSER_PAD_SPAN(len)`, pads, and writes
  `len + pad_size`; MPEG-2 recovers its last two pictures (88 → 90 frames).
- S805 lacks canvas endianness controls, so reference buffers stay separate
  from CPU-visible output; cached alias plus explicit synchronization makes
  the byte-order conversion practical. There remains a copy, not zero-copy.
- DOS control bits and PMU fields are shared by VDEC1 and HCODEC. Masked
  writes require one owner/lock; no whole-register overwrite across engines.
  [Shared ownership design](power-domain-design.md) proposes two Meson8b genpd
  domains plus a DOS syscon; it is not implemented.
- Vendor HEVC RPM lacks the conformance window. The bounded SPS prefix
  parser supplies geometry; HEVC Main10 and tiled streams are rejected.
- On this 64 MiB CMA device the encoder raw input must match negotiated
  plane geometry instead of allocating the vendor private MB input area.
- H.264 M8 EOS batches can contain reordered pictures; use queued PTS for
  presentation order and retain packet offsets for timestamp lookup.
- Acceptance evidence must include complete frame counts, picture hashes,
  malformed input recovery and module/overlay rollback with all services.
