# A 阶段：帧池与 CAPTURE 解耦（2026-11-19）

在役机 `wanos`（OneCloud S805），原内核 `5.15.79-meson`、64 MiB CMA，五项
业务服务在运行。测试只替换解码模块：`dev-swap.sh dev` 用仓库构建的
`meson-vdec.ko`（余量 2）覆盖已安装包里的 `c60842e8`，其余模块、固件与
`/boot` 不动。

| 模块 | md5 | 内容 |
| --- | --- | --- |
| 已安装（v0.3.0） | `c60842e8ffc02a1c9b208a9463adad4b` | 每 CAPTURE buffer 独立 shadow |
| 候选（帧池） | `15d5c77d25225e8ba7a42519ee5da152` | 驱动持有帧池，`h264_extra_buffers=2` |
| VIFIFO 2 MiB 对照 | `4498cb25d4d0afdaeb95ca41527bfa2b` | 仅改 `vdec.c` 的 `SZ_4M`，未采用 |

## 逐帧一致性

`decode-compare.sh check DIR`，每个 CAPTURE 数（default、2、4、20、32）跑
一遍全部样片，与参考逐帧比较：原始日志 [decode-compare.log](decode-compare.log)，
CAPTURE 1/2/4/20/32 的 H.264 与 HEVC 明细见 [extra-buffers.log](extra-buffers.log)。

- H.264 1080p/1360x768/1600x900/B 帧、HEVC 1080p/crop/left6/left64、MPEG-2
  （I 与 B 帧）、MPEG-4、rst-h264 在全部 CAPTURE 数下与参考一致。
- `vc1`、`vc1-small` 的条目是参考自身的产物：`vc1.md5` 为 0 行，`vc1-small`
  参考 1 行而运行时按 0 行交付（末帧缺陷，见 validation.md 的限制），
  与本次改动无关；同一脚本在已安装模块的对照运行
  （`dev/check-rel.log`）行为相同。
- 4 参考帧 B 帧片源单独在 default/2/4/20/32 下 120/120 一致
  （[4ref-capture-counts.log](4ref-capture-counts.log)）。

## 分辨率切换

`phase-a-res-switch.sh`：320x240 → 1600x900/1080p → 320x240 的拼接流，
CAPTURE 1、2、4、8、20、32 各跑一次，要求图片序列与分段单跑一致且以
LAST/EOS 结束。H.264 130 幅、HEVC 50 幅全部 PASS
（[res-switch.log](res-switch.log)）。

## 并发 CMA

1080p 解码与 1080p H.264 编码同时运行，每秒采样 `CmaFree`
（[concurrent.log](concurrent.log)）：

| 解码 | 帧数 | CmaFree 最低 | 结束 | 编码 |
| --- | --- | --- | --- | --- |
| HEVC 1080p | 30 | 8 kB | 23284 kB | 120 帧 |
| H.264 1080p 1 参考 | 300 | 3512 kB | 18836 kB | 120 帧 |
| H.264 1080p 4 参考 B 帧 | 120 | 636 kB | 26372 kB | 120 帧 |

三种组合的解码输出都与软件参考逐帧一致，编码码流都能被软件解码。余量
最小的是 HEVC 与 4 参考帧：8 kB 与 636 kB，仍在 64 MiB CMA 内完成。
样本来自更早一次运行 [concurrent-old-sample.log](concurrent-old-sample.log)。

## 余量从 5 收紧到 2

`h264_extra_buffers` 的取值用 4 参考帧 B 帧 1080p 片源实测：余量 1、2、3、5
逐帧一致；余量 2 在 CAPTURE 1/2/4/20/32 与 FFmpeg 默认参数下全部通过。
1080p 每槽 3 MiB，因此较原先的 5 每路会话省 9 MiB CMA。记录在
[开发计划](../../docs/development-plan.md)。

## 未完成

- 30 分钟持续负载在余量 2 下第 330 轮出现一次 H.264 零帧失败（详见下节）。
- VIFIFO 2 MiB 的对照模块已构建，尚未在实机跑完整回归。

## 余量 2 的零帧失败

2026-11-20 04:55，`soak.sh` 第 330 轮（第 66 次 H.264 解码）报
`FAIL h264 decode`：framemd5 为 0 字节（[soak-fail-h264.md5](soak-fail-h264.md5)），
内核窗口内除例行 `loaded firmware` 外没有任何 vdec/venc 警告或错误，CmaFree
36 MB，`video0`/`video1` 均在。同一晚余量 5 的 468 轮运行没有失败。

复现脚本 `pool-headroom-repro.sh` 按 soak 的顺序（HEVC 解码 → H.264 解码 →
H.264/JPEG 编码）重复 300 轮，每轮与参考比对，结果在
[headroom-300.log](headroom-300.log)：

```
2026-11-20T05:01:28+08:00 == start rounds=300 extra_buffers=2
2026-11-20T05:57:03+08:00 HEADROOM_DONE rounds=300
```

300 轮全部 300/300 且逐帧哈希一致，`pool_waiting_peak` 始终为 8，
没有一次零帧或短输出，期间 0 条 `FAIL`/`HASHMISMATCH`。因此这次失败
**没有**按槽数复现：余量 2 与余量 5 在 300 轮同负载下都干净。

累计计数：余量 2 的 H.264 1080p 解码 = soak 66 次（含 1 次失败）+ 复现
300 次 = 366 次里 1 次；余量 5 的 soak 468 轮里 H.264 解码 93 次全过。
两者与 v0.3.0 上那次 1/1700 的短输出是同一量级——500–2000 次一遇，
单次样本无法区分"余量 2 引入的缺陷"与"既有的稀有短输出"。判断为后者，
但记录保留：复现脚本和新 soak.sh 在失败时会留下 ffmpeg stderr 与
`pool_slots`/`pool_bytes`/`pool_waiting_peak`，下一次出现即可定位。

