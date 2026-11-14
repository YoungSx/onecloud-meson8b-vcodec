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
16. ~~MPEG-4 Part 2 解码~~：完成（e72f1a2 / aec6494）——
    vmpeg4_mc_5 微码（无尾部零哨兵，末词 0x00400000 是真代码，
    words=3196）+ codec_mpeg4.c。协议要点：4 解码槽 `((reg&7)-1)&3`、
    回收 `~(1<<idx)` 反相位掩码（与 mpeg12 的 idx+1 不同）、
    AV_SCRATCH_F 是 MEM_OFFSET_REG（workspace - 0x01400000 换算）
    非 fatal-error、AV_SCRATCH_C 是固件写入的流偏移（只读）。
    验收：720p hw vs sw PSNR 差 0.06dB，CPU **-94%**（3.35s →
    0.21s），mpeg12 回归干净，rmmod 回退干净。"只到 2 帧"是裸流
    ts=0 被 ffmpeg framesync 丢弃的测试假象（setpts 重定时解决）。
    数据见 `evidence/RC-MPEG4-ACCEPTANCE-2026-11-13.md`。
17. ~~VC-1 / AVS 解码~~：VC-1 完成（ccbbbb8 / d750ab9）——
    meson8b_vc1.bin（vc1_vc1_linux.h 的 MicroCode 数组，plain
    `unsigned[]` 声明，16K 补零）+ codec_vc1.c。仅选通 advanced
    profile（WVC1 / V4L2_PIX_FMT_VC1_ANNEX_G）；AV_SCRATCH_4=1
    是格式选择（0=WMV3 simple/main），漏写即 0 帧输出。VC-1 特有：
    VIFIFO 水位 (2/8)（厂商仅 vc1/avs 抬高）、独立 2MB workspace
    （GXL 模型，MEM_OFFSET_REG = paddr - 0x01f00000）、M8B 复位
    序列不同（BIT7|6|4 脉冲两次 + BIT9|8）。验收：5 个 fate 样片
    （176x144–1920x1080、逐行+隔行、含 B 帧）唯一帧哈希与软解
    **bit-exact**（hw_only=0），差异仅为已知 2 帧 drain 边界与 RPS
    重复显示帧（硬件每图只出一帧，属显示特性非解码错误）；mpeg12/
    mpeg4 回归干净（88/88 帧、逐帧 PSNR 全部 ≥40dB，末 2 帧 drain
    边界与 P4-16 基线一致）。AVS **N/A** —— 内核 uapi 无
    V4L2_PIX_FMT_AVS，ffmpeg 亦无 v4l2m2m 包装，集成层无落点。
    WMV3（simple/main）未选通：fate 无样片，留待有需求时翻转
    AV_SCRATCH_4 并补验收。H.263 N/A（h263_mc 在厂商树中未定义）、
    HEVC 解码 N/A（M8B 无 HEVC 核）、encoder 多会话 N/A。
    数据见 `evidence/RC-VC1-ACCEPTANCE-2026-11-13.md`。

## P6 — 能力扩展（2026-11-14 新增，按推荐优先级）

19. ~~MJPEG 硬解 + FORCE_KEY_FRAME 编码控件~~：完成（00491e9 /
    4f35deb / a18e820）—— meson8b_mjpeg.bin + codec_mjpeg.c。
    三个关键发现：(a) MJPEG 微码写**平面 Y/U/V**（厂商表
    AV_SCRATCH_4-7 每槽三个 canvas），2-plane NV12 表会使 U/V
    别名、V 覆盖 U —— set_canvas_mjpeg() 分配三 canvas，capture
    copy 时交织成 NV12M；(b) 微码只在 host 清零 MREG_FROM_AMRISC
    后才发下一帧 picinfo，ISR 漏清即"只解一帧"；(c) vb2 把 OUTPUT
    队列的 start_streaming 延迟到首个 OUTPUT QBUF（REQBUFS 后
    不 QBUF 的客户端），落在 STREAMON(CAPTURE) 之后，原有的
    STATUS_INIT+CAPTURE resume 门永不再触发 —— poweron 路径补
    OUTPUT 侧 resume 门（对 mpeg12/mpeg4/vc1 各序均恰好触发一次）。
    PSCALE 三角滤波表 + 相位步进 + pscaler 复位缺一不可（无
    in-stream 头，全靠 host 编程）。验收：320x240/720p/1080p 各
    30 帧 EOS 干净（33 buffer），luma PSNR 33-39 / chroma 30-36
    （含厂商全量程 YCbCr 增益 0.86/0.88，线性拟合扣除后残差），
    mpeg2 回归 96 帧不变，rmmod 回退干净。harness 增加 MJPEG SOI
    分块 + V4L2_BUF_FLAG_LAST 干净退出 + FOURCC 参数化。
    FORCE_KEY_FRAME（0x009909e5 button 型）S_EXT_CTRLS 验证通过。
20. JPEG 硬编码（HCODEC + jpeg_encoder_mc，AVC/JPEG 仲裁）。
21. 硬解→硬编转码管线（VDEC1+HCODEC 并发审计 + 压测）。
22. HEVC 解码（HEVC_ASSIST_SCRATCH_* 寄存器考古，M8B 无独立
    HEVC 核 —— 大概率 N/A，先做可行性结论）。

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
