# v0.4.0 运行包热更新、回归与 30 分钟持续负载

2026-11-20 在役机实测。从未安装过帧池版本的在役机出发，按
[安装指南](../../docs/installation.md)的升级路径换成 GitHub Release
`v0.4.0` 运行包，随后跑回归、30 分钟持续负载与五项服务并存探测。
原始日志见 [v040-regress.log](v040-regress.log)、
[soak-30min-v040.log](soak-30min-v040.log)（已去掉每帧编码输出，保留每轮
PASS 行与指标行）、[service-coexist.log](service-coexist.log)。

## 包与校验

| 项目 | 值 |
| --- | --- |
| 源码 commit | `89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f` |
| 包 sha256 | `32f0e975e800a67d8b96e69c592a79e176d7aa297c1e29454431008195bf77aa` |
| 包 md5 | `5a0b85ae055e30614069417897043658` |
| 包内 `SHA256SUMS` | sha256 `f7397f00d09a0e52985ccce5af099966d6ddced05d1853eec2176d6816945608`，54 个文件全部通过 |
| meson-vdec.ko md5 | `15d5c77d25225e8ba7a42519ee5da152` |
| meson-venc.ko md5 | `2507c7cc69fb1b00943fb21e2c615800` |
| meson8b-runtime.ko md5 | `af61f4dea7b68e06015d78d5dc52ab05` |

包由 CI 从打标签的提交 `89dd558` 构建。与 v0.3.0 的 Release 包对比，
只有 `meson-vdec.ko` 变化（`c60842e8…` → `15d5c77d…`）；`meson-venc.ko`
和 `meson8b-runtime.ko` 逐字节相同，`ffmpeg` 与设备树未变。

包按 300 KiB 分块经 DERP 传输，逐块 md5 与本机清单一致后重组，重组后
包 sha256 与本机相同。机上 `sha256sum -c SHA256SUMS` 54 个文件全部 OK，
`runtime-device.py check` 报 “Package checksums, kernel, board and overlay
providers match.”。

## 安装

沿用既有升级路径：先 `remove-autoload.sh` 完整移除 v0.3.0 安装并恢复
原固件，再以新状态目录 `codec-state-v040-install` 安装 v0.4.0。

| 时间（UTC） | 事件 |
| --- | --- |
| 07:30:03 | 移除 v0.3.0 安装（unit 停止，模块按顺序卸载，原固件恢复） |
| 07:30:12 | `/opt/meson8b-vcodec` 已不存在，`/dev/video*` 已消失 |
| 07:30:24 | v0.4.0 安装完成，`install_rc=0` |

编解码设备中断 21 s。安装前后五项业务服务均在运行，容器未重启。

| 项目 | 结果 |
| --- | --- |
| `meson8b-vcodec.service` | `active`、`enabled` |
| 设备节点 | `/dev/video0`、`/dev/video1` |
| 运行中模块指纹 | `15d5c77d…`、`2507c7cc…`、`af61f4de…`（与包内一致） |
| `meson_vdec` coresize | 98304（帧池版本；v0.3.0 为 90112） |
| 状态备份 | `/root/venc-deploy/p6/codec-state-v040-install`，`status=loaded`，9 个固件/Overlay 文件的 original 与 installed 哈希全部相同 |
| 旧安装 | v0.3.0 状态 `codec-state-v030-release` 保留为 `status=rolled_back`，Release 包仍在 `/root/venc-deploy/release/` |

回滚：`bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh`，再以
v0.3.0 包和一个新的状态目录重装。原内核、原设备树与 `/boot` 未改动。

## 回归

| 项目 | 结果 |
| --- | --- |
| MPEG-2 90 帧 | `EOS frames=98 resolutions=1 pictures=90`，tight md5 `f555b2b0373a7a052455c08d24c59391`，与 v0.2.0/v0.3.0 记录值相同 |
| H.264 1080p 解码 | `PASS: 300/300 frame hashes match` |
| HEVC 1080p 解码 | `PASS: 30/30 frame hashes match` |
| H.264 10 帧编码 | `PASS: 10 frames, ownership, busy format, timestamps, EOS`，每帧字节数与 v0.3.0 相同 |
| JPEG 3 帧编码 | `PASS: 3 frames, ownership, busy format, timestamps, EOS` |
| HTTP 服务 | OpenList 5245 / SublinkPro 8000 / VoCat 7575 / Home Assistant 8123 全部 200 |
| 容器 | arcane-agent、homeassistant、iphone-agent、iptv-api、openlist 运行中 |
| 回归窗口内核消息 | `warn/oops/error` 0 条 |

