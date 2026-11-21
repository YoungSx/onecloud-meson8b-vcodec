# Portability and the application boundary

The deployed v0.4.0 stack is a compatibility port for OneCloud on the original
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
[research port](../evidence/mainline-2026-11-16/PORT.md).

The research port's patch 0013 still allocates a shadow per CAPTURE buffer.
It predates the v0.4 independent frame pool and does not inherit its memory
and lifetime acceptance. Before using it, port the pool and its source-change
and STREAMOFF ordering, then repeat frame counts, CAPTURE 1/2/4/20/32, dynamic
resolution and concurrent encode checks. Another board/kernel needs hardware
validation; current v0.4 results cannot be copied onto that research series.

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
records its own limits. Browser rendering, sustained playback and arbitrary
remote sources still need their own evidence.

This phase does not replace the production kernel, DT, firmware or modules,
submit the research series upstream or claim multi-board support.
