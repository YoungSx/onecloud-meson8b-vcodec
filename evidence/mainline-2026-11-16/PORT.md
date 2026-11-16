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

The three bring-up patches were regenerated against pristine v7.3.4, so they
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
- All thirteen patches are clean under `checkpatch.pl --strict` (0 errors,
  0 warnings) and apply in order to a pristine tree
  ([series check](series-check.log)).
- The decompiled `meson8b-odroidc1.dtb` carries the expected node: DOS
  `0xd0050000`, parser `0xc110a580`, IRQs 44/32, three clocks, canvas
  phandle, `power-domains` and `status = "okay"`.

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

## Power domain

The three-patch series left the AO sleep and isolation bits with the driver,
so a Meson8b decoder had to write the AO syscon itself and any second engine
behind the DOS aperture would race it. Five patches now move that ownership to
genpd, which is what [the design note](../../docs/power-domain-design.md)
proposed:

| Patch | Change on v7.3.4 |
| --- | --- |
| 0004 | `PWRC_MESON8_VDEC_ID` added to `meson8-power.h` |
| 0005 | `meson8b_pwrc_domains[]` gains a VDEC top domain over `GENMASK(3, 2)` of `AO_RTI_GEN_PWR_SLEEP0` and `GENMASK(7, 6)` of `AO_RTI_GEN_PWR_ISO0`; new `TOP_PD_NO_MEM()` because VDEC1 memory power-down is `DOS_MEM_PD_VDEC` in the DOS aperture, not an HHI register |
| 0006 | the driver takes the AO bits from the domain when it has one, wraps the device in runtime PM, and writes AO only where there is no domain (`amvdec_ao_power()`); `amlogic,ao-sysctrl` becomes optional |
| 0007 | binding: `power-domains` described, `amlogic,ao-sysctrl` required only when there is no domain |
| 0008 | `meson8b.dtsi`: the VDEC node takes `power-domains` instead of the sysctrl phandle |

Ordering is structural rather than a sequence the driver has to get right.
`platform_probe()` calls `dev_pm_domain_attach()` with
`PD_FLAG_ATTACH_POWER_ON`, so `genpd_power_on()` — sleep bits, then isolation —
completes before `vdec_probe()` runs; `vdec_poweron()` then takes a runtime
reference before enabling `dos_parser`/`dos`, and `vdec_poweroff()` drops it
after the engine reset, so the domain powers off last. That is the vendor
`vdec_poweron()` order, without the driver encoding it.

Verified at compile level only:

- The nine patches then in the series applied to pristine v7.3.4 in order and
  were clean under `checkpatch.pl --strict` ([series check](series-check.log)).
- `meson-vdec.ko` and `meson-ee-pwrc.o` build with no warning
  ([modules](modules-power-domain.log), [pwrc](pwrc-build.log)); the module
  hash is `5ad19f41f5d31ffc4ae441570eaee28c7da65655be4ab7c3e6d0af2138e2ef47`,
  reproducible across relinks, and `depends`/`vermagic` are unchanged.
- All six Meson8/8b/8m2 DTBs validate against the processed schema bundle with
  no complaint ([positive](dtbs-check-power-domain.log)), and the negative case
  is caught: dropping `power-domains` from the ODROID-C1 node without adding
  `amlogic,ao-sysctrl` reports "'amlogic,ao-sysctrl' is a required property"
  ([negative](dtbs-check-negative.log)).
- The same six DTBs validate with the DOS syscon patches on top
  ([positive](dtbs-check-dos-syscon.log)), and the aperture case is caught:
  removing `amlogic,dos` while leaving one region reports that `reg` and
  `reg-names` need two entries ([negative](dtbs-check-dos-negative.log)).
- The decompiled ODROID-C1 DTB has `power-domains = <&pwrc 3>` and no
  `amlogic,ao-sysctrl`, and the phandle resolves to the pwrc node.

