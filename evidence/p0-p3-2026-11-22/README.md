# P0–P3 development evidence,2026-11-22

This pass targets official unmodified FFmpeg. The whole priority list is
**not complete** and the new driver stack is experimental. Production was
restored after two outages; sustained production tests are paused.

## Candidate identities

The final hardware-tested decoder is
`0a3fa95c91ce38a6ce0975a7676d292cad9f01aeb9c8d248bda124c5d91df86b`;
encoder is `e72e3a6697b5f7296520f8b339b7c7e6af9147b299d0639e92c5e42128f4747b`.
Those tests used runtime module
`70f9025465f8bf4c888562b8ff34b127a3e98a9226150d846413c26e469c1c64`
and fixed-parent overlay
`72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e`.

The later runtime-only overlay isolation fix has host/build checks; its
module is `7ca640be512c76d71d2b3606c2a1bf4b0d7f3ca0db12896706ee6649abd6cb32`.
It has not run on the board. Decoder/encoder bytes remain unchanged.
Original installed source is `89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f`.

## Hardware records

- [Final full-range matrix](final-matrix-full-range.json):26 cases. H.264
  4-reference 120-picture and HEVC 30-picture direct-client signatures agree
  at CAPTURE 1/2/4/20/32. MJPEG delivers all 24 pictures at 1/4/20/32 with equal
  visible-picture MD5; against an explicit full-range software reference,
  luma PSNR mean 31.74125, min 31.57. Six early STREAMOFF cases reopen and
  verify 300 H.264 pictures each. Original modules/services were restored.
- [Legacy cases in the final matrix](final-matrix-full-range.json): MPEG-2 89/90 and 149/150, MPEG-4 88/90,
  VC-1 6/7 and 14/15 remain limits. [Earlier tail checks](decoder-tail.json)
  preserve intermediate results; no complete tail fix is claimed.
- [Fixed-parent test](fixed-parent-live.json): H.264 300/300, HEVC 30/30 and
  H.264 4refs 120/120 pass hashes. Fixed PLL 2550000000 Hz and MPLL2 499996410 Hz
  are unchanged before/after. VDEC1 stays at 182142857 Hz. Original overlay
  and all five services restored.
- [JPEG matrix](jpeg-matrix.json): 40 images, 320×240, 854×480, 1280×720,
  1920×1080 at quality 40/90; independent decoding and counts pass.
- [Error recovery](encoder-errors.json): missing/truncated JPEG firmware
  causes EIO; original firmware reopens for 3 pictures. Direct V4L2 CPB
  budget violation fails queues; a new H.264 session encodes 30 pictures.
  Firmware and original services were restored. Parser error injection is
  covered by host tests.
- [Performance](recycle-performance.json): two 600-picture/20 s transcodes
  per version. Baseline 26.058/23.808 s, candidate 23.025/23.487 s. Both decode
  300 reference pictures. This is an intermediate recycle module and a
  small improvement, not 1.3× realtime acceptance.
- [Raw quality](real-quality-raw.json): nine 90-picture real-content samples
  at 30, 30000/1001, 24 fps, targets 800k/1.6M/3M. Explicit fps filtering aligns
  reference pictures. At 30 fps, actual rates 819741/1586776/2694539 bps and
  luma PSNR 44.742/51.723/57.098. At 24 fps, 1026172/1827012/2815232 bps and
  PSNR 45.121/51.312/58.823. Saturation remains possible; this does not
  establish strict CBR. [Transcribed observations](real-quality-observed.json)
  are retained separately from retrieved raw results.
- [Browser observations](browser-observed.json): previous image a137737,
  frontend 2926dbd; bounded playback, seeks, cancel, device release 0.257 s,
  pool zero. Buffering occurred. Frontend a845b34 has lint, 72 tests, build
  and CI coverage; latest clean image has no new browser acceptance.
- [Official FFmpeg CAPTURE 2](ffmpeg-small-count.json): checksum output
  truncates even with a copy filter. CAPTURE 1 is rejected by FFmpeg's
  option bounds before opening the device. Direct-client small-count
  success does not prove every FFmpeg command works.

The real-content source is the Blender Big Buck Bunny trailer, CC BY 3.0:
https://peach.blender.org/ and
https://download.blender.org/peach/trailer/trailer_1080p.ogg.
Trailer SHA256 is
`6614fbdec56804eea700f1eb8cae14a2729f01ca25d848e800f7b55d775ab646`.
The [rejected MJPEG reference](rejected-mjpeg-reference.json) used automatic
limited-range conversion and failed its threshold. It is retained as a
failed reference experiment; the final result uses full-range YUV without
lowering the acceptance threshold.

## Two outages and recovery

[255 MHz observations](clock255-observed.json) show two600-picture transcodes
in 23.610/23.961 s, 300 matching decode hashes and completed restoration. The
first later connectivity failure cannot be attributed conclusively to that
clock experiment. No higher default is retained.

[Mixed-load record](mixed-load-interrupted.json) remains `running`: 30 rounds,
1169.753 s, 3600 transcode pictures, 9000 H.264 hashes, 900 HEVC hashes, 90 JPEG
pictures, 31 service samples. Last temperature 59.062°C. WanOS went offline
at the original 182 MHz and required a second local reboot. There is no
completion/finally-restoration receipt. **This is an interrupted/failing
stability test, not a completed 30-minute soak.**

[Second recovery](second-reboot-recovery.json) verifies original kernel,
source 89dd558,original overlay SHA256
`a5201a696e369f62aecb5d220feab313ca8b83d062d7ba03aaf66a8cb9f9d95d`,
two codec nodes, idle pool, four HTTP 200 services and running Arcane.
OpenList's original container was restarted without changing its image or
configuration. Boot kernel/DT/CMA and installed package were preserved;
the temporary firmware overlay had to be restored explicitly.

[Outage analysis](outage-analysis.json) records absent previous-boot journal
and pstore, and a 346 s gap between retained kernel logs and the final successful
sample. No retained panic/OOM/lockup does not exclude a codec or system fault.
Raw device logs are private; published records omit network addresses,
credentials and unrelated production logs.

Temporary tests must use the [isolated checked overlay path](../../docs/installation.md#开发-overlay-的临时加载)
instead of overwriting the installed overlay. Historic `run-*.py` files are
archived reproductions,not approved scripts for another production soak.
Host success/failure cleanup and 5.15.79 cross-build cover the new path;
board/reboot acceptance is pending.

## Research checks

[Research build](research-build.json), [series](research-series.log) and
[module](research-module.log), together with the CI binding job below, are
compile/schema checks. The 19-patch
Linux 7.3.4 series preserves DOS syscon,genpd/runtime PM, GX behavior and GXLX,
and uses independent Meson8b frame slots. Meson8b research formats remain
H.264 only. [Research CI](https://github.com/YoungSx/onecloud-meson8b-vcodec/actions/runs/45008497420)
passed at 80f589f; it does not prove new-kernel or cross-board hardware support.

中文：原模块和五项业务已恢复。最新解码器通过有界帧数、图像和关闭恢复测试，
但混合负载失联，稳定性未通过。持续生产负载实验暂停；旧格式尾帧、实时性能、
最新镜像浏览器与跨板验收仍开放。临时 overlay 隔离修复只通过主机/构建检查。
