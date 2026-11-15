# 30 分钟持续负载（中断于 191 轮）

> 2026-11-16 已在 v0.2.0 运行包上跑满 30 分钟（545 轮，0 失败），
> 见 [soak-30min-v020.md](soak-30min-v020.md)。本文件保留被复位截断的
> 那次记录作为历史。

日志记录区间 `2026-11-15T22:40:52+08:00` 至 `22:54:18+08:00`，共 191 轮，
`FAILS=0`。原始日志见 [soak-30min.log](soak-30min.log)。本轮**未跑满**
30 分钟：脚本在最后一轮后不再输出、进程消失，随后系统复位，复位后 USB
磁盘未能重新枚举，故不能作为完整的 30 分钟验收。

时间说明：相邻轮次间隔中位数 3 s、p90 11 s、最大 12 s、无回跳，191 轮
合计约 13.4 分钟。该机无 RTC 且启动时时钟会跳变，日志首尾墙钟差值（806 s）
只作轮次节奏的旁证，不作为精确时长。

## 每轮内容

- HEVC 1080p 硬解，输出 NV12 framemd5 与 P5 软件参考逐帧比对；
- 每 5 轮追加一次 H.264 1080p 硬解；
- 每轮 `encode-test` H.264 10 帧与 JPEG 3 帧硬件编码（ownership、busy
  format、时间戳、EOS 全项检查）。

## 观测

| 指标 | 范围 |
| --- | --- |
| SoC 温度 | 47–56 °C |
| MemFree | 147536–209220 kB |
| MemAvailable | ≈ 399–405 MB |
| CmaFree | 44068–48644 kB（CmaTotal 65536 kB） |
| docker / sublinkpro / vocat | 191 轮全部 active |
| 容器数 | 4 |
| 失败轮次 | 0 |

CMA、内存与温度在观测窗口内无单调趋势，未见句柄或 CMA 泄漏。

## 中断

最后一轮之后脚本不再写日志、进程消失；随后 codec 模块被重新加载
（`meson8b-vcodec.service` 完成），再之后系统复位。日志中没有关机记录、
oops 或 panic；复位与 USB 存储子系统有关，详见
[outage-2026-11-15.md](outage-2026-11-15.md)。复位后
`meson8b-vcodec.service` 自动恢复，HEVC/H.264 解码与 H.264/JPEG 编码均
复测通过（见下），说明编解码栈本身在复位后可用。

## 复位后复测

| 项目 | 结果 |
| --- | --- |
| `meson8b-vcodec.service` | active，`/dev/video0`、`/dev/video1` 均在 |
| HEVC 1080p 硬解 | 通过，CmaFree 由 1308 kB 回到 50712 kB |
| H.264 10 帧编码 | `PASS: 10 frames, ownership, busy format, timestamps, EOS` |
| JPEG 3 帧编码 | `PASS: 3 frames, ownership, busy format, timestamps, EOS` |

复位后空闲时 `CmaFree` 曾低至 232–1308 kB，解码一次即回到约 50 MB，随后
又降到 37968 kB；这是页缓存占用可迁移 CMA 区间的正常回收行为，不是泄漏。

## 结论

持续负载下编解码功能与五项服务在观测窗口内稳定，但**本记录不构成 30 分钟
验收**：窗口被外部复位截断，且复位原因与编解码无关（USB 存储子系统自
11-12 起已持续报 `usb_submit_urb returned -121`）。完整 30 分钟、2 小时
与过夜验证仍待补做。
