# Kernel and FFmpeg patches

[series](series) contains the single current additive Linux 5.15.79 patch.
Apply it after the matching [Armbian baseline](../baseline/series). It adds
runtime/decoder/encoder modules under drivers/media/platform/meson/meson8b-vcodec.
The deployment still uses a removable runtime overlay.

[ffmpeg-7.0.2-v4l2-drain.patch](ffmpeg-7.0.2-v4l2-drain.patch) is applied only
to the independent static test tool, not to the production FFmpeg.

`research/` holds the upstream candidate series, rebased onto mainline
v7.3.4 on 2026-11-16 and verified by compiling:

| Patch | Subject |
| --- | --- |
| [0000](research/0000-esparser-eos-padding.patch) | pad the ESPARSER end-of-stream buffer (independent of the rest) |
| [0001](research/0001-meson8b-h264-platform.patch) | guarded Meson8b platform bring-up |
| [0002](research/0002-meson8b-vdec-binding.patch) | Meson8b VDEC dt-binding |
| [0003](research/0003-meson8b-vdec-device-tree.patch) | Meson8b VDEC device tree, enabled on ODROID-C1 |

They apply with `patch -p1 -F0` to pristine v7.3.4, and the module, every ARM
DTB and the binding schema check all pass. Reproduce the apply/checkpatch part
with `bash scripts/check-mainline-port.sh /path/to/linux-7.3.4`, which copies
only the touched files into a temporary tree and runs `checkpatch.pl --strict`
on each patch. None of it ran on S805 hardware and
Meson8b streaming still returns `-EOPNOTSUPP`; see the
[port record](../evidence/mainline-2026-11-16/PORT.md). The research series is
not in the current 5.15.79 series and must not be stacked on the current patch.
CI compares a freshly exported current patch against the checked-in file.
