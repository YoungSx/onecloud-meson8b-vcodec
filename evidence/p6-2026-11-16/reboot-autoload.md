# 重启后自动加载验收（2026-11-16）

关闭此前一直标为"未验证"的一项：运行包（`c20a473`）装在在役机上之后，
**冷启动会自己把 codec 模块和两个 V4L2 节点拉起来**。

这次不是计划内的 `reboot` 命令，而是当天 USB 存储失效与硬复位之后的冷启动。
对自动加载来说这比优雅重启更强：没有人在旁边补执行任何命令。

## 证据

内核日志按开机相对时间戳排列，与 journal 的时间无关（板子无 RTC，见下文）：

```
[   29.115517] meson8b_runtime: loading out-of-tree module taints kernel.
[   29.128936] meson8b-runtime: applied volatile codec overlay 1
[   29.161314] meson-vdec d0050000.video-decoder: registered meson-video-decoder as /dev/video0
[   29.161352] meson-vdec d0050000.video-decoder: Meson8b capture uses separate references and CPU byte-order conversion
[   29.196859] meson-venc d0050000.video-encoder: registered meson-video-encoder as /dev/video1
```

即内核启动 29 s 时模块已加载、overlay 已应用、两个设备节点已注册——是开机路径
完成的，不是事后手工 `modprobe`。

| 项 | 值 |
| --- | --- |
| Unit | `meson8b-vcodec.service`：`enabled`、`active (exited)`、`ExecMainStatus=0` |
| 节点 | `/dev/video0`、`/dev/video1` 均在本次启动中创建 |
| 内核 | `5.15.79-meson #trunk ... 2022-11-18`，即**原厂内核，未替换**；`/boot` 未改 |
| Overlay | `SOURCE_COMMIT=c20a4732c4017a1ff9b30262b36f99d5a6491943`，`KERNEL_RELEASE=5.15.79-meson` |
| 故障 | dmesg 无 oops/panic/`BUG:`（唯一的 grep 命中是 `ramoops` 模块名）、无 refcount/`WARNING:` |

开机后解码冒烟：把参考流按 MPEG-4 下发，导出 `17301504 B / 196608 = 88 帧`，
与改动前的基线一致（见 [m4v-tail.md](m4v-tail.md) 的 base 行）。

## 五项业务服务

冷启动后同一台机上：OpenList、sublinkPro、VoCat、Home Assistant、Arcane
agent 的端口 `5245`、`8000`、`7575`、`8123` 全部返回 `200`。运行 1 h 52 min
时 `MemAvailable` 350 MB、`CmaFree` 4.2 MB、温度 41.9 °C。

## 时间戳说明

板子无 RTC，启动早期时钟落在 2024/1970，之后被校时**向前跳变约 5 h**，因此
`journalctl -b` 里同一单元的 `Starting`/`Finished` 时间（14:50:41 / 20:07:02）
不能用来判断先后，也不能用来算开机耗时。本页结论只用 `dmesg` 的相对时间戳和
`/proc/uptime`。

## 未覆盖

- 这次只验证"自动加载 + 节点出现 + 解码冒烟"，没有在开机后跑全部 codec 的
  逐帧回归；H.264/HEVC/JPEG 与持续负载的基线仍来自当天的既有记录。
- 是复位后的冷启动，不是 `systemctl reboot` 的优雅重启；优雅重启路径未单独测。
- 自动加载失败时的回退分支仍只有主机测试（`tests/test_runtime_device.py`），
  没有在机上故意造过加载失败。

原始记录：[reboot-autoload.log](reboot-autoload.log)。
