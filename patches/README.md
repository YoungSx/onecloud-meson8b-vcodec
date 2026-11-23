# Kernel and FFmpeg patches

[series](series) contains the single current additive Linux 5.15.79 patch.
Apply it after the matching [Armbian baseline](../baseline/series). It adds
runtime/decoder/encoder modules under drivers/media/platform/meson/meson8b-vcodec.
The deployment still uses a removable runtime overlay.

[ffmpeg-7.0.2-v4l2-drain.patch](ffmpeg-7.0.2-v4l2-drain.patch),
[input/error fixes](ffmpeg-7.0.2-v4l2-bugs.patch) and
[CROPCAP/SAR direction fix](ffmpeg-7.0.2-v4l2-sar.patch) are optional, applied only
with `scripts/build-ffmpeg-test.sh --bugfixes`. Default builds and the production
image use official unmodified FFmpeg.

`research/` holds the upstream candidate series, rebased onto mainline
v7.3.4 on 2026-11-16 and verified by compiling:

| Patch | Subject |
| --- | --- |
| [0000](research/0000-esparser-eos-padding.patch) | pad the ESPARSER end-of-stream buffer (independent of the rest) |
| [0001](research/0001-meson8b-h264-platform.patch) | guarded Meson8b platform bring-up |
| [0002](research/0002-meson8b-vdec-binding.patch) | Meson8b VDEC dt-binding |
| [0003](research/0003-meson8b-vdec-device-tree.patch) | Meson8b VDEC device tree, enabled on ODROID-C1 |
| [0004](research/0004-meson8b-vdec-power-domain-id.patch) | Meson8b VDEC power domain ID |
| [0005](research/0005-meson8b-vdec-power-domain.patch) | Meson8b VDEC power domain in the Amlogic pmdomain driver |
| [0006](research/0006-meson-vdec-ao-power-domain.patch) | let a power domain own the VDEC AO bits |
| [0007](research/0007-vdec-binding-optional-ao-sysctrl.patch) | make `amlogic,ao-sysctrl` conditional on the domain |
| [0008](research/0008-meson8b-vdec-power-domain-dt.patch) | attach the VDEC to its power domain |
| [0009](research/0009-meson8b-dos-syscon-binding.patch) | Meson8b DOS syscon binding |
| [0010](research/0010-vdec-binding-dos-syscon.patch) | let the decoder take a DOS syscon |
| [0011](research/0011-meson-vdec-dos-syscon.patch) | access a shared DOS through a syscon |
| [0012](research/0012-meson8b-vdec-dos-syscon-dt.patch) | describe the DOS aperture as a syscon |
| [0013](research/0013-meson8b-capture-conversion.patch) | convert Meson8b capture output in software |
| [0014](research/0014-meson8b-capture-neon.patch) | swap the capture words with NEON where the caller allows it |

They apply with `patch -p1 -F0` to pristine v7.3.4, and the module, every ARM
DTB and the binding schema check all pass. Reproduce the apply/checkpatch part
with `bash scripts/check-mainline-port.sh /path/to/linux-7.3.4`, which copies
only the touched files into a temporary tree and runs `checkpatch.pl --strict`
on each patch. None of it ran on S805 hardware: capture conversion and the
shared power-domain/DOS ownership are implemented, but unproven on the
silicon; see the
[port record](../docs/research-validation.md). The research series is
not in the current 5.15.79 series and must not be stacked on the current patch.
CI compares a freshly exported current patch against the checked-in file.

The current research series has 28 patches (`0000`–`0027`); the table above
records its initial platform split. Later patches port the independent pool,
existing VDEC1/HEVC formats and IRQ/EOS/DMA/clock guards. Current scope is in
[portability](../docs/portability.md); none is hardware-accepted on Linux7.3.4.
`0027` discards HEVC pictures during STREAMOFF; it passed series/checkpatch
and changed-object ARM compilation, with no new full research-kernel or DT build.
