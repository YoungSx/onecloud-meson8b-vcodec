# Deployment and acceptance status, 2026-11-15

Final driver is runtime-only on original 5.15.79-meson, with no reboot or
/boot writes. JPEG/HEVC and concurrent B-frame input are validated on
OneCloud. The systemd loader is reversible and its start/stop, failure
cleanup and removal were tested. The final installation leaves codecs
available, with installation-time originals retained in its state backup.

See evidence/HEVC-ACCEPTANCE-2026-11-14.md and
BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md for frame counts, hash/PSNR checks,
malformed data boundaries and mixed-load run length. HEVC supports Main
8-bit linear output. Main10 and tiles remain explicitly unsupported; no
claim of arbitrary-stream or indefinite production soak is made.

Original WanOS ffmpeg remains installed. The independent static tool has
HEVC M2M decoding and a separate FFmpeg V4L2 drain/POLLERR patch. Both
sources and exact build flags are in the deliverable. On malformed data
ffmpeg may output partial frames; exit code alone is not acceptance.
Use scripts/verify-hashes.py and the recorded frame counts for correctness.

OpenList and Arcane were found stopped at the inherited baseline; their
original containers were restarted without configuration changes. They,
SublinkPro, VoCat and Home Assistant remained running during mixed-load
acceptance. The final HTTP and container checks are recorded separately.

The local linux-media RFC draft is prepared but unsubmitted. A current
mainline static-DT/power-domain port and review are still required for
upstream acceptance. No email or external publication was requested.

Verified release archive: build/onecloud-verified-release.tar.gz, SHA-256
71f7922e9a60cb9c4ae3bebb34181bf95f467a99e824f34d7bfd9daf9e3ff903.
Package source commit bd1cb79ad502d0b8cdcf9667a0ceb0ca0f7876ab.
Independent tools and all scripts are covered by the manifest.

Installed at /opt/meson8b-vcodec/current. The enabled and active
meson8b-vcodec.service leaves /dev/video0 and /dev/video1 available.

Current installation: package source commit cd4e6b5, kernel release
5.15.79-meson, state /root/venc-deploy/p6/codec-state-r5. Module hashes
meson-vdec b8009e10c4373aaf, meson-venc fa17e35d50f986d2,
meson8b-runtime 70f9025465f8bf4c; all nine firmware files identical to the
previous install. The bd1cb79 installation was removed first and the new one
installed with a fresh state directory; see evidence/p6-2026-11-15/deploy.log
for upgrade, rollback rehearsal and restart results.

The 30-minute soak stopped after 191 iterations (0 failures), short of its
window, and the box reset afterwards. The USB disk behind /mnt/sda2 then
stopped enumerating, so Docker, SublinkPro and VoCat are unavailable until it
is serviced; the codec stack itself recovered automatically and passed decode
and encode retests. Log timestamps on this box are unreliable (no RTC, clock
restored at boot), so the evidence records order and counts rather than a
timeline. See evidence/p6-2026-11-15/outage-2026-11-15.md. A full 30-minute,
2-hour and overnight soak is still outstanding.

Pre-install firmware journal: /root/venc-deploy/verified-codec-state.
After ending codec clients, roll back using:

```sh
bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh
```

The final archive installation passed HEVC frame hashes: 10/10 small,
30/30 1080p B frames, 10/10 crop. JPEG returned all five requested frames,
including timestamps and LAST/EOS checks. See verified-deployment.log,
verified-*.md5 and verified-live-state.log under evidence/hevc-2026-11-14.
The original /boot hashes were rechecked against the initial snapshot.
Later documentation/evidence commits do not change the installed payload.
The rollback restores files as they were immediately before this install;
earlier experiment firmware already present then remains inert after unload.

The standalone v4l2-ctl 1.22.1 tool is under /root/venc-deploy/v4l2-ctl.
It enumerated both M2M devices, all coded/raw formats and controls. Source
and binary accompany build/onecloud-verified-source.tar.gz; the installed
runtime manifest remains unchanged. See v4l2-enumeration.log.

Interim failed runs are retained for diagnosis, not counted as acceptance.
The old recovery failure is hevc-recovery-interim-failure.log; final recovery
and independent-tool checks are hevc-recovery.log and reviewed-validation.log.
final-install.log/installed-hevc.md5 describe the superseded b8263f6 package;
verified-deployment.log and verified-*.md5 describe the final bd1cb79 payload.

Final source companion: build/onecloud-verified-source.tar.gz (136 MiB),
SHA-256 afd36fc89007b9a3793cc91cc8ba4207151c355b9634c47f3ad369d4cca1cb1a.
It contains exact bd1cb79 payload sources and a supplement from
15e4460769514a294b27bad5fcb5d420318e6109, including Linux/FFmpeg/v4l-utils
source archives, pinned microcode sources, reproduction scripts and evidence.
Its complete manifest was checked directly from the final archive.
