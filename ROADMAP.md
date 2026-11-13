# 下一步任务计划

当前状态（2026-11-13 深夜）：硬解验收 1-4 全部通过；**P3 编码（HCODEC
H.264 encoder）三项全部完成**：1080p V4L2 M2M 编码可用（ffmpeg
`h264_v4l2m2m`），矩阵 PSNR 42.6-45.7 dB，1080p30 编码 CPU -75%、
28.4fps 近实时；**P4-14 CBR 码率控制完成**（误差 ≤1.4%）。
免重启可回滚。
剩余工作按优先级：

## 已完成（历史存档）

- ~~P0 零拷贝/低 CPU 输出~~：vmap 缓存别名 + dma_sync 路径达成验收 4
  （commit f7e3761；旧"不省 CPU"结论作废，数据见 evidence）。
- ~~第二会话挂起~~：vdec_1_stop() 厂商 IMEM-DMA 等待序列（commit e85bf76）。
- ~~B 帧 1080p~~：High profile + bframes=2 验收通过。
- ~~P1-4 反复压测~~：10/10 + 50 轮，无泄漏、无内核告警；CMA 56KB 极限通过。
- ~~P1-5 分辨率切换~~：竞态根因修复（changed_format → capture_queue_fits），
  shadow canvas 重分配，reinit 帧积压丢弃，OUTPUT 1MiB 下限；规范客户端
  350/350 帧（commit 1b3e94f）。注意：ffmpeg 7.0.2 自身 reinit 有死锁缺陷
  （sem_wait refsync），res-switch 流用 ffmpeg 播放会挂——这是 ffmpeg 上游
  问题，不是驱动问题；实际容器播放（分辨率不变或两段式 seek）不受影响。
- ~~P1-6 错误输入~~：损坏流不崩溃、模块存活、后续会话正常。

## P2 — 使用集成（0.5 轮会话）

8. ~~WanOS/OpenList 转码链路使用 h264_v4l2m2m 的配置示例文档。~~ 完成（0373337，docs/transcode.md）。
9. ~~`extract-firmware.py` 扩展 mpeg12 微码（vmpeg12_mc 单数组，16KiB 零填充）
   + 驱动侧 vdec_platform.c 平台条目（NV12M-only，M8B shadow 路径只支持
   NV12M）~~ 完成（24ed700）。关键发现：m8 固件在 MPSR=1 释放处理器时快照
   AV_SCRATCH_0-7 canvas 表——须在编程表后重启处理器（vendor amvdec_start
   序列）；固件只在下一幅图开始时才完成上一幅，EOS 需 .drain 软件冲刷
   （延迟 work 等待在途图片，空 LAST 帧 + EOS 事件）。1080p30 MPEG-2：
   0.60s CPU vs 4.42s 软解（-86%），H.264 300 帧 framemd5 回归一致。
   h265 在 M8B 上经 vdec_1 不可行（vdec_hevc.c 需要 GXL+ HEVC 核，
   厂商 m8 目录虽有 h265 微码）——标记 N/A。

## P3 — 编码（amvenc_avc 移植，验收 5）

体量约为解码移植的 2-3 倍，独立推进：

10. ~~寄存器级调研~~：完成（`evidence/vendor-avc-protocol.txt`）。
11. ~~最小可用 encoder~~：完成 —— 1080p H.264，V4L2 M2M，ffmpeg
    `h264_v4l2m2m` 可调，输出软解可播，矩阵 PSNR 42.6-45.7 dB
    （nv12/nv21/yuv420p，320x240-1920x1080），EOS drain 按
    v4l2-mem2mem 契约。数据见 `evidence/ENCODE-ACCEPTANCE-2026-11-13.md`。
    关键坑：HCODEC 时钟必须 fclk_div4/2 精确 318.75MHz（MPLL2 是
    以太网参考钟，CCF 重parent 会静默断网）；AO 寄存器两个窗口
    需偏移换算；canvas 地址是绝对总线地址（含 buffer 基址）。
12. ~~编码质量/性能测量~~：完成 —— 1080p30×150 帧：CPU 5.85s vs
    软编 23.89s（**-75%**），28.4fps 接近实时（软编 19.5fps），
    PSNR 45.6dB（QP 固定 26，vendor 模型）。注意：裸流 PSNR 比较前
    必须 `setpts=N/(fps*TB)` 重定时，否则 framesync 错配读到假 23dB。
    `scripts/venc-bench.py`。

## P4 — 能力补全（2026-11-13 晚新增，按推荐优先级）

14. ~~CBR 码率控制~~：完成（ba4f124）—— 厂商 amvenc 无硬件码率
    控制，但 quant 逐帧可变（ISR 置 need_reset → 每帧 prot_init）。
    加法式控制器：`V4L2_CID_MPEG_VIDEO_BITRATE` 非零开启（默认 0 =
    固定 QP 26），5M/8M 目标实测误差 -1.4%/-0.3%，固定 QP 会话
    零回归。两个 ffmpeg 交互坑：BITRATE 默认必须为 0（ffmpeg 总是
    发送 bit_rate，默认非 0 会让"固定 QP"静默变 CBR）；控件默认值
    不能与常用目标重合（v4l2 core 对值未变的 set 跳过 s_ctrl）。
    timeperframe（G/S_PARM）只喂预算。数据见
    `evidence/RC-MPEG4-ACCEPTANCE-2026-11-13.md`。
15. ~~B 帧编码~~：**N/A** —— 厂商 M8B encoder 命令集只有
    SEQUENCE/PICTURE/IDR/NON_IDR/MB_HEADER/MB_DATA（encoder.h），
    无 B-picture 命令，硬件不支持。（早期 ROADMAP 曾写"微码支持
    bframes"，系误判，更正。）
16. MPEG-4 Part 2 解码：vmpeg4_mc_5 微码（无尾部零哨兵，末词
    0x00400000 是真代码，words=3196）+ codec_mpeg4.c。协议要点见
    evidence：4 解码槽 `((reg&7)-1)&3`、回收 `~(1<<idx)` 反相位
    掩码（与 mpeg12 的 idx+1 不同）、AV_SCRATCH_F 是 MEM_OFFSET_REG
    （workspace - 0x01400000 换算）非 fatal-error、AV_SCRATCH_C
    是固件写入的流偏移（只读）。320x240 I-only 与单 GOP 流 150/148
    帧解码通过；720p 帧到用户态路径待修（目标机 SSH 中断暂停）。
17. VC-1 / AVS 解码：厂商 ucode 存在（vc1/、avs/ 目录），mpeg4
    落地后按同模式评估。H.263 N/A（h263_mc 在厂商树中未定义）、
    HEVC 解码 N/A（M8B 无 HEVC 核）、encoder 多会话 N/A。

## P5 — 上游化（可选，长期）

18. Meson8b 平台数据 + capture 路径整理成 linux-media RFC 补丁。
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
