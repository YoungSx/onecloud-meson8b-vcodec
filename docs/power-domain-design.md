# Meson8b shared DOS ownership design

2026-11-16. Design note for the upstream series; not implemented and not
validated on hardware. It exists because the runtime stack solves this
outside the kernel (a helper module takes the DOS aperture and exports a
locked bit-update), and that workaround is not acceptable upstream.

## What is actually shared

Two engines sit behind one DOS aperture at `0xd0050000`:

| Resource | VDEC1 | HCODEC | Overlap |
| --- | --- | --- | --- |
| DOS aperture | `0xd0050000` | `0xd0050000` | whole window |
| `DOS_GCLK_EN0` | bits 0–9 | bits 12–26 | disjoint |
| `DOS_MEM_PD_VDEC` `0xfcc0` | whole register | — | none |
| `DOS_MEM_PD_HCODEC` `0xfcc8` | — | whole register | none |
| AO `0x08` sleep | bits 2–3 | bits 0–1 | one register |
| AO `0x0c` isolation | bits 6–7 | bits 4–5 | one register |
| `AO_RTI_PWR_CNTL_REG0` | — | `0x10`, `& ~0x18` | HCODEC only |

So the register-level overlap is small and already mask-safe: the AO bits are
distinct bitfields, and the only whole-register writes are per-engine DOS
memory power-down registers. The real problem is that both engines need the
same `ioremap` and both perform power sequencing on the same syscon.

Mainline has neither a Meson8b VDEC power domain nor an HCODEC driver, so
nothing owns this today. `meson8b_pwrc_domains[]` in
`drivers/pmdomain/amlogic/meson-ee-pwrc.c` lists only VPU, ethernet memory
and audio-DSP memory.

## Proposal

1. **AO power goes to genpd, not to the drivers.** Add
   `PWRC_MESON8_VDEC_ID` and `PWRC_MESON8_HCODEC_ID` to
   `include/dt-bindings/power/meson8-power.h` and to
   `meson8b_pwrc_domains[]`, each a `TOP_PD()` with
   `sleep_reg = MESON8_AO_RTI_GEN_PWR_SLEEP0` (`0x08`) and
   `iso_reg = MESON8_AO_RTI_GEN_PWR_ISO0` (`0x0c`), masks `GENMASK(3, 2)` /
   `GENMASK(7, 6)` for VDEC1 and `GENMASK(1, 0)` / `GENMASK(5, 4)` for HCODEC.
   Both drivers then drop their direct `regmap_update_bits` on the AO syscon
   and use `pm_runtime`/`dev_pm_domain_attach`. The two domains are siblings,
   so enabling one cannot disturb the other, and genpd's own locking replaces
   the runtime helper's spinlock.

2. **DOS memory power-down stays with the engine.** `DOS_MEM_PD_VDEC` and
   `DOS_MEM_PD_HCODEC` are per-engine registers inside the DOS window, and
   `meson-ee-pwrc` routes memory power-down through the HHI regmap, which does
   not reach them. Teaching pwrc a second regmap for this would buy nothing:
   no other consumer shares those registers.

3. **The aperture becomes a syscon.** Declare `dos: syscon@d0050000` in
   `meson8b.dtsi` and let both engines take it as a phandle, using
   `regmap_mmio` with `regmap_config.max_register = 0x10000`. `regmap` gives
   the serialization that the runtime helper hand-rolls, and it removes the
   `devm_platform_ioremap_resource_byname()` exclusivity conflict that makes a
   second engine fail to probe today. Mainline's vdec driver reads DOS
   registers through `amvdec_read_dos()`; that indirection already exists, so
   only its body changes.

4. **DT shape** once all three land:

   ```dts
   dos: syscon@d0050000 {
           compatible = "amlogic,meson8b-dos", "syscon";
           reg = <0xd0050000 0x10000>;
   };

   vdec: video-codec@d0050000 {
           reg = <0xd0050000 0x10000>, <0xc110a580 0xe4>;
           amlogic,dos = <&dos>;
           power-domains = <&pwrc PWRC_MESON8_VDEC_ID>;
           /* no amlogic,ao-sysctrl: genpd owns the AO bits */
   };
   ```

   `amlogic,ao-sysctrl` has to stay in the binding for the existing GX/G12
   platforms, which still write those registers directly.

## Ordering and risk

The three parts are independent enough to land separately, but only the
combination removes the helper: (1) alone leaves the aperture conflict,
(3) alone leaves the AO writes unowned. The HCODEC driver does not exist
upstream, so (1) and (3) are reviewable now and the encoder follows later.

Risk to check on hardware before submitting: whether the VDEC1 sleep bits
must be cleared before or after `clk_prepare_enable(vdec_1)`.

The vendor tree settles what the silicon was brought up with, though not
whether it is mandatory. `vdec_poweron()` in
`sources/vendor/drivers/amlogic/amports/vdec.c` for `MESON_CPU_TYPE >=
MESON8` runs, in order: clear `AO_RTI_GEN_PWR_SLEEP0` bits 3:2 → `udelay(10)`
→ `DOS_SW_RESET0` → `vdec_clock_enable()` → `DOS_MEM_PD_VDEC = 0` → clear
`AO_RTI_GEN_PWR_ISO0` bits 7:6 → reset `DOS_VDEC_MCRCC_STALL_CTRL`. Power
first, clock second. The encoder side does the same: `avc_poweron()` clears
`AO_RTI_PWR_CNTL_REG0` bits 4:3, then `AO_RTI_GEN_PWR_SLEEP0` bits 1:0, and
only then resets `DOS_SW_RESET1` and enables the clock.

Mainline `vdec_1_start()` currently does the opposite — `clk_prepare_enable()`
before clearing the sleep bits — and it works on GX/G12. A genpd conversion
makes the driver's order irrelevant, because the power sequencing moves out
of `vdec_1_start()` entirely: `platform_probe()` calls
`dev_pm_domain_attach()` with `PD_FLAG_ATTACH_POWER_ON`, and
`__genpd_dev_pm_attach()` runs `genpd_power_on()` before `drv->probe()` is
entered at all. So `meson_ee_pwrc_on()` — sleep bits, memory power-down,
isolation — completes before the vdec driver can enable `vdec_1`, which is
the vendor order.

Verifying the conversion on S805 hardware is still required; the vendor
sequence and the attach ordering are evidence of intent, not proof that the
reversed order breaks anything.
