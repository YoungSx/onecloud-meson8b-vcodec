# Deployment and acceptance status, 2026-11-22

Current development targets official unmodified FFmpeg. The earlier private
peak/CPB option is retired; its dated acceptance below remains historical.
See [official compatibility](ffmpeg-compatibility.md).

Current explicitly authorized development installation is source
`3ee2fb100d5e9dc4e2bdc02fea562a99e33ea066` on the original5.15.79-meson kernel.
Production OpenList runs `v4.2.10-official-ffmpeg-a845b34-clean`; its FFmpeg
and ffprobe are byte-identical to the pinned official base. See
[current deployment evidence](../evidence/development-2026-11-22/README.md).

Original v0.4.0 package,firmware,unit and rollback state are preserved.
Updates keep the production OpenList start time unchanged. Runtime module
switches and matching overlay updates do not change boot kernel/DT or CMA.
The new source passes bounded content/recovery checks; prior outages still
prevent a new stability/soak claim. Research builds are not installed.

## Installed release history

Historical installation (2026-11-20 07:30 UTC): the GitHub release v0.4.0
runtime package, built by CI from 89dd558, kernel release 5.15.79-meson,
state /root/venc-deploy/p6/codec-state-v040-install. Module hashes meson-vdec
15d5c77d25225e8b, meson-venc 2507c7cc69fb1b00, meson8b-runtime
af61f4dea7b68e06; the vdec module is the frame-pool build, the other two are
byte-identical to v0.3.0, and the firmware files are unchanged. The codec
devices were down 21 s during the upgrade; the regression set passed, a
30-minute load ran 441 iterations with 0 failures, and five service probes
sampled 106 times with no failure. See evidence/p6-2026-11-20/v040-deploy.md.
The v0.3.0 package and its state remain on the box for rollback.

Final read-only recheck on 2026-11-20: all 54 installed payload files pass
SHA256SUMS, module fingerprints match the release package, the unit is
active/enabled, both video devices are present, and all four HTTP entry
points return 200. OpenList, Arcane and Home Assistant containers are
running. The idle decoder pool has zero allocated slots and bytes.
See evidence/p6-2026-11-20/final-live-check.log.

Transcode performance work remains deferred. The user subsequently chose the
OpenList fork, deployed on 2026-11-20. The existing v0.4 driver passed bounded
HTTP HLS checks at 320×240 and 1080p, including audio and forward/backward
segment requests. Follow-up candidates use disposable data; production still
uses the deployed image. Browser and sustained-playback acceptance remain
open. See [portability](portability.md).

## Earlier acceptance and package history

The driver runs as external modules on original 5.15.79-meson. The v0.4.0
upgrade changed no boot files and required no reboot. Earlier cold-start and
service-restart checks are recorded separately. JPEG/HEVC and concurrent B-frame input are validated on
OneCloud. The systemd loader is reversible and its start/stop, failure
cleanup and removal were tested. The final installation leaves codecs
available, with installation-time originals retained in its state backup.

See evidence/HEVC-ACCEPTANCE-2026-11-14.md and
BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md for frame counts, hash/PSNR checks,
malformed data boundaries and mixed-load run length. HEVC supports Main
8-bit linear output. Main10 and tiles remain explicitly unsupported; no
claim of arbitrary-stream or indefinite production soak is made.

Original WanOS ffmpeg remains installed. The independent static tool has
H.264/HEVC M2M, AAC/HLS/MPEG-TS, file/HTTP and optional minimal V4L2 bug-fix
patches (default builds are unmodified upstream). It lacks HTTPS/TLS and software H.264/HEVC encoders. Its
sources and exact build flags are in the deliverable. On malformed data
ffmpeg may output partial frames; exit code alone is not acceptance.
Use scripts/verify-hashes.py and the recorded frame counts for correctness.

OpenList and Arcane were found stopped at the inherited baseline; their
original containers were restarted without configuration changes. They,
SublinkPro, VoCat and Home Assistant remained running during mixed-load
acceptance. The final HTTP and container checks are recorded separately.

The linux-media findings are recorded in the repository's Discussions and are
not submitted upstream by decision; no email or external publication was made.
The research series was rebased onto mainline v7.3.4 on 2026-11-16 and builds
the module, every ARM DTB and the binding schema check; the static-DT/power-domain
design and hardware validation were still required for upstream acceptance,
which this project no longer pursues. See evidence/mainline-2026-11-16/PORT.md.

Verified release archive: build/onecloud-verified-release.tar.gz, SHA-256
71f7922e9a60cb9c4ae3bebb34181bf95f467a99e824f34d7bfd9daf9e3ff903.
Package source commit bd1cb79ad502d0b8cdcf9667a0ceb0ca0f7876ab.
Independent tools and all scripts are covered by the manifest.

