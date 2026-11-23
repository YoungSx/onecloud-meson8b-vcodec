# Portability and the application boundary

The runtime stack is a compatibility port for OneCloud on the original
5.15.79-meson kernel. It exposes standard stateful V4L2 M2M devices. FFmpeg and
applications do not need to know DOS registers, firmware layout, canvas byte
order or fixed device numbers; they discover supported formats through V4L2.

The runtime loader intentionally checks the OneCloud compatible, reserves the
Meson8b DOS aperture and applies an overlay whose provider phandles match the
live device tree. This keeps the original boot kernel and DT in place and
allows removal to restore the previous state. Deleting those checks does not
make the driver portable and can attach resources to the wrong board.

Retain these boundaries:

- Hardware references stay in the independent v0.4 frame pool. User CAPTURE
  buffers receive converted output; their count does not dictate CMA use.
- Shared DOS control fields have one owner and locked masked updates. Sleep
  and isolation use regmap masks so the two engines cannot overwrite them.
- Codec formats, controls, firmware protocol and EOS/error handling remain
  driver responsibilities. OpenList selects a standard FFmpeg wrapper and
  reports failure instead of embedding silicon-specific workarounds.
- Docker device mappings authorize access to host devices even when the
  application already selects V4L2 codecs correctly.

The next portable-kernel phase should use the existing DOS syscon/regmap and
genpd/runtime-PM design, then static board DT/bindings and platform data for
each supported SoC. Retain the runtime loader as the old-kernel deployment
adapter. See [shared ownership](power-domain-design.md) and the
[research port](research-validation.md).

Research patches0016–0026 port the independent pool, IRQ/completion/EOS
ordering, existing VDEC1 formats and HEVC Main8 protocol to Linux 7.3.4.
The DOS syscon owns shared registers; genpd owns VDEC1/HEVC AO bridge power
and each engine owns its DOS memory power gates. Fixed clock parents avoid
MPLL retuning. All27 patches apply without fuzz and pass checkpatch;
full kernel/modules,ARM DTB/binding and ODROID checks pass locally.
Research support has not run on a board. Known stream/content limits remain.

Another board/kernel still requires hardware validation, CAPTURE and dynamic
resolution tests and a rescue route. [P0–P3 results](validation-records.md)
separate the build from hardware acceptance. CI now rebuilds this research
series independently; it does not prove a board's decode or power behavior.

Encoder standard H.264 profile/level controls expose only the firmware's
verified Baseline/Level 4.0 values. Do not claim
High/Main or another level without firmware and output-SPS evidence. Apps
still need the actual output profile to generate HLS CODECS accurately.

On 2026-11-20 the OpenList fork was deployed with the existing v0.4 driver.
Bounded HTTP HLS checks covered H.264 with audio at 320×240 and 1080p,
including forward/backward segment requests. Follow-up application fixes
cover probed filters, device serialization, FFmpeg capability matching, shared
local/remote arguments and out-of-order segment notifications. These are
application improvements, not a new driver hardware acceptance. The
[OpenList implementation](https://github.com/YoungSx/OpenList/tree/feat/transcode-v4l2m2m/internal/transcode/v4l2m2m)
records its own limits. Latest clean-image bounded browser playback and seeks were checked separately
on 2026-11-22; buffering, realtime throughput, sustained playback and arbitrary
remote sources remain separate acceptance boundaries.

The runtime modules and overlay were upgraded by explicit user request; the
production boot kernel,boot DT and CMA stay unchanged. Research is not booted
on production,submitted upstream or claimed as multi-board acceptance.
