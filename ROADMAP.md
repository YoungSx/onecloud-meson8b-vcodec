# 下一步任务计划

当前状态（2026-11-13 晚）：H.264 1080p 硬解**验收 1-4 全部通过**（正确性 300/300
MD5、CPU 总量 16.1→7.9 CPU-s 即 1/2、user 态 15 倍降低；P1 加固三项全部完成：
压测 10+50 轮、分辨率切换 350/350、损坏输入不崩溃）。免重启可回滚。
剩余工作按优先级：

## 已完成（历史存档）

- ~~P0 零拷贝/低 CPU 输出~~：vmap 缓存别名 + dma_sync 路径达成验收 4
  （commit 1817393；旧"不省 CPU"结论作废，数据见 evidence）。
- ~~第二会话挂起~~：vdec_1_stop() 厂商 IMEM-DMA 等待序列（commit de14360）。
- ~~B 帧 1080p~~：High profile + bframes=2 验收通过。
- ~~P1-4 反复压测~~：10/10 + 50 轮，无泄漏、无内核告警；CMA 56KB 极限通过。
- ~~P1-5 分辨率切换~~：竞态根因修复（changed_format → capture_queue_fits），
  shadow canvas 重分配，reinit 帧积压丢弃，OUTPUT 1MiB 下限；规范客户端
  350/350 帧（commit 1a48649）。注意：ffmpeg 7.0.2 自身 reinit 有死锁缺陷
  （sem_wait refsync），res-switch 流用 ffmpeg 播放会挂——这是 ffmpeg 上游
  问题，不是驱动问题；实际容器播放（分辨率不变或两段式 seek）不受影响。
- ~~P1-6 错误输入~~：损坏流不崩溃、模块存活、后续会话正常。

## P2 — 使用集成（0.5 轮会话）

8. WanOS/OpenList 转码链路使用 h264_v4l2m2m 的配置示例文档。
9. ~~`extract-firmware.py` 扩展 mpeg12 微码（vmpeg12_mc 单数组，16KiB 零填充）
   + 驱动侧 vdec_platform.c 平台条目（NV12M-only，M8B shadow 路径只支持
   NV12M）~~ 完成（d3dee91）。关键发现：m8 固件在 MPSR=1 释放处理器时快照
   AV_SCRATCH_0-7 canvas 表——须在编程表后重启处理器（vendor amvdec_start
   序列）；固件只在下一幅图开始时才完成上一幅，EOS 需 .drain 软件冲刷
   （延迟 work 等待在途图片，空 LAST 帧 + EOS 事件）。1080p30 MPEG-2：
   0.60s CPU vs 4.42s 软解（-86%），H.264 300 帧 framemd5 回归一致。
   h265 在 M8B 上经 vdec_1 不可行（vdec_hevc.c 需要 GXL+ HEVC 核，
   厂商 m8 目录虽有 h265 微码）——标记 N/A。

## P3 — 编码（amvenc_avc 移植，验收 5）

体量约为解码移植的 2-3 倍，独立推进：

10. 寄存器级调研：HCODEC 供电/时钟、输入 canvas、码率控制命令队列、
    输出码流 FIFO、中断语义（底料已在 `evidence/vendor-avc-protocol.txt`）。
11. 最小可用 encoder：1080p H.264 CBR，V4L2 M2M 接口，ffmpeg
    `h264_v4l2m2m` encoder 可调；验收 = 编码结果软解可播。
12. 编码质量/性能测量。

## P4 — 上游化（可选，长期）

13. Meson8b 平台数据 + capture 路径整理成 linux-media RFC 补丁。
    resume 竞态修复（capture_queue_fits）值得单独提交上游——它是通用
    stateful decoder 竞态，GXL/G12A 同样受影响。

## 可选项（原 P1-7）

- 开机自动加载 systemd unit，含失败自动 rmmod 回退（当前手动 insmod 流程
  已稳定运行，按需再做）。

## 风险与边界

- 内核小版本升级破坏 vermagic 绑定，需重编两模块（构建流程在 README）。
- 目标机是在役生产机：所有验证用单会话回退路径；kill -9 残留会话后需
  rmmod+等待+重载两模块清 VDEC 状态（观测到的 abort 洪泛属此情况）。
- 测量必须无值守（nohup）：管理 SSH 经 DERP 中继污染 CPU 数据。
