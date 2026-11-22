# 修正后的开发范围 / Corrected development scope

官方原版 FFmpeg 是默认兼容目标；不维护自定义功能扩展。默认源码构建不
应用补丁，仅明确 bug 可选择最小修复。以下把实现、实测和仍未满足的目标
分别列明，不能把编译或短期回归当作全部功能完成。

| Priority | Work | Current result and remaining boundary |
| --- | --- | --- |
| 1 | Decoder integrity and recovery | Immediate parser completion ordering has a deterministic host reproducer; the candidate passed 15 complete 300-picture H.264 hash runs with official FFmpeg and bounded rawvideo output threads. Custom drains now own LAST completion, and parser terminator failures abort rather than schedule success. Separate marker experiments were rejected; rare H.264 short output and older-codec tails are not declared solved. |
| 2 | 1080p performance | Write-combined staging and bulk row copies reduce one 8 s 1080p case from 29.527 s to 10.098 s. Still below the original 1.3× realtime target; scaling is expensive on Cortex-A5. |
| 3 | Rate control and quality | Standard official bitrate/QP/GOP works. Keep the silicon-tested P-QP ceiling rather than trading more bits and worse quality at QP >32. Nine 90-picture static/motion/scene PSNR cases and a 30-minute, 11880-picture official-FFmpeg encoder regression passed; low targets still saturate. No strict CBR or arbitrary-target promise. |
| 4 | Standard controls | Baseline/Level 4.0 controls match observed SPS; reject unsupported profiles/levels. Standard contiguous NV12 accepts official software-scaled input. |
| 5 | OpenList player | Official FFmpeg arguments, measured preflight, stable geometry, redacted read-only status and longer bounded HLS preparation waits are implemented and tested separately. |
| 6 | Existing formats | H.264/HEVC 8-bit, MPEG-1/2/4, VC-1 and MJPEG remain within their documented stream/firmware boundaries; unsupported formats are not advertised. |
| 7 | New kernels/boards | All 16 research patches apply and pass checkpatch on pinned 7.3.4; vmlinux and 944 modules build without warnings. Its per-CAPTURE shadow predates the runtime frame pool; another board/kernel still requires the pool port and actual hardware acceptance. |

No reboot, boot-kernel replacement, CMA change or production application switch
is part of this development pass. Tests use temporary idle module replacements
and disposable app data, restoring originals on exit. The current delivery uses
standard official FFmpeg; the older private-option image is superseded.

尚未达到的吞吐目标、稀有短输出、旧编码尾帧与跨板验证不能用“已全部完成”
概括。成功实现以模块/镜像哈希、帧数/内容、CI 和设备结果对应；失败实验
保留，不更名为成功。HEVC 片上编码继续排除，没有硬件依据就不增加格式。

## Application delivery

- Backend implementation: `a137737a4c219fc9c5446419beaf3d3f42f698f9`;
  latest documentation: `6c77764f6e940c218f08576ae67782376467d54c`.
- Frontend implementation: `2926dbd51ff997f37c9356bafffd5fe373d1b07f`;
  latest CI: `26238c334992e6d97abe0123125354b3e9056e8e`.
- Image: `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a137737`,
  preserving the pinned official base's FFmpeg executable.
- Local and worker HLS acceptance and known frontend type-check baseline:
  [application evidence](https://github.com/YoungSx/OpenList/blob/6c77764f6e940c218f08576ae67782376467d54c/internal/transcode/v4l2m2m/OFFICIAL-FFMPEG.md).
- Frontend unit tests and production build passed. Full type-check retains
  pre-existing Monaco/archive/storage errors. Browser playback acceptance
  remains separate from HTTP HLS and seek checks.

The portable image and support/source archives are prepared separately from
installation. The production container is retained; device access remains an
explicit Docker mapping even when FFmpeg supports the codecs natively.

## Bounded hardware evidence

The [2026-11-22 report](../evidence/official-ffmpeg-2026-11-22/README.md)
links encoder/quality results, original-module comparisons and kernel builds.
Encoder soak used the installed decoder; the new parser decoder's separate
H.264 acceptance is 15 × 300 complete picture hashes. Do not describe this
as a 30-minute soak of the new decoder. Default multithreaded checksum output
can hold every capture buffer; standard single-thread filter/rawvideo options
avoid the reproduced truncation without changing FFmpeg.

## Drain follow-up

[Drain ownership and failure propagation](../evidence/drain-2026-11-22/README.md)
records the next decoder fix: custom drains finish LAST themselves, and a
failed parser terminator fails the queues. The final candidate passed 900
H.264 and 90 HEVC picture hashes and legacy completion with CAPTURE 1/8,
then restored the original module. MPEG tail counts/content still expose
missing pictures. Marker reordering produced wrong pictures and was removed.
