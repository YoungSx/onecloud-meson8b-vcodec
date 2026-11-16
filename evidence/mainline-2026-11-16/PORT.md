# Mainline port build, 2026-11-16

Compile-time port of the research series onto current mainline. No S805
hardware was involved: this proves the patches apply, build and pass the
binding schema, not that Meson8b decoding works on mainline.

## Base

- Source: `linux-7.3.4.tar.xz` from cdn.kernel.org, `xz -t` clean, SHA-256
  `b11b6df8295e8e991ff040b71aaa4994a8b55caa23b6b092907f195b5840fd1f`.
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
- All four patches are clean under `checkpatch.pl --strict` (0 errors,
  0 warnings), and applying 0000→0003 in order to a pristine tree reproduces
  the built tree byte for byte.
- The decompiled `meson8b-odroidc1.dtb` carries the expected node: DOS
  `0xd0050000`, parser `0xc110a580`, IRQs 44/32, three clocks, canvas and
  PMU phandles, `status = "okay"`.

`patches/research/0000-esparser-eos-padding.patch` is a standalone fix for the
upstream end-of-stream defect and applies to pristine v7.3.4 on its own; the
module above was rebuilt with it and still builds. It is not part of the
three-patch Meson8b series because it affects every platform.

## Full build

The first module builds stopped at a single `.ko` with no `vmlinux`, so
modpost reported 105 unresolved symbols and `KBUILD_MODPOST_WARN=1` was
needed to link anything. A full build removes that caveat. `vmlinux`
(`multi_v7_defconfig`, `-j4`, 143 MB) produced `vmlinux.symvers`, and
`make modules` then linked all 938 configured modules with no `WARNING`
line and no unresolved symbol
([modules-full-build.log](modules-full-build.log), 952 `=m` symbols in
`.config`). modpost ran against a real `vmlinux.o`, so the dependency list in
`modinfo` is now resolved rather than inferred:

```
depends=v4l2-mem2mem,videobuf2-v4l2,videobuf2-dma-contig,videobuf2-common
vermagic=7.3.4 SMP mod_unload ARMv7 p2v8
```

The full-build hash is `9d6e8080c53e1cb1cf35df352fe58c78584dfab6d0f2325fe0858251895e24f5`,
which supersedes the earlier `af170a6e…` recorded from the partial
single-module link. The two are not byte-comparable — the partial link had no
`vmlinux`/`Module.symvers`, so its build id and modpost-generated `.mod.c`
differ — and the earlier `.ko` was not kept to diff. The source state is the
same either way; only the link inputs changed.

The full build is deterministic: reverting 0000 and rebuilding gives
`5d2689ab94200db3e3dda8e1b1602f6972ff2ae43f89bdc9b5b8ca8460947a51`
([without 0000](modules-full-build-without-0000.log)), and re-applying it
reproduces `9d6e8080…`
([rebuild](modules-full-build-rebuild.log)). So 0000 does change the compiled
driver, as it must, and the hash above is stable across reruns.

Neither hash is a loadable artifact for the S805: this is an ARMv7 mainline
build, not the 5.15.79-meson production module.

## What is still missing for upstream

- Capture byte order. The driver still rejects STREAMON with `-EOPNOTSUPP`
  on Meson8b; the production stack solves this with an NV12 shadow
  conversion outside mainline's reach.
- Shared DOS/power-domain ownership: VDEC1 and HCODEC both touch PMU fields,
  and the node currently has no `power-domains` link.
- Nothing in this series has run on hardware; the vendor firmware is not in
  linux-firmware.