Not verified: anything at runtime. Whether the reversed clock/power order
matters on this silicon, whether the domain survives a stop/start cycle, and
whether `DOS_MEM_PD_VDEC` is safe to keep in the driver once genpd owns the AO
bits all need the board. Meson8b still returns `-EOPNOTSUPP` from STREAMON, so
none of this changes what the port can do.

## DOS syscon

The other half of the design is the aperture itself. Four patches make it
shareable, which is what a second engine behind the same window needs:

| Patch | Change on v7.3.4 |
| --- | --- |
| 0009 | `amlogic,meson8b-dos` added to the `syscon.yaml` allowlist, so the window can be described once |
| 0010 | binding: `amlogic,dos` phandle; one region (parser only) with it, both regions without it |
| 0011 | `amvdec_read_dos()`/`amvdec_write_dos()` use the regmap when the phandle is present and keep `readl_relaxed()`/`writel_relaxed()` otherwise; the bit helpers become `regmap_update_bits()` so the read-modify-write is one locked op; the two raw HEVC accesses go through the helpers |
| 0012 | `meson8b.dtsi`: the aperture becomes `dos: syscon@d0050000`, and the decoder moves to `video-codec@c110a580` carrying only `esparser` |

The regmap is the `regmap_mmio` bus, which sets `fast_io`, so its lock is a
spinlock and the hardirq writes in `codec_h264_isr()` stay legal. Every DOS
offset in `dos_regs.h` and `hevc_regs.h` is 4-aligned, which is the
`reg_stride` `of_syscon_register()` derives, so no access is rejected.

The bit helpers matter more than the plain accesses. `DOS_GCLK_EN0` holds a
bitfield per engine, and the old `amvdec_write_dos_bits()` read and wrote it
in two steps; with a second engine on the aperture that pair is a lost-update
race, and two separately-locked regmap ops would be just as racy. Under a
syscon the helpers are `regmap_update_bits()`, which holds one lock across
both halves.

Two things this buys, and one it costs. The window is mapped once, so VDEC1
and HCODEC no longer conflict in `devm_platform_ioremap_resource_byname()`;
bit updates are serialised across both engines by the shared regmap lock
instead of by a helper module's own spinlock. It costs a locked
read-modify-write on every DOS access, including `vififo_level()` in the
parser work handler, where the old path was a bare `readl_relaxed()`. That
overhead is unmeasured and matters most on this CPU.

Verified at compile level only:

- Thirteen patches apply to pristine v7.3.4 in order and are clean under
  `checkpatch.pl --strict` ([series check](series-check.log)).
- The module builds with no warning and the hash is
  `8fd4f7731534dcb7723aa37b3807119ac9fb4011863cbd9aafcca1fb237cd3e8`,
  reproduced across a relink.
- All six Meson8/8b/8m2 DTBs validate against the full processed schema
  bundle ([positive](dtbs-check-dos-syscon.log), empty because there is
  nothing to report), and the negative case is caught: dropping `amlogic,dos`
  while leaving the single parser region reports `reg` and `reg-names`
  "too short" ([negative](dtbs-check-dos-negative.log)). A bogus property on
  the same node is also rejected, so the empty positive log is not a
  validator that silently passes everything.
- The binding is shared, so the platforms that keep the old shape were
  checked too: GXBB, GXL, G12A and SM1 DTBs validate unchanged with both
  regions and `amlogic,ao-sysctrl` ([arm64](dtbs-check-dos-arm64.log)).
- The ODROID-C1 DTB has the syscon at `0xd0050000`, the decoder at
  `0xc110a580` with `amlogic,dos` pointing at it, and no duplicate
  unit-address — the reason the decoder node had to move off `0xd0050000`.

Nothing was run on hardware.

## What is still missing for upstream

- Capture byte order. The driver still rejects STREAMON with `-EOPNOTSUPP`
  on Meson8b; the production stack solves this with an NV12 shadow
  conversion outside mainline's reach.
- The encoder itself. Nothing upstream consumes the shared aperture yet, so
  the syscon is reviewable but its benefit is prospective until an HCODEC
  driver exists.
- Nothing in this series has run on hardware; the vendor firmware is not in
  linux-firmware.
