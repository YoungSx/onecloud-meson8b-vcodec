# P0 reliability fixes on the build host, 2026-11-23

The user chose P0 queue/error/resource work before codec extensions, and asked
to keep tests off the weak WanOS device whenever possible. **No WanOS command,
module load, reboot or workload was run in this pass.** This is a locally built
candidate, with no new hardware acceptance or release.

## Fixes and direct regressions

| Defect | Change | Before / after |
| --- | --- | --- |
| HEVC stop called `codec_hevc_flush_output()` after the pool worker was cancelled, enqueueing delivery while codec state was being destroyed. | Discard pending frame metadata on STREAMOFF; normal firmware EOS still flushes pictures. The caller frees the pool after codec stop. | [Before](shutdown-before.log) fails; [after](shutdown-after.log) passes the actual stop function with a delivery-work counter. |
| `venc_loadmc()` kicked IMEM DMA without a memory barrier and returned on timeout without cancelling/read-back. | Publish firmware with `dma_wmb()`, cancel DMA and read its control register before returning `ETIMEDOUT`. | [Before](firmware-before.log) fails the missing-barrier check; [after](firmware-after.log) covers success and timeout cancellation. |
| SPS/PPS firmware counters were subtracted/copied without validating zero, backwards or excessive lengths. | Validate SPS and strictly larger total size against the allocated 1 MiB bitstream buffer; errors use the existing complete startup cleanup. | [Before](headers-before.log) fails; [after](headers-after.log) passes six invalid cases and valid generation. |

[driver-fixes.patch](driver-fixes.patch) contains only these runtime fixes.
Tests extract the actual driver functions and substitute kernel/hardware
dependencies. They exercise behavior and failure cleanup on the host; they do
not emulate silicon DMA or execute firmware. The HEVC shutdown issue is a
source-confirmed requeue window, not a captured use-after-free crash.
**No causal link was established to the historic LAN outage or H.264 299/300.**

## Verification

- [Host checks](host-tests.log): 60 Python tests, C ASan/UBSan, ShellCheck,
  frame-pool/copy/EOS and codec/client regressions passed.
- [Style](checkpatch.log): strict checkpatch, zero errors/warnings/checks.
- Verified original Linux 5.15.79 archive and Armbian baseline; original
  configuration built `vmlinux modules` on the host, with 16182 symbols.
  Full log remains in ignored `build/codec-development-2026-11-23/kernel-build.log`.
- [External modules](modules-build.log) and [additive build](additive-build.log)
  passed with matching `5.15.79-meson` vermagic and runtime dependencies.
  All additive source/header/Makefile bytes match the external source;
  decoder and encoder binaries are identical between the two build entrances.
- [Additive application](additive-apply.log) applies without fuzz; the patch
  was freshly exported and compared against the matching clean baseline.
- [Research series](research-check.log): all 28 patches apply without fuzz and
  strict checkpatch passes. New `0027` ports only HEVC STREAMOFF handling.
  [Changed HEVC object](research-object.log) compiles for ARM. A full new
  Linux 7.3.4 kernel/module/DT validation was not repeated.
- [Overlay build](overlay-build.log) passed; provider phandles remain the
  existing target snapshot and still need checking on a new device.

[results.json](results.json) records exact module/patch hashes and limits.
The full 5.15 build has two pre-existing unused-function warnings in baseline
DRM (`meson_video_clock_exit`, `meson_encoder_cvbs_detach`); changed codec
module builds have no warnings.

The original packaged headers attempt failed because its GCC plugin belonged
to another host/compiler. The matching full kernel was rebuilt on this host
with the original configuration and GCC11, retaining stack protection.
Accepted module results use the full build, not the failed headers attempt.

## Deferred work and tracking

Lower-priority extensions have separate plans and primary references:
[GE2D #14](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/14),
[tiles #15](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/15),
[H.263 #16](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/16),
[WMV3 #17](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/17),
[AVS #18](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/18),
[encoder contexts #19](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/19),
[advanced H.264 #20](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/20),
[HEVC long-term references #21](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/21),
[early slice delivery #22](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/22),
[low bitrate / CBR #24](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/24).

The untested HEVC tiles draft is removed from active sources and preserved as
[patch](deferred-hevc-tiles/codec-hevc.patch),
[header](deferred-hevc-tiles/hevc-tiles.h) and
[identity](deferred-hevc-tiles/manifest.json). It is paused, incomplete and
outside the candidate module. No H.263 runtime format/new firmware was added.

[P0 #23](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/23) tracks
these fixes. [Outage #4](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/4)
and [short output #5](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/5)
remain open; host checks do not satisfy their hardware closure criteria.

## Minimal remaining board acceptance

After an idle-device window and usable recovery/evidence route are available:

1. Verify candidate source/module/firmware/overlay identities before loading.
2. Use small H.264/HEVC/JPEG inputs and reference hashes prepared on the build
   host; verify actual picture content and complete counts on the board.
3. Run a bounded HEVC early STREAMOFF/reopen and normal EOS matrix, plus
   encoding after a reversible firmware-load failure. Check shared clocks,
   resource counters and services before/after.
4. Restore the known installation and record results. Keep sustained-load
   experiments paused until outage evidence can survive a failure.

These steps were pending at this host-only snapshot. Subsequent user-authorized
hot deployment and bounded board checks are recorded separately in
[P0 deployment](../p0-deploy-2026-11-23/README.md); no stable release or long
stability acceptance is implied.
