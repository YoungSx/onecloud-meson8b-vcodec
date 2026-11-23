# P0 deployment and bounded hardware checks, 2026-11-23

Installed decoder/encoder source is
`3c7d7854bcab3b93a716b7da84002015913082f0`. The existing runtime module,
overlay and firmware were retained; their source matches the codec commit,
but the runtime binary remains the previously deployed build. No boot kernel,
boot DT, CMA, production FFmpeg or application configuration was changed.

## Hardware checks

[Trial receipt](trial.json) records the temporary load, tests and successful
restoration of the previous complete installation before the final upgrade.

- H.264 320x240: 50/50 picture hashes match the build-host software reference.
- HEVC small/crop/1080p: 10/10, 10/10 and 30/30 matching picture hashes.
- Four HEVC early STREAMOFF cases: CAPTURE counts 1/4, OUTPUT-first and
  CAPTURE-first shutdown. Each closes after three pictures and reopens for
  ten complete matching HEVC pictures.
- H.264 encoding: 12 complete pictures, including a same-fd queue restart
  before picture six. JPEG: three complete pictures.
- Missing and truncated AVC firmware fail with exit 1, then restoration
  encodes three pictures successfully after each failure. Firmware bytes are
  restored in `finally`; the original package is reloaded at trial exit.

All inputs, software decode references and ARM clients were prepared on the
build host. The [prepared encoder client](encode-prepared.c) reads NV12 rows
from `RAW_INPUT` during these tests, avoiding image generation on WanOS.
The script's fallback generator was not used. [Input/tool hashes](inputs.json)
record the exact bytes. [Trial script](trial.py) is a dated reproduction, not
an instruction to repeat production tests.

Encoded H.264/JPEG outputs were retrieved and independently decoded/count-checked
on the build host. H.264 average full-image PSNR is **43.318 dB**, minimum
per-picture combined PSNR **42.486 dB** for this 12-picture 320x240 test pattern;
see [quality log](encoded-quality.log). These are bounded content checks,
not a general quality or rate-control guarantee.

## Final installation

[Installed receipt](installed.json) verifies final source/package identity,
service active, zero pool slots/bytes, four loopback HTTP 200 responses and
retained original installation. An additional installed-module H.264 50/50
and HEVC 1080p 30/30 checksum smoke passed. OpenList's container start time
did not change. Loopback checks do not alone prove LAN connectivity; the SSH
control path also remained available throughout the codec tests.

[Production-container transcode](container-transcode.json) also passes using
the existing unmodified FFmpeg 8.1.3: hardware H.264 decode plus encode returns
50 pictures at 320x240, independently decoded/count-checked on the build host.
The container was not restarted. This small regression is not a new 1080p
throughput or sustained application-playback acceptance.

| Installed file | SHA256 |
| --- | --- |
| meson-vdec.ko | `34953c44cdfcaa997f96157a4036b6899bd9c6136a0111bf6c52ccdcedca6baa` |
| meson-venc.ko | `7d31095d1fa82a4565f3175d20badb5e3ed3ec7d476065d16b895cafbe1817d2` |
| meson8b-runtime.ko | `7ca640be512c76d71d2b3606c2a1bf4b0d7f3ca0db12896706ee6649abd6cb32` |
| Overlay | `72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e` |

[Source hashes](source-state.json) match every compiled source to the driver
commit. The package's `SOURCE_COMMIT` and explicit source-state document were
updated after the driver commit and the complete manifest was checked again.
The runtime was reused without recompilation/replacement; its code/overlay
has no difference from the previous deployed source.

[Clock before](clock-before.log)/[after](clock-after.log) retain MPLL2 at
499996410 Hz and Ethernet RGMII TX at 124999102 Hz. No new BUG/WARNING/Oops,
panic or lockup appeared in the retained kernel delta; selected codec lines
are in [kernel log](codec-kernel.log). Raw production logs remain private.

## Boundaries and recovery

Before loading, the complete previous installation, firmware state and old
rollback helper were copied to the build host and archive integrity checked.
The previous `5fedae9` installation is also retained beside the installed
package. The original v0.4.0 recovery materials remain intact.

Two new SSH connections timed out during the initial larger file transfer,
before any module switch. The pending transfer was stopped and a verified
2 MiB payload sent after connectivity returned. The candidate had not run
at that point, so this is not classified as a codec regression.

The [host regressions and cross-build](../p0-offline-2026-11-23/README.md)
preceded hardware execution. The current installation passes hot-load and
bounded codec/error/reopen checks. New cold-boot, abnormal reboot and long
stability acceptance are still pending. The historical outage and 299/300
causal investigations remain open; no long production load was resumed.
