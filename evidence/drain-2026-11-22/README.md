# Decoder drain ownership and failure propagation, 2026-11-22

OneCloud S805, Linux 5.15.79-meson and 64 MiB CMA. All tests use temporary
idle decoder module swaps with restoration, preserving the production
container, boot kernel, DT and CMA. Official unmodified FFmpeg is the target.

## Changes retained

- Codecs with a custom drain callback own their LAST/EOS completion. The
  common picture path must not mark LAST from the pending **input packet**
  count while the firmware may still hold delayed pictures. Source-change
  LAST and the H.264 path remain separate.
- If the EOS parser write fails, MPEG-1/2, MPEG-4 and VC-1 abort both queues
  and do not schedule a successful flush. H.264/HEVC STOP also returns the
  parser error. This describes driver ioctl/queue behavior, not a promise
  that every version of FFmpeg reports every error identically.

The extracted-function host tests cover custom-drain LAST ownership,
source-change flags, failed EOS writes (-ENOMEM, -ETIMEDOUT, -EINTR),
idempotent successful scheduling and STOP error return. Hardware error
injection was not performed; those failure paths are tested on the host.

## Hardware checks

[LAST ownership comparison](last-owner.json) preserves three installed-module
runs per MPEG-2 input and six candidate runs per input. The first candidate
only changes LAST ownership, SHA-256
`f899ce88c94bd8cb12d049ccc4b0c157542c7860460d0897656fe23102830cef`.
It still delivers 149/150 B-frame pictures and 89/90 progressive pictures.
Only complete runs match the software picture order or recorded visible MD5.
This fixes an end-of-stream ownership error; it does **not** solve all missing
firmware tail pictures.

[Final regression](regression.json), decoder SHA-256
`7079a4ca9cb9c1fe3b2849e073a8006853596ebee0017b0ac3fb27857102dc00`:

- Three H.264 runs: 300/300 reference hashes each, 900 total, using the host's
  official 7.0.2. Three HEVC runs: 30/30 hashes each, 90 total, using the
  independently built unmodified upstream tool. Standard single-thread
  filter/rawvideo output options avoid the previously reproduced retention.
- MPEG-2, MPEG-4 and VC-1 all finish at CAPTURE counts 1 and 8. MPEG-2 remains
  89/90 or 149/150; MPEG-4 is 88/90; VC-1 emits six coded pictures for the
  recorded stream. These limited legacy checks establish completion and
  reopening, not full-image acceptance of unsupported stream profiles.
- Original modules are restored, the production container's start time is
  unchanged, four HTTP entry points return 200 and Arcane is running.

## Rejected stream-marker experiment

[Marker reorder](rejected-order.json) moves the MPEG-2 picture-start header
before sequence end. All three nominal 150-picture runs fail picture-order
comparison, and the 90-picture reference yields wrong content or 91 pictures.
The module SHA-256 is
`dd914286d3e9bda72009a39a795c6d3c26afabf8eb94ea974a440e0701c79f9d`.
The experiment is removed; equal counts are not content acceptance.

中文：本轮保留专用 drain 的 LAST 结束权和结束码写入失败传播修复，未改
固件或追加图像来掩盖丢帧。旧格式尾帧仍未完整解决，帧数相等但内容错误的
实验已撤回。故障注入仅为主机函数测试；实机记录保留失败与原模块恢复结果。

## Rejected quiet-window experiment

[Minimum post-EOS quiet interval](rejected-quiet.json) prevents a stale IRQ
from satisfying the quiet test immediately. Its 100 ms minimum did not
eliminate missing pictures: the same B-frame and progressive inputs still
show 149/150 and 89/90 outputs with content failures on the short runs.
The experiment was removed; no new delay is retained. Its module SHA-256 is
`f95cb878c0b9a97258a8b766ff6ef699952b2597d0eb0436ac9644c4745cd9b6`.
