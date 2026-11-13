# P3 编码验收记录（2026-11-13）

## P3-11 最小可用 encoder — 通过

1080p H.264，V4L2 M2M 接口，ffmpeg 7.0.2 `h264_v4l2m2m` 可调，输出软解可播。
矩阵（软件解码 + 对源 PSNR）：

| 分辨率/格式 | 帧数 | 平均 PSNR |
|---|---|---|
| 1920x1080 nv12 | 30 | 45.7 dB |
| 1920x1080 yuv420p | 30 | 45.68 dB（与 nv12 一致） |
| 1920x1080 nv12 (60fps) | 60 | 45.6 dB |
| 1280x720 nv12 | 30 | 45.0 dB |
| 854x480 nv12 / yuv420p | 20 | 解码 20/20（PSNR 输出缺失，仅记录） |
| 640x360 nv12 | 20 | 43.5 dB |
| 320x240 nv12 | 4 | 43.3 dB |
| 320x240 nv21 | 4 | 42.65 dB |

EOS drain（V4L2_EVENT_EOS + FLAG_LAST）按 v4l2-mem2mem 契约实现；
yuv420p/nv21 经 MPLANE M-variant fourcc 别名路径正确（PSNR 与 nv12 相同）。

## P3-12 编码质量/性能测量 — 通过

`scripts/venc-bench.py`（无值守 nohup + wdt-guard，CPU 取
os.times() 子进程时间，DERP 中继 SSH 不污染）。1080p30 × 150 帧
（testsrc2，各 3 次取中位）：

| 指标 | h264_v4l2m2m | libx264 ultrafast |
|---|---|---|
| CPU 时间（user+sys） | 5.85 s | 23.89 s |
| 墙钟 | 5.28 s | 8.44 s |
| 编码帧率 | 28.4 fps（接近实时） | 19.5 fps |
| 码流大小 | 8.03 MB | 9.70 MB |
| 平均 PSNR vs 源 | 45.6 dB | 52.1 dB |

- **CPU 降低 75%**（5.85 vs 23.89 CPU-s），且墙钟下硬件路径更快。
- 硬件 QP 固定 26（vendor fixed-quant 模型，码率控制为 no-op），
  故 PSNR 低于 ultrafast x264 约 6.5 dB、码流小 17% —— 属预期。
- 数据见 evidence/enc-bench.json、evidence/enc-bench.log。

## 测量方法学备注（重要）

裸 H.264 elementary stream 无时间戳，ffmpeg 解码默认按 25fps 解释；
对 30fps 源直接 PSNR 会被 framesync 错配读到 ~23 dB（假象）。
正确做法：比较前 `[0:v]setpts=N/(30*TB)` 重定时 → 真实 45.6 dB。
逐帧 `select=eq(n,N)` 验证全部 150 帧均 ~45-48 dB，无长序列衰减。

## 稳定性

基准全程 gateway 可达、零内核告警；模块卸载/重载干净。
