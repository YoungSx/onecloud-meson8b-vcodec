# v0.3.0 运行包部署、回归与 30 分钟持续负载

2026-11-17 在役机实测。部署本仓库 head `0a0542b` 构建的运行包，回归
MPEG-2 帧尾、H.264/HEVC 解码与 H.264/JPEG 编码，然后跑满 30 分钟持续
负载。原始日志见 [v030-regress.log](v030-regress.log) 和
[soak-30min-v030.log](soak-30min-v030.log)（soak 已去掉每帧编码输出，
保留每轮 PASS 行与指标行）。

## 包与安装

| 项目 | 值 |
| --- | --- |
| 源码 commit | `0a0542b4b6d36e02e639c4f39d673a54c170701a` |
| 包 sha256 | `eef6bf04ac2c0d1acaf7d881d63544f51fd7508758002ff2f8f96f034d4c1861` |
| 包 md5 | `9af91fe4753ef8d947e57dd18c65e4bf` |
| 包内 `SHA256SUMS` | sha256 `493e705c8da4910aff47f32ab5bb73cd3c04f414b5715e0c41ca1e7b2d071039`，53 个文件全部通过 |
| meson-vdec.ko md5 | `c60842e8ffc02a1c9b208a9463adad4b` |
| meson-venc.ko md5 | `2507c7cc69fb1b00943fb21e2c615800` |
| meson8b-runtime.ko md5 | `75c8e3c2baf7290ce5c03bcdbd62dfe0` |

包按 400 KiB 分块经 DERP 传输，逐块 md5 与本机清单一致后重组，重组后
包 sha256 与本机相同。安装沿用既有升级路径：移除旧安装，再以新状态
目录 `codec-state-v030-install` 安装；14:37:04 停 unit，14:37:34 安装后
重新启动，`meson8b-vcodec.service` 为 `active`、`enabled`，
`/dev/video0`、`/dev/video1` 均在。状态备份中的固件文件 original 与
installed 哈希全部相同，运行时模块加载状态为 `loaded`。回滚：
`bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh`。

## MPEG-2 帧尾

90 帧 `regress.m2v`（320×240）：`EOS frames=98 resolutions=1 pictures=90`
（`EOS frames` 含零负载出队，不是图像数）。`tight.py` 去除 canvas 行填充
后 md5 为 `f555b2b0373a7a052455c08d24c59391`，与 v0.2.0 记录值相同，
帧尾修复未回退。

## 回归

| 项目 | 结果 |
| --- | --- |
| H.264 1080p 解码 | `PASS: 300/300 frame hashes match` |
| HEVC 1080p 解码 | `PASS: 30/30 frame hashes match` |
| H.264 10 帧编码 | `PASS: 10 frames, ownership, busy format, timestamps, EOS` |
| JPEG 3 帧编码 | `PASS: 3 frames, ownership, busy format, timestamps, EOS` |
| HTTP 服务 | OpenList 5245 / SublinkPro 8000 / VoCat 7575 / Home Assistant 8123 全部 200 |
| 容器 | openlist、arcane-agent、iptv-api、homeassistant 运行中 |

内核日志中 `warn/oops/error` 共 1 条，是 `esparser: input parsing error`
（uptime 76916 s，约 17:29），出现在给 B 帧流追加帧尾的 A/B 实验期间
（见 [m2v-bf-delivery.md](m2v-bf-delivery.md)），不在回归和负载窗口内。
另有 2 条 `esparser: EOS write was interrupted`（uptime 73972/73983 s），
来自被信号终止的 `res-switch-test` 客户端，同样不在回归和负载窗口内；
这两条是该提交新增的可观测性日志首次生效，30 分钟负载和回归窗口内均为
0 条。

## 30 分钟持续负载

时间 15:09:26 至 15:39:35，`SOAK_DONE iter=525 FAILS=0 elapsed=1809s`。
每轮 HEVC 1080p 30 帧硬解对软件参考逐帧比较，每 5 轮追加 H.264 1080p
300 帧，每轮 H.264 10 帧与 JPEG 3 帧硬件编码。

| 项目 | 结果 |
| --- | --- |
| 轮次 | 525，全部完成，平均 3.45 s/轮 |
| HEVC 逐帧比较 | 525 次 `PASS: 30/30 frame hashes match` |
| H.264 逐帧比较 | 105 次 `PASS: 300/300 frame hashes match` |
| H.264 编码 | 525 次 `PASS: 10 frames, …` |
| JPEG 编码 | 525 次 `PASS: 3 frames, …` |
| 失败 | 0 |
| 编解码设备 | 每轮 `/dev/video0`、`/dev/video1` 均在 |
| 窗口内内核警告 | 0 |

| 指标 | min | p50 | p95 | max | 前 50 轮均值 | 后 50 轮均值 |
| --- | --- | --- | --- | --- | --- | --- |
| SoC 温度 (°C) | 40 | 53 | 55 | 62 | 49.2 | 55.7 |
| MemFree (MB) | 124 | 175 | 206 | 220 | 154.1 | 171.7 |
| MemAvailable (MB) | 381 | 418 | 436 | 454 | 389.7 | 426.0 |
| CmaTotal (MB) | 64 | 64 | 64 | 64 | — | — |
| CmaFree (MB) | 34 | 44 | 45 | 45 | 42.3 | 42.7 |

CMA 与可用内存在窗口内没有单调下降（CmaFree 前后 50 轮均值 42.3 →
42.7 MB，MemAvailable 389.7 → 426.0 MB），未见句柄或 CMA 泄漏。温度在
40–62 °C 区间随负载波动，落在持续负载的高位但没有持续爬升。

## 覆盖范围

- 本记录覆盖编解码路径与 HTTP 服务的安装后状态；五项业务服务在负载
  窗口内始终运行，但 soak 本身不探测 HTTP，服务结果是回归末尾的单次
  采样，不是负载期间的连续性断言。
- MPEG-2 B 帧流的交付帧数另见 [m2v-bf-delivery.md](m2v-bf-delivery.md)：
  该流不是每次都返回 150 帧。
- MPEG-4 尾帧限制与 VC-1 结论不变，见 [drain-eos.md](../p6-2026-11-15/drain-eos.md)。