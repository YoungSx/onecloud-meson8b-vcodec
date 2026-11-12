# 下一步任务计划

当前状态（2026-11-13）：H.264 1080p 硬解**正确性**已实机验收（300/300 逐帧 MD5 一致，
免重启可回滚）；第二会话挂起已修复（见下）；但当前 CPU 拷贝输出路径
**不省 CPU 也不省时间**（实测见 `evidence/ACCEPTANCE-2026-11-13.md`）。
按优先级排序的后续工作：

## 已完成（2026-11-13 本轮）

- ~~第二会话挂起~~：根因是 `vdec_1_stop()` 在 IMEM DMA 仍可能传输时直接复位
  处理器，导致同一模块加载下第二个会话微码加载后 0 帧输出。修复 = 厂商
  `amvdec_stop()` 序列：stop 时先等 `IMEM_DMA_CTRL` 忙位清零（1s 超时），并补
  CBUS dummy read 时序。修复后 bench（6 hw 会话）+ 双会话测试全部通过，
  无内核告警。见 `vdec/vdec_1.c`。
- ~~B 帧 1080p 样片~~：High profile + bframes=2 样片验收（结果见 evidence 更新）。

## P0 — 性能目标（下一轮会话）

1. **零拷贝/低 CPU 输出路径**：当前实现不满足验收 4 的"CPU 显著低于软解"。候选：
   a. **GE2D 零拷贝转换**：5.15 `meson/ge2d/ge2d.c` 支持有限 YUV 格式，
      需确认 NV12M(大端→标准) 是否可表达；GE2D 是独立硬件块，拷贝不占 CPU。
   b. **厂商写端字节序控制**：查 3.10 `vpp`/`canvas` 头文件中 Meson8 是否有
      VDEC 输出字节序寄存器（若有则完全零拷贝，风险是参考帧读取也受影响）。
   c. NEON 不适用（Cortex-A5 无 NEON）；纯 C bswap64 已是当前实现。
   完成前，对外表述必须是"功能可用（正确、释放 CPU 解码线程）但整机 CPU 更高"。

## P1 — 加固（1 轮会话）

4. **反复 open/close 压测**：10 轮 insmod/解码/rmmod + 内存/slab/dmesg 监控
   （依赖 P0-1 修复后才有意义）。
5. **分辨率切换**：同会话 320x240 → 1080p 切换（v4l2 src_change 路径）。
6. **错误输入**：截断/损坏 NAL 的行为确认。
7. **开机自动加载（可选）**：systemd unit，含失败自动 rmmod 回退。

## P2 — 使用集成（0.5 轮会话）

8. WanOS/OpenList 转码链路使用 h264_v4l2m2m 的配置示例文档。
9. `extract-firmware.py` 扩展 h265/mpeg12 微码 + 驱动侧平台数据复制。

## P3 — 编码（amvenc_avc 移植，验收 5）

体量约为解码移植的 2-3 倍，独立推进：

10. 寄存器级调研：HCODEC 供电/时钟、输入 canvas、码率控制命令队列、
    输出码流 FIFO、中断语义（底料已在 `evidence/vendor-avc-protocol.txt`）。
11. 最小可用 encoder：1080p H.264 CBR，V4L2 M2M 接口，ffmpeg
    `h264_v4l2m2m` encoder 可调；验收 = 编码结果软解可播。
12. 编码质量/性能测量。

## P4 — 上游化（可选，长期）

13. Meson8b 平台数据 + capture 路径整理成 linux-media RFC 补丁。
    若 P0-2a/b 实现了零拷贝，一并整理。

## 风险与边界

- 内核小版本升级破坏 vermagic 绑定，需重编两模块（构建流程在 README）。
- 目标机是在役生产机：P0-1 修复验证需严格用单会话回退路径，不做长时挂机实验。
- 测量必须在无值守（nohup）模式下做：管理 SSH 经 DERP 中继本身消耗目标机 CPU，
  会污染性能数据（本轮实测首次 cycle 与无值守复测结果相反，教训已写入 evidence）。