Installed at /opt/meson8b-vcodec/current. The enabled and active
meson8b-vcodec.service leaves /dev/video0 and /dev/video1 available.

Previous installation (2026-11-17 23:27): the GitHub release v0.3.0 runtime
package, built by CI from abee6f6, kernel release 5.15.79-meson, state
/root/venc-deploy/p6/codec-state-v030-release. Module hashes meson-vdec
c60842e8ffc02a1c, meson-venc 2507c7cc69fb1b00, meson8b-runtime
af61f4dea7b68e06; firmware files unchanged. It replaced the locally built
0a0542b package, whose driver code is byte-identical (only the embedded
build path differs), and passed the regression set again; see
evidence/p6-2026-11-17/v030-deploy.md for both installs, the regression and
the 30-minute soak, and evidence/p6-2026-11-15/v020-deploy.md for the v0.2.0
install that superseded cd4e6b5.

The first 30-minute soak stopped after 191 iterations (0 failures), short of
its window, and the box reset afterwards. The USB disk behind /mnt/sda2 then
stopped enumerating, so Docker, SublinkPro and VoCat are unavailable until it
is serviced; the codec stack itself recovered automatically and passed decode
and encode retests. Log timestamps on this box are unreliable (no RTC, clock
restored at boot), so the evidence records order and counts rather than a
timeline. See evidence/p6-2026-11-15/outage-2026-11-15.md.

The 30-minute soak was then completed on the v0.2.0 package on 2026-11-16:
545 iterations, 0 failures, every HEVC 30/30 and H.264 300/300 frame hash
matching the software reference, no CMA or memory trend, no kernel warnings.
The five business services were inactive throughout because of the disk, so
that run says nothing about their coexistence. See
evidence/p6-2026-11-15/soak-30min-v020.md.

A 2-hour soak on the same package then ran 2205 iterations with 0 failures:
66150 HEVC and 132300 H.264 frames matched the software reference, no CMA or
memory trend, no kernel warnings. See evidence/p6-2026-11-15/soak-2h-v020.md.
The five services were still inactive for the same disk reason, so that run
says nothing about their coexistence.

An overnight run on the same package then went 7620 iterations over 6 h 54 min
and ended on one failure: at iteration 7620 an H.264 1080p decode returned 299
of 300 frames, with temperature, CMA and memory normal at the time. 7620 HEVC,
1523 of 1524 H.264 and 7619 encode checks passed, and neither video device
went down. Two follow-up repeats were clean: 200 runs of that decode alone
(200/200 at 300 frames, hashes matching) and 400 rounds under the soak's own
order, an HEVC decode then the H.264 decode then H.264 and JPEG encodes
through the same CMA (400/400 at 300 frames). The short output is therefore
rare — about 1 in 1700 H.264 runs — and its cause is not yet identified. The overnight
window is therefore a bounded regression ending on a failure, not a
zero-failure soak. See evidence/p6-2026-11-16/soak-overnight-v020.md and
evidence/p6-2026-11-16/h264-tail-race.md and
evidence/p6-2026-11-16/h264-shortload.md.

Once the USB disk was back and all five services were running, the same load
ran for 377 iterations over 25 minutes while four HTTP entry points were
probed every 20 s: 71 samples, every one returning 200, Arcane still advancing
its own request log, and no kernel warnings in the window. That closes the
coexistence gap the earlier runs left open; it is a bounded window, not a
long-term coexistence claim. See evidence/p6-2026-11-16/service-coexist.md.

Graceful restart was accepted separately: `systemctl restart meson8b-vcodec`
runs ExecStop (withdraw the overlay, unload three modules) and ExecStart in
one transaction with no reboot in between, and the stack came back in 5 s
with rc=0, both nodes present and a 22-frame decode still correct. See
evidence/p6-2026-11-16/restart-check.md.

On 2026-11-17 the v0.3.0 package (source commit 0a0542b, which only adds the
EOS-write timeout log and tooling) replaced it: 53/53 files verified, same
MPEG-2 reference md5, H.264 300/300, HEVC 30/30, both encode checks and all
four HTTP services passing, then 525 soak iterations over 30 minutes with 0
failures and no kernel warning in the window. The same evening the GitHub
release package replaced that local build and passed the regression set
again.
See evidence/p6-2026-11-17/v030-deploy.md.

The same rerun corrected the MPEG-2 B-frame count: a 150-picture stream
delivers 149 or 150 pictures, 14 of 20 runs full, not always 150. A short
run loses the last picture in decode order, the B picture shown at position
148; every other picture is in place. The EOS write itself was delivered
(the new log stayed silent), and appending a picture header recovers the
picture but sometimes adds a duplicate of the last one, so the drain is
unchanged. See evidence/p6-2026-11-17/m2v-bf-delivery.md.

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
