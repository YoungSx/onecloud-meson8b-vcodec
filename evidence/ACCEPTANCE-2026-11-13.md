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

## 性能与 CPU（验收 4）：**达标（缓存别名拷贝路径，2026-11-13 复测）**

首版 shadow 拷贝在 coherent 内存上逐字节读，整机 CPU 反超软解（旧数据见 git
历史）。改用 vmap 缓存别名 + `dma_sync_single_for_cpu` 后（commit 1817393），
`syscpu-bench.py` 无值守复测（`/proc/stat` 系统级增量，CLK_TCK=100，含内核
kworker，1080p 300 帧完整解码）：

| | user CPU 秒 | system+irq+softirq | 合计 CPU 秒 | wall 秒 |
| --- | --- | --- | --- | --- |
| 软解 | 14.15 | 1.99 | **16.1** | 5.7 |
| 硬解 | 0.93 | 6.95 | **7.9** | 7.0 |

整机 CPU 总量约为软解的 **1/2**，其中 user 态低 **15 倍**（解码线程完全卸载，
为其他进程腾出用户态算力）；代价是 system 态上升与 wall 时间 +24%（缓存别名
拷贝占内核时间）。测量方法教训（管理 SSH 经 DERP 中继污染数据）保留在
`evidence/`，所有数字均为 nohup 无值守测量。

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

## P1-4 反复压测：通过（2026-11-13）

`stress-cycle.sh`（每轮 insmod 两模块 → 1080p hw 解码 → rmmod → dmesg 增量扫描
+ meminfo/slab 对比）：**10/10 轮通过**，另加 50 轮无内核 BUG/WARNING/Oops；
内存无泄漏（MemFree 改善 10.9MB，slab +28KB 属正常抖动）。
CMA 压力测试：tmpfs 挤压 CmaFree 至 56KB 时解码仍通过 300/300。

## P1-5 分辨率切换：通过（2026-11-13，commit 1a48649）

样片 `samples/res-switch.h264`（前 50 帧 320x240 → 300 帧 1920x1080，7.2MB）。

ffmpeg 自身无法验证该路径：其 v4l2 reinit 在持有未释放 AVBufferRef 时阻塞在
`sem_wait(refsync)`（ffmpeg 7.0.2 `v4l2_m2m.c:223`，上游缺陷），res-switch 流
回放必然挂起。改用自带规范客户端 `tests/res-switch-test.c`（POLLPRI → DQEVENT →
STREAMOFF → S_FMT/REQBUFS → STREAMON 完整 source-change 流程）：

- **350/350 帧**（50×320x240 + 300×1080p），与软解帧数一致；
- 两次 `codec_h264_resume` 干净完成，0 次 abort；
- 首帧竞态（同会话内 REQBUFS 与 ISR 侧 src_change 竞速导致偶发 0 帧）：改为
  检查已分配 capture buffer 是否适配新格式（替代可被 ISR 清零的 changed_format
  标志），100+30 连续 smoke 运行 50/50 帧。

本项同时修复两个跟随缺陷：M8B shadow canvas 在分辨率变化时按新尺寸重分配
（原实现返回 EINVAL 直接 abort）；reinit 期间固件积压 >6 帧就绪改为丢弃
（原实现 abort 整个会话）。OUTPUT plane 尺寸下限 1MiB（容器分辨率推导的
sizeimage 在切换后失效，导致 ESPARSER 搜索 padding 溢出丢包）。

## P1-6 错误输入：通过（2026-11-13）

`samples/corrupt.h264`（截断/损坏 NAL）：硬解 0 帧输出（软解 8 帧——固件对
损坏流容忍度更低），但**模块存活、无内核 BUG/WARNING/Oops、无 abort 洪泛**，
后续会话立即恢复正常（smoke 50/50）。驱动不崩溃、可恢复即达标。
