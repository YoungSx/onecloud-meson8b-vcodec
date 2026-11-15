# 30 分钟持续负载（中断于 191 轮）

运行区间 `2026-11-15T22:40:52+08:00` 至 `22:54:18+08:00`，共 191 轮，`FAILS=0`。
原始日志见 [soak-30min.log](soak-30min.log)。本轮**未跑满** 30 分钟：目标机在
23:01 前后连续两次硬复位，其中第一次复位前的 7 分钟里测试脚本已无输出，
第二轮复位后 USB 磁盘未能重新枚举，故不能作为完整的 30 分钟验收。

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

CMA、内存与温度在 14 分钟内无单调趋势，未见句柄或 CMA 泄漏。

## 中断

`22:54:18` 之后脚本不再写日志，进程消失；`22:57:48` codec 模块被重新加载
（`meson8b-vcodec.service` 完成）；`23:01:09` 系统硬复位。两次复位都没有
关机记录、oops 或 panic。复位后 `meson8b-vcodec.service` 自动恢复，
HEVC/H.264 解码与 H.264/JPEG 编码均复测通过（见下），说明编解码栈本身
在复位后可用。

## 复位后复测（2026-11-15 23:40 前后）

| 项目 | 结果 |
| --- | --- |
| `meson8b-vcodec.service` | active，`/dev/video0`、`/dev/video1` 均在 |
| HEVC 1080p 硬解 | 通过，CmaFree 由 244 kB 回到 51116 kB |
| H.264 10 帧编码 | `PASS: 10 frames, ownership, busy format, timestamps, EOS` |
| JPEG 3 帧编码 | `PASS: 3 frames, ownership, busy format, timestamps, EOS` |

复位后空闲时 `CmaFree` 曾低至 232 kB，解码一次后回到 51 MB，属于页缓存占用
可迁移 CMA 区间的正常表现，不是泄漏。

## 结论

持续负载下编解码功能与五项服务在观测窗口内稳定，但**本记录不构成 30 分钟
验收**：窗口被外部复位截断，且复位原因与编解码无关（USB 存储子系统在
复位前数小时已持续报 `usb_submit_urb returned -121`）。完整 30 分钟、2 小时
与过夜验证仍待补做。
