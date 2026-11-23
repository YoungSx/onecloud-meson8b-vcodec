# Current development and acceptance status

Official unmodified FFmpeg remains the compatibility target. Current deployed
runtime source is `5fedae9a74d2e6ec03c8fc2f6bf81d011ba82f33`; production OpenList
uses `v4.2.10-official-ffmpeg-a845b34-clean`. New driver deployment was explicitly
authorized. [Current evidence](../evidence/development-2026-11-22/README.md)
records tested module hashes and bounded acceptance. **The whole development
list is not complete.**

| Priority | Implemented and checked | Remaining boundary |
| --- | --- | --- |
| P0 reliability | Wait for the threaded IRQ to finish before declaring inactivity; fail drain timeouts and recycle allocation failures; publish EOS once, after its LAST buffer is dequeued; preserve source-change LAST separately. Order firmware DMA writes and cancel timed-out transfers before freeing staging. | The first outage now has a high-confidence shared-MPLL2 clock explanation; the second remains undetermined. Old H.264 short-output race is addressed by concrete ordering fixes, but bounded tests cannot prove elimination. Production sustained load stays paused. |
| P0 deployment | New modules, matching overlay and autoload package installed; original v0.4.0 installation/firmware/unit/state preserved. Idle checks, checksums, rollback-on-load-failure and service probes used. Two-client QUERYCAP/EBUSY/takeover check passed. Production OpenList was not restarted. | New installed package has service restart/short content checks; a new cold boot has not been requested or tested. Temporary overlay isolation also lacks abnormal-reboot acceptance. |
| P1 playback | Clean image and latest frontend played real HLS in a browser, sought forward into uncached content, backward, and to the end; no MediaError. Cached seeked events hit 12 and 50 exactly; cancel released hardware in 0.242s. | Buffering occurred. This is bounded playback, not a realtime or multi-browser guarantee. |
| P1 performance | Run conversion on the existing pool worker outside the firmware IRQ. A copying slot remains queued and protected while IRQs queue other slots; separate delivery/reconfiguration locking protects lifetime. | Production CPU/memory load causes large timing variation; 1.3× realtime remains unmet. A high-priority queue experiment had no stable gain and was rejected. No higher clock retained. |
| P2 encoder | Add SPS/VUI crop, negotiated color range/known colorimetry and coded-picture period. Reject frame-period changes while streaming. Stock `h264_metadata` can supply explicit color tags after an actual colorspace conversion. 30,30000/1001,24 fps MP4 duration/count/softdecode pass. | Official V4L2 wrappers do not forward every color option; unknown V4L2 colorimetry stays unspecified. Rate control is VBR; low targets may saturate at the validated QP32 ceiling. Strict CBR,HRD and pacing are absent. |
| P2 research | 27 patches port all existing runtime decode formats to Linux 7.3.4: independent pool, IRQ/EOS ordering, VDEC1 legacy formats and HEVC Main 8. Shared DOS uses syscon; genpd owns VDEC1/HEVC AO bridge power; fixed clock parents avoid MPLL retuning. Full kernel/modules,ARM DTBs,bindings and ODROID checks pass locally. | No second board or serial test device exists. Research kernel is not booted on production; cross-board/static-DT runtime acceptance remains open. |
| P3 formats | H.264/HEVC direct-client CAPTURE 1/2/4/20/32,full-range MJPEG and six early STREAMOFF/reopen cases passed with new conversion/EOS paths. Firmware and budget errors have bounded recovery. H.264 stream changes pause recycling and clear old slot mailboxes; joined130-picture content checks pass at CAPTURE1/4. | MPEG/VC-1 tails and particular MPEG-4 B-picture content remain incomplete. Official FFmpeg checksum output with CAPTURE2 has a separate retention boundary. No guessed/replayed-picture drain fix is merged. |

## Production recovery and diagnostics

The [read-only outage investigation](../evidence/outage-investigation-2026-11-22/README.md)
separates the two failures. The 255 MHz experiment's old unrestricted rate path
can select and retune Ethernet's shared MPLL2; extracted Linux 5.15.79 CCF
selection reproduces the mechanism. LAN/Tailnet failed shortly after the
experiment started, while host processes continued for over an hour. Module
restoration and loopback HTTP checks did not establish Ethernet recovery.
This is a high-confidence explanation, without a failure-time clock register
snapshot. Current fixed-parent protection avoids that path.

The later 182 MHz mixed-load run used protected fixed parents. Its journal is
readable only by exact archived file/boot ID and is corrupt: its index records
7271 s but the tail after 6924 s is all zero. The initiating fault remains unknown;
this is not a passing soak or a reason to transfer the first cause to the
second outage.

After the second reboot, the temporary overlay had persisted at the canonical
firmware path. Its checksum mismatch correctly blocked original autoload;
original overlay/modules/services were restored before further work. Subsequent
explicitly authorized upgrades preserved that original installation, boot
kernel/DT/CMA and business container configuration.

Some previous-boot journal entries were recovered from archived files, but the
second outage still lacks its final 347.75 s and a crash stack. Ramoops has no
configured memory reservation; lockup/hung-task detectors are disabled. The
second cause remains undetermined.
Development uses bounded checks and a transient, rotating, per-line-fsynced
kernel log on persistent storage. It improves evidence retention but cannot
capture every CPU/network hang. Raw production logs stay private.

2026-11-23: the earlier temporary kernel reader is no longer running. A
[bounded passive observer](../evidence/passive-observer-2026-11-23/README.md)
now records network/IRQ/memory/storage counters during normal activity; its short
deployment check passes. A separate six-hour observation has started, not finished.
The data SSD had filled and Docker could not restart Arcane agent. Verified
duplicate archives reclaimed about 1.53 GiB, ordinary available space recovers
to about 426 MiB, and the stopped agent is started successfully. Capacity remains
tight. Logs use the system partition. This new capacity finding does not establish
the earlier outage's cause; stacks/serial and independent LAN observation are
still missing.

## Further extensions

GE2D/full zero-copy and per-slice early delivery are research gates. Existing
register headers do contain source/destination endianness controls; the old
claim that GE2D has no such controls was too broad. Meson8b register,canvas,
clock,power and actual conversion/scale behavior still need implementation and
hardware validation before integration. Current capture copies remain correct
and explicit. Multiple encoder sessions,HEVC hardware encoding,Main10/tiles,
CABAC/High-profile/B-picture encoding are not advertised.

中文：新驱动已按授权部署，与新版 OpenList 配套使用；本轮补齐了 IRQ/EOS、
固件 DMA 防护、异步转换和编码 VUI，并完成最新版浏览器的有界播放与跳转。
研究线已移植现有解码格式，构建和设备树检查通过。首轮失联高置信度归因于旧时钟路径，第二轮根因、实时吞吐、
旧格式完整性、异常重启和跨板验收仍开放，不能宣称全部完成。
