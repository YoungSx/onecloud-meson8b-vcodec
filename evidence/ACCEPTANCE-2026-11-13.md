# 实机验收记录 — 2026-11-12/13，目标机 100.87.212.107（OneCloud S805, 5.15.79-meson）

方法：`private/1080p-cycle.py` 完整循环 — insmod 两模块 → 软解 framemd5 基线 →
硬解 framemd5 → 逐帧 MD5 比对 → rmmod → dmesg 增量扫描（BUG/WARNING/Oops/refcount）→
服务状态检查。原始输出：`private/1080p-cycle.log`。

## 正确性（验收 4 前半）：通过

样片 `samples/h264-1080p-baseline.mp4`：1920x1080 Constrained Baseline, 300 帧,
SHA-256 3144c1a5...（完整清单见 samples/SHA256SUMS）。

- 硬解 300 帧全部输出，ffmpeg 退出码 0。
- **逐帧 MD5 与软解 300/300 一致**（多轮复测一致：1080p-cycle、无值守 A/B 复测）。
- dmesg：`applied volatile codec overlay 1`；`registered meson-video-decoder as
  /dev/video0`；`loaded firmware meson/vdec/meson8b_h264.bin (36864 bytes)`。
- 模块卸载干净，dmesg 增量无 BUG/WARNING/Oops/refcount 告警；卸载后无 /dev/video*。

## 性能与 CPU（验收 4 后半）：**未达标，如实记录**

三次独立测量（同一输入、framemd5 输出）：

| 测量 | 软解 wall | 硬解 wall | 备注 |
| --- | --- | --- | --- |
| 1080p-cycle（首次，~00:07） | 28.7s (10fps) | 12.2s (25fps) | 系统内存压力期（MemAvailable 344MB），软解被拖慢，**不可复现** |
| 无值守 A/B（~01:40） | 10.8s | 28.2s | 无人值守 nohup，无管理流量干扰 |
| 无值守 1080p-cycle 复测 | 10.5s (29fps) | 27.8s (11fps) | 同上，复现 A/B 结果 |

`/proc/stat` 系统级 CPU 增量（CLK_TCK=100，含内核 kworker）：

| | user | system+irq+softirq | 合计 CPU 秒 |
| --- | --- | --- | --- |
| 软解 | ~13.9 | ~1.8 | **~15.7** |
| 硬解 | ~2.1-3.3 | ~28-38 | **~30-41** |

结论：当前实现的硬解路径**正确但不省 CPU、不省时间**——shadow buffer 的内核态
8 字节字节序交换拷贝（1080p 约 3MB/帧）在 kworker 中消耗的 CPU 超过软解全部
解码开销，wall time 约为软解 2.7 倍。ffmpeg 进程自身的 user CPU 很低（~0.6s），
但那是成本转移到了内核线程，不是消失。

首次 cycle 的 "25fps vs 10fps" 读数是在内存压力下软解被偶发拖慢时测得，
不构成性能结论；后续两次无值守复测均反转。

**要达到验收 4 的"CPU 显著低于软解"，必须实现零拷贝/硬件转换输出路径**
（GE2D YUV 转换，或找到 VDEC 写端字节序控制的厂商依据）。见 ROADMAP P0-3。

## 验收 1-3：通过

- 系统未重启、未换内核、未改 /boot；目标机在役服务（sublinkpro、vocat、
  OpenList、Home Assistant、Arcane agent）全程正常，Docker RestartCount=0。
- rmmod 后目标机恢复原状（无模块、无 /dev/video*）。

## 验收 5：未实现

见 BLOCKERS-RESEARCH.md。

## 稳定性：第二会话挂起已修复（2026-11-13）

连续第二个 STREAMON 会话（不卸载模块、连续两次 ffmpeg hw 解码）曾会挂起：
第二次会话微码加载后 0 帧输出，ffmpeg 挂在 DQBUF。

根因：`vdec_1_stop()` 在 IMEM DMA 仍可能传输时直接复位处理器（DOS_SW_RESET0），
下一会话微码加载后处理器不产生中断。对照厂商 3.10 `amvdec_stop()` 序列修复：
stop 时先等 `IMEM_DMA_CTRL` 忙位（bit15）清零（1s 超时告警兜底），再补 CBUS
dummy read 时序，然后才复位。见 `vdec/vdec_1.c` `vdec_1_stop()`。

修复后（干净构建，无插桩）验证：
- `cpu-bench.py`：6 个连续 hw 会话（3 轮 × 2 解码）全部正常，无挂起；
- `two-session-test.sh`：3 个连续 hw 会话全部正常；
- dmesg 增量无 BUG/WARNING/Oops。

## B 帧 1080p（High profile, bframes=2）：通过（2026-11-13）

样片 `samples/h264-1080p-bframes.mp4`（SHA-256 1c428c1d...）。`bframes-cycle.py`
（sw → hw → hw2 三会话，hw2 不卸载模块直接二次解码，兼测第二会话修复）：

- sw/hw/hw2 退出码均 0，各 300 帧；
- **hw、hw2 与软解逐帧 MD5 300/300 一致**；
- 双模块卸载干净，dmesg 无 BUG/WARNING/Oops。
