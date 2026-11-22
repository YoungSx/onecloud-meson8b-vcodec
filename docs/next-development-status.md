# P0–P3 development and acceptance status

Official unmodified FFmpeg remains the target. The full P0–P3 goal is **not
complete**. [Dated evidence](../evidence/p0-p3-2026-11-22/README.md) distinguishes
hardware results from host tests and compile checks.

| Priority | Delivered and checked | Remaining work |
| --- | --- | --- |
| P0 | Parser submission mutex, stop timed-out fetches, drain every codec's queued input before STOP, bounded inactivity wait, abort feed stop, YUV420M validation. Final decoder passed six early STREAMOFF/reopen cases and direct H.264/HEVC checks. | Rare H.264 short output and MPEG/VC-1 tail pictures remain. Repeated device outages prevent stability acceptance. |
| P1 driver | Recycle queue wakes on pending slots/IRQs; two 600-picture transcodes per version completed. VDEC1 fixed-parent test preserved fixed PLL and MPLL2 frequencies. | Improvement is small; 1.3× realtime remains unmet. Sustained production tests are paused. |
| P1 OpenList | Previous image passed bounded actual browser playback, uncached forward/backward seeks and cancellation; device release took 0.257 s. Latest frontend passed full lint, 72 tests, build and CI, including HLS replacement cleanup. Clean image contains byte-identical official FFmpeg/ffprobe. | Latest clean image has not passed browser/hardware acceptance. Previous playback buffered and does not prove realtime throughput. |
| P2 quality | Raw nine-case real-content PSNR/rate results retrieved: 90 pictures at 30, 30000/1001, 24 fps. Explicit fps filter aligns the reference. | Low targets can saturate. Strict CBR is not provided; the small set does not justify changing the rate algorithm. |
| P2 research | Independent Meson8b frame pool, current vb2 API, syscon/genpd/runtime PM and GX behavior preserved. 19 patches, full Linux 7.3.4 build and board schema checks passed dedicated CI. | Research Meson8b remains H.264 only. New-kernel/static-DT and other-board hardware acceptance still require a recoverable test device. |
| P3 formats | H.264 120 pictures and HEVC 30 at direct CAPTURE counts 1/2/4/20/32; MJPEG 24 at 1/4/20/32; JPEG 40 images across four resolutions and two qualities; firmware/budget errors reopen correctly. | MPEG-2 produces89/90 and 149/150, MPEG-4 88/90,VC-1 6/7 and 14/15. Official FFmpeg checksum output with CAPTURE 2 still truncates despite direct-client success. |

## Production recovery and stability gate

The device required two local reboots during this development pass. The first
outage followed a temporary 255 MHz experiment. Retrieved results show that the
experiment itself completed and restored modules/services; its connection to
the later outage is unproven. No higher default rate is retained.

The second outage occurred during mixed load at the original 182 MHz. The last
saved record contains 30 iterations over 1169.753 s, 3600 transcode pictures, 9000
H.264 hashes, 900 HEVC hashes, 90 JPEG pictures and 31 service samples. It remains
`running` with no completed/restored result: **the 30-minute soak failed to
complete**. The last sampled temperature was 59.062°C; it does not establish a
thermal cause.

After the second reboot, the temporary overlay still occupied the original
firmware path. The original autoload checksum correctly rejected it, and
OpenList was stopped. On 2026-11-22 the original overlay and installed source
`89dd558` were restored, both codec nodes probed, all four HTTP services returned
200 and Arcane ran. Production retains the original kernel/boot DT/CMA and
OpenList image `v4.2.10-v4l2m2m-29c7842`.

No previous-boot journal or pstore record is available. Retained kernel logs
end about 346 s before the last successful test sample, with no retained
panic/OOM/lockup. This does **not** exclude a driver or system fault. Raw device
logs are retained privately; [sanitized analysis](../evidence/p0-p3-2026-11-22/outage-analysis.json)
records the evidence limits.

Continuous production load tests are paused. Temporary development overlays
now have a separate checked loading path that preserves the installed file,
cleans up on success/failure and restores the original service after load
failure. The new runtime parameter has host tests and matching-kernel build
checks; its hardware/reboot acceptance remains pending. See
[development loading](installation.md#开发-overlay-的临时加载).

## Application artifact

Backend `6c77764f`, frontend `a845b34`, prepared clean image:
`youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`.
The earlier image without the `-clean` suffix contained residual frontend
assets and is superseded. The clean image has not replaced production.

中文：开发和有界验证已有成果，但稳定性、旧格式尾帧、实时吞吐、最新版浏览器
验收和跨板运行仍未完成。两次重启后原系统与业务已恢复，生产持续负载实验
暂停；临时 overlay 恢复缺陷已修，不能把构建或短测通过视为稳定性通过。
