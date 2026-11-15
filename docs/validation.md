# 验证范围与证据

下列结果来自 **2026-11-15 及之前的 OneCloud 实机记录**，原内核
`5.15.79-meson`，64 MiB CMA。2026-11-14 完整验收对应 `bd1cb79`；
在役机 2026-11-15 起安装 `c20a473`（v0.2.0 包，含 P1–P5 编码扩展、
NEON 与 MPEG/VC-1 drain 修复）。后续文档、打包和 CI 变更不自动继承
新二进制的硬件验收。[部署快照](rollout-status.md)包含包哈希和服务状态。

| 项目 | 已验证结果 | 证据 |
| --- | --- | --- |
| H.264 解码 | 1080p 300 帧逐帧一致；后续独立回归 60/60 一致 | [Decode](../evidence/ACCEPTANCE-2026-11-13.md) |
| HEVC Main 8-bit | 320×240 10/10、1080p B 帧 30/30、854×480 crop 10/10 哈希一致；重复会话和恢复 | [HEVC](../evidence/HEVC-ACCEPTANCE-2026-11-14.md) |
| H.264 编码 | 三种原始格式、多尺寸、CBR、强制关键帧；1080p30 编码 CPU 对比软编降低约 75% | [Encode](../evidence/ENCODE-ACCEPTANCE-2026-11-13.md) |
| 编码控件与码率控制（候选模块） | QPPICTURE 修复后变 QP 码流可解码；I/P QP、QP 范围、GOP 0、码率目标误差 motion/scene ±1.8% 内 | [Controls](../evidence/ENCODER-CONTROLS-2026-11-15.md) |
| 编码输入、DMA-BUF、分片（候选模块） | 9 种新增输入格式逐字节/PSNR 检查；非整宏块宽度原尺寸解码；连续 DMA-BUF 直读；整行多分片 | [Input](../evidence/ENCODER-INPUT-SLICES-2026-11-15.md) |
| JPEG 编码 | 八组输入矩阵；3×300 张 1080p 图像软解；质量、时间戳、互斥及 EOS | [JPEG](../evidence/P6-JPEG-ENCODE-ACCEPTANCE-2026-11-14.md) / [Long run](../evidence/BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| H.264 并发转码 | 1080p B 帧输入十轮 60 帧完整；PSNR 最低 45.03 dB、平均 45.82 dB | [Concurrent B frames](../evidence/BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| 解码输出转换（候选模块） | NEON 后 1080p 每帧 6.8–8.3 ms（标量 15.2–16.1 ms）；左偏移窗口 7.3–8.5 ms；1360x768/1600x900 canvas 修复后哈希一致 | [Decoder convert](../evidence/P5-DECODER-CONVERT-2026-11-15.md) |
| MPEG-1/2 drain 帧尾 | 90 帧参考流由 88 恢复为 90（99% 像素与软件参考相同，差异为舍入）；150 帧 B 帧流返回全部 150 帧 | [Drain EOS](../evidence/p6-2026-11-15/drain-eos.md) |
| VC-1 drain 复验 | 两个 FATE 样片唯一图 6/7、14/15，硬解图像与软解逐字节相同，比旧模块多恢复末一幅 | [VC-1 drain](../evidence/p6-2026-11-15/vc1-drain.md) |
| v0.2.0 运行包部署 | 包校验 52 文件通过，安装后服务 active；MPEG-2 90 帧复验、H.264/HEVC 解码、H.264/JPEG 编码回归通过 | [v0.2.0 deploy](../evidence/p6-2026-11-15/v020-deploy.md) |
| 30 分钟持续负载 | 545 轮 0 失败；HEVC 30/30 与 H.264 300/300 逐帧哈希全部匹配；CMA 与可用内存无单调下降，dmesg 无警告 | [Soak v0.2.0](../evidence/p6-2026-11-15/soak-30min-v020.md) |
| 原有服务与回滚 | OpenList、Arcane、Home Assistant、SublinkPro、VoCat 均运行；加载/卸载/固件恢复通过 | [Final state](../evidence/hevc-2026-11-14/verified-live-state.log) / [Autoload](../evidence/hevc-2026-11-14/autoload-cycle.log) |
| 设备枚举 | v4l2-ctl 枚举两个设备、格式和控件 | [Enumeration](../evidence/hevc-2026-11-14/v4l2-enumeration.log) |
| 干净源码构建 | 原配置完整构建、三个外部模块哈希一致、additive 补丁/Kbuild | [Build](../evidence/hevc-2026-11-14/reproduced-codec-build.log) / [Hashes](../evidence/hevc-2026-11-14/reproduced-module-hashes.txt) |

## 限制

- HEVC Main10、tiles、长期参考帧未支持或广告；当前 8-bit 线性路径。
- H.264 encoder 无 B 帧；一个 encoder open，共享 HCODEC 的 JPEG/AVC 互斥。
  `bd1cb79` 的 CBR 码流解码错误、设置 MIN/MAX_QP 会死锁，`cd4e6b5` 已修复。
- 编码码流为 Baseline、无 VUI；码率控制的 P 帧 QP 不超过 32，低于该码率的
  目标不可达。分片随整帧交付，没有逐片低延迟。单帧码流上限 1 MiB。
- RGB24 最宽 1376，三字节 BGR 不支持；运动搜索随 DRAM 时序变化，有运动的
  码流在多次运行间不逐字节一致。
- VC-1 advanced 的唯一图像与软解逐字节相同，RPS 重复显示帧不补发。
  MPEG-1/2 帧尾已修复（EOS 此前从未写入解析器），参考流 90/90；VC-1 同构
  改动在两个 FATE 样片上复验为 6/7、14/15 幅唯一图，仍差末一幅；MPEG-4
  尾两帧仍缺失，起始码方案已排除。不能声称任意流逐帧完整，见
  [VC-1 drain](../evidence/p6-2026-11-15/vc1-drain.md)。
- MJPEG 带厂商全量程色度增益，不能声称与软解 bit-exact。
- 分辨率切换由标准 V4L2 客户端验证 350 帧；FFmpeg 7.0.2 reinit 仍可能
  等待持有的 buffer ref。独立 drain 补丁不宣称修复所有 reinit 行为。
- 900 张 JPEG、十轮并发与 30 分钟持续负载都是有界回归，非无限期生产
  soak；2 小时与过夜验证未做。
- 未替换启动内核、未重启验收自动加载；当前主线端口和公开 RFC 未完成。
- CPU 输出字节序转换与编码输入拷贝存在，非端到端零拷贝；NEON 转换
  自 `cd4e6b5` 起包含在安装包中，当前在役机的 `c20a473` 包同样包含。
- 解码输出按 canvas 对齐尺寸（32 字节行宽、32 行高）补零，`bytesused`
  即该尺寸；左偏移不是 8 字节倍数的裁剪窗口走逐字节路径，约 21–23 ms/1080p。

旧 codec 详细记录：[MPEG-4/CBR](../evidence/RC-MPEG4-ACCEPTANCE-2026-11-13.md)、
[VC-1](../evidence/RC-VC1-ACCEPTANCE-2026-11-13.md)、
[MJPEG](../evidence/P6-MJPEG-ACCEPTANCE-2026-11-14.md)。

## 主机 CI 与实机检查

`make test` 复现微码，运行 Python 测试及 C ASan/UBSan。CI 还交叉构建原
基线、检查补丁同步和包校验，但不连接生产设备、不宣称硬件测试通过。

实机验证时记录 source commit、模块哈希、样片 metadata、完整帧数和图像
内容。保留软解参考，对照输出帧哈希：

```sh
python3 scripts/verify-hashes.py software.md5 hardware.md5
```

缺失/损坏固件应有界失败，恢复后新会话应正常。损坏流可能输出部分帧且
FFmpeg 返回 0；验收必须检查内容。完整测试后核查服务、内存回收、dmesg
增量和卸载回滚。原始失败日志保留，最终记录及历史区别见
[证据索引](../evidence/README.md)。
