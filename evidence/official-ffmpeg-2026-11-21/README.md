# 官方 FFmpeg 适配与性能 / Official FFmpeg compatibility

The candidate adds standard contiguous NV12 memory layout, verified Baseline /
Level 4.0 controls, and write-combined input staging. It requires no FFmpeg
private option. Hardware tests temporarily replace only the idle encoder and
restore the installed module in finally. Boot files and production application
configuration are unchanged.

新编码模块支持连续 NV12 与原 NV12M 布局，声明 Baseline/Level 4.0；输入
staging 使用写合并和全平面复制快路径。测试仅临时替换空闲 encoder，
不替换生产 FFmpeg、启动内核或设备树；每次恢复原模块。

## 实测 / Results

[原版控件跟踪](stock-controls.json)使用既有 7.0.2-static：目标码率、QP、GOP
传入驱动，三次 60/60；maxrate/bufsize 没有产生 peak/CPB ioctl。
[连续 NV12 验收](official-nv12.json)使用同一个既有工具，覆盖官方软缩放。

| Case | Output / Frames | Wall time |
| --- | --- | --- |
| 1080p, 4M target, no scaling | 240/240, Baseline, Level 4.0 | 10.098 s for 8 media seconds |
| 720p, 2M target, software scale | 120/120, 1280×720 | 14.076 s for 4 media seconds |
| 480p, 800k target, software scale | 120/120, 854×480 | 15.609 s for 4 media seconds |
| Direct NV12M/YUV420M/YUYV/XRGB | 30/30 each | Bounded regression |

Before write-combining, the same 8-second 1080p command took 29.527 s; the
intermediate staging-only candidate took 10.039 s. This is one sample pair,
not a throughput guarantee; it remains slower than real time. Software scaling
is now the main cost in the smaller-output examples. Successful H.264 output
was software decoded and counted. No new PSNR or all-content quality promise
is made. The matching host/sanitizer result is [recorded](host-tests.log).

写合并前同一 8 秒命令为 29.527 s，staging 中间版本为 10.039 s。单组对照
不能当作所有视频的吞吐保证；当前仍未达实时，较小输出主要受软件缩放
影响。成功输出均软件解码并计数；未建立新的完整 PSNR 矩阵。

## 拒绝的 drain 实验 / Rejected drain experiments

The historical client printed 157–158 capture completions after an EOS/LAST
experiment, and another input/quiet-handling experiment failed H.264 checks.
Both were removed and no drain fix was accepted.

2026-11-22 correction: that client's `frames=` included empty completions,
so 157–158 cannot establish duplicate **pictures**. Default threaded checksum
output also reproduced truncation with the original module. These experiments
remain unvalidated; they are not retrospectively declared successful. New
checks count only nonempty pictures and compare content; see the
[corrected regression](../official-ffmpeg-2026-11-22/README.md).

2026-11-22 复核修正：旧客户端报告的 157–158 次出队包含空载荷，不能据此
确认重复图像；默认多线程哈希输出的短输出也在原模块上复现。实验仍未
通过充分内容验证，继续保留撤回状态，不把旧计数当作新修复的依据。
