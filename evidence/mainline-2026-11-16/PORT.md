# Mainline port build, 2026-11-16

Compile-time port of the research series onto current mainline. No S805
hardware was involved: this proves the patches apply, build and pass the
binding schema, not that Meson8b decoding works on mainline.

## Base

- Source: `linux-7.3.4.tar.xz` from cdn.kernel.org, `xz -t` clean.
  Pristine file hashes in [hashes.txt](hashes.txt).
- Cross toolchain: `arm-linux-gnueabihf-gcc` 13.3.0.
- Config: `multi_v7_defconfig` plus `CONFIG_STAGING_MEDIA=y`,
  `CONFIG_VIDEO_MESON_VDEC=m`, `CONFIG_MESON_CANVAS=y`
  ([config-fragment.log](config-fragment.log)). `CONFIG_VIDEO_MESON_VDEC`
  is only reachable with staging media enabled, which is why the earlier
  attempt produced nothing.

## Rebase

The three research patches were regenerated against pristine v7.3.4, so they
apply with `patch -p1 -F0` and no rejects:

| Patch | Change on v7.3.4 |
| --- | --- |
| 0001 | `vdec_platform.h` gained the Meson8b revision; `vdec_1.c` AO writes go through the `ao_sysctrl_offset`-derived `sleep_reg`/`iso_reg` |
| 0002 | line offsets only |
| 0003 | retargeted: mainline has no `meson8b-onecloud.dts`, so the node goes in `meson8b.dtsi` under `/soc` and the enable hunk targets `meson8b-odroidc1.dts` |

Applying the regenerated series to a pristine tree reproduces the built tree
byte for byte.

## Results

- `drivers/staging/media/meson/vdec/meson-vdec.ko` builds:
  vermagic `7.3.4 SMP mod_unload ARMv7 p2v8`, `of:` alias
  `amlogic,meson8b-vdec`, firmware `meson/vdec/meson8b_h264.bin`.
  See [module-build.log](module-build.log).
- All arm DTBs build ([dtbs-build.log](dtbs-build.log)).
- `dtbs_check` limited to `amlogic,gx-vdec.yaml` passes with dtschema
  2026.9 ([dtbs-check.log](dtbs-check.log)).
- The decompiled `meson8b-odroidc1.dtb` carries the expected node: DOS
  `0xd0050000`, parser `0xc110a580`, IRQs 44/32, three clocks, canvas and
  PMU phandles, `status = "okay"`.

`patches/research/0000-esparser-eos-padding.patch` is a standalone fix for the
upstream end-of-stream defect and applies to pristine v7.3.4 on its own; the
module above was rebuilt with it and still builds
(`efeb04970d9c2730d891576edec742dcc253068a8b228747e6d02d485095bd0e`). It is not
part of the three-patch Meson8b series because it affects every platform.

`KBUILD_MODPOST_WARN=1` was needed only because the build stopped at a single
`.ko` without a full `vmlinux`/`Module.symvers`; the ten reported unresolved
symbols are all core kernel exports (`v4l2_m2m_init`, `vb2_queue_init`,
`devm_request_threaded_irq`, …), which is what a partial build expects. A
full `make modules` is not run here: 1.8 GB source plus 190 MB output against
9.1 GB free.

## What is still missing for upstream

- Capture byte order. The driver still rejects STREAMON with `-EOPNOTSUPP`
  on Meson8b; the production stack solves this with an NV12 shadow
  conversion outside mainline's reach.
- Shared DOS/power-domain ownership: VDEC1 and HCODEC both touch PMU fields,
  and the node currently has no `power-domains` link.
- Nothing in this series has run on hardware; the vendor firmware is not in
  linux-firmware.