两个编码用例走 `/dev/video1`、质量 90、10/3 帧，与 v0.3.0 回归相同；
回归脚本本身见 [v040-regress.sh](v040-regress.sh)。

## 30 分钟持续负载

时间 15:39:42 至 16:09:44（本机时间），`SOAK_DONE iter=441 FAILS=0
elapsed=1802s`。每轮 HEVC 1080p 30 帧硬解对软件参考逐帧比较，每 5 轮追加
H.264 1080p 300 帧，每轮 H.264 10 帧与 JPEG 3 帧硬件编码。

| 项目 | 结果 |
| --- | --- |
| 轮次 | 441，全部完成，平均 4.09 s/轮 |
| HEVC 逐帧比较 | 441 次 `PASS: 30/30 frame hashes match` |
| H.264 逐帧比较 | 88 次 `PASS: 300/300 frame hashes match` |
| H.264 编码 | 441 次 `PASS: 10 frames, …` |
| JPEG 编码 | 441 次 `PASS: 3 frames, …` |
| 失败 | 0 |
| 编解码设备 | 每轮 `/dev/video0`、`/dev/video1` 均在 |
| 窗口内内核警告 | 0 |

| 指标 | min | p50 | p95 | max | 前 50 轮均值 | 后 50 轮均值 |
| --- | --- | --- | --- | --- | --- | --- |
| SoC 温度 (°C) | 50 | 58 | 61 | 64 | 55.3 | 58.6 |
| MemFree (MB) | 133 | 189 | 218 | 244 | 179.9 | 193.0 |
| MemAvailable (MB) | 434 | 494 | 519 | 530 | 467.5 | 511.9 |
| CmaTotal (MB) | 64 | 64 | 64 | 64 | — | — |
| CmaFree (MB) | 27 | 39 | 41 | 44 | 38.4 | 38.8 |

CMA 与可用内存在窗口内没有单调下降（CmaFree 前后 50 轮均值 38.4 → 38.8
MB，MemAvailable 467.5 → 511.9 MB），未见句柄或 CMA 泄漏。负载结束后帧池
计数：`pool_slots=0`、`pool_bytes=0`、`pool_waiting_peak=8`、
`converted_frames=40470`、`cropped_frames=0`、`convert_ns=361923844545`
（累计 362 s，含此前回归）。

## 五项服务并存

15:40:40 至 16:09:49，每 15 s 采样一次：
`COEXIST_DONE samples=106 fails=0 soak_rc=0 final: openlist=200
sublink=200 vocat=200 ha=200 arcane_hits=3284`。四项 HTTP 探针全程 200，
Arcane agent 的健康检查日志计数在窗口内持续推进（3197 → 3284），说明其
实际在提供服务而不只是进程存在。

## 覆盖范围

- 本次负载只覆盖解码（HEVC 1080p 每轮、H.264 1080p 每 5 轮）与编码
  （H.264、JPEG 320×240）；MPEG-1/2、MPEG-4、VC-1、MJPEG 解码与分辨率
  切换不在这 30 分钟里，其验收见
  [Phase A](../phase-a-2026-11-19/README.md)。
- 帧池模块与 2026-11-19 的 Phase A 候选逐字节相同（`15d5c77d…`），Phase A
  的 CAPTURE 1/2/4/8/20/32 矩阵、并发解码编码、分辨率切换 12/12 与 439 轮
  负载结论对本次安装同样成立；本次是在正式 Release 包上重新验证安装、
  回归与负载。
- 负载期间的服务结果是每 15 s 一次的单点探测，不是对业务链路的端到端
  断言。
- 回归首次运行暴露的两个问题都在测试脚本而非驱动：MPEG-2 原先拿硬件
  dump 与软件参考 `sw-m2v.nv12` 做 `cmp`，两条解码路径本就不逐字节相同，
  改为比对记录值 `f555b2b0…`；编码用例原先把 `/dev/video0` 当编码器并传错
  质量参数，改为 `/dev/video1`、质量 90。修正后按上表重跑，两个用例都通过。