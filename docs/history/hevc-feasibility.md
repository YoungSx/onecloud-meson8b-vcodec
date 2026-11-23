> 历史快照：此处描述研究阶段，不代表当前功能。当前范围见[验证指南](../validation.md)。

> 2026-11-14：本文件为可行性来源记录；HEVC Main 8-bit 实现已完成并实测，当前验收见[HEVC 记录](../validation-records.md#解码与帧池)。

# Meson8b HEVC feasibility, corrected 2026-11-14

S805/Meson8b has a dedicated HEVC decode block in the pinned vendor source.
The old ROADMAP statements "M8B has no HEVC core" are incorrect. This is a
source-level feasibility conclusion, not a claim of a working V4L2 HEVC
implementation or a hardware decode test in this repository.

Evidence from Hardkernel linux commit
5aed95d35d252cafc75ce613a3a0052285662de2:

- `drivers/amlogic/amports/amports_config.h:11-15` defines
  `HAS_HEVC_VDEC 1` for `MESON_CPU_TYPE_MESON8B`; `HAS_VDEC2` is zero.
  These are different blocks. The final CONFIG_AM_VDEC_H265 guard can
  disable the HEVC driver at build time; it does not remove silicon.
- `drivers/amlogic/amports/Kconfig:107` makes AM_VDEC_H265 depend on
  ARCH_MESON8B or ARCH_MESON8 and default to y.
- `drivers/amlogic/amports/vh265.c:54` excludes SUPPORT_4K2K specifically
  on Meson8b. The vendor path is limited to 1080p, matching S805 datasheet.
  Its module init marks only original M8 as hevc_unsupport; M8B remains
  supported, while M8M2 is separately marked 4k.
- `drivers/amlogic/amports/amvdec.c:451` loads microcode through
  HEVC_IMEM_DMA_ADR/COUNT/CTRL and starts HEVC_MPSR, not VDEC1 or HCODEC.
- `drivers/amlogic/amports/vdec.c` powers the HEVC island using SLEEP0
  bits 7:6, ISO0 bits 11:10 and DOS_MEM_PD_HEVC. This differs from GX
  isolation masks; GX platform data cannot be used unchanged.
- `drivers/amlogic/amports/m8/ucode/h265/vh265_mc.c` provides the vendor
  HEVC firmware and a SEND_PARAM_WITH_REG variant.
- Linux 5.15.79 `drivers/clk/meson/meson8b.c` exposes
  vdec_hevc_sel/div/en and CLKID_VDEC_HEVC. This corroborates the vendor
  hardware topology, but the clock tree alone is not decode acceptance.

Reproducible source SHA-256 values:

| File under drivers/amlogic/amports | SHA-256 |
| --- | --- |
| amports_config.h | ac731568b56090006a88dd44feac100edcaac52c9969d78a2c1da3892582f345 |
| vh265.c | f84a112acea6277ec0fe5122c312ef3936ce1511a7031648b32959db83b273d2 |
| amvdec.c | c76aaca1e7724c7e14f36a93d118077962baa8298d882dfbbdfbe6f0435d16aa |
| vdec.c | d1ed9e4496e96248c4edc2b318c8bc2f538ea7d50c4c7d33b81ee08796314ce5 |
| m8/ucode/h265/vh265_mc.c | e442a7953c617407107ed65bd8bdd9e1ffe8eb49597a3cb3b54ab9173fd691b2 |

V4L2 implementation still needs a separate Meson8b HEVC engine path:
correct clocks, AO power/reset/IRQ setup, vendor scratch/RPM command
protocol, decoded picture buffer and reference lists, SAO/MPRED workspace,
linear NV12 output, crop/colorimetry, EOS and recovery. The upstream
5.15 staging directory has a HEVC engine but no complete stateful HEVC
codec handler suitable for this firmware (the common helpers serve VP9).
The VDEC1 H.264 platform entry is not an HEVC implementation.

Required verification before advertising HEVC: firmware provenance and
layout; external build; bounded small-frame hardware bring-up; main-profile
1080p picture comparison, reorder/drain/error tests; resource peak on this
64 MiB CMA system; ffmpeg hevc_v4l2m2m and service/rollback checks.
No HEVC registers were touched on the production device during this audit.
