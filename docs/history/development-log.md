> 历史快照：此处描述研究阶段，不代表当前功能。当前范围见[验证指南](../validation.md)。
>
> 2026-11-23 更正：下文“H.263 微码未定义”和“encoder 多会话 N/A”
> 已被固定厂商源码否定；AVS 缺少集成入口不代表硬件做不到。
> “无独立 B-picture 命令所以硬件不支持”也不是充分证据，当前仍未找到
> 可用 B/CABAC 编码路径。保留原始记录，准确判断见[能力复核](../capability-recheck.md)。

# 下一步任务计划

当前状态（2026-11-14）：原内核运行时硬解、硬编及自动加载已验证；**P3 编码（HCODEC
H.264 encoder）三项全部完成**：1080p V4L2 M2M 编码可用（ffmpeg
`h264_v4l2m2m`），矩阵 PSNR 42.6-45.7 dB，1080p30 编码 CPU -75%、
28.4fps 近实时；**P4-14 CBR 码率控制完成**（误差 ≤1.4%）。
免重启可回滚。
HEVC、B 帧并发与最终安装见末尾“最终补全”；以下保留开发阶段记录。
未替换内核或重启，新的启动内核不在已验证范围。

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

8. ~~WanOS/OpenList 转码链路使用 h264_v4l2m2m 的配置示例文档。~~ 完成（0373337，docs/usage.md）。
9. ~~`extract-firmware.py` 扩展 mpeg12 微码（vmpeg12_mc 单数组，16KiB 零填充）
   + 驱动侧 vdec_platform.c 平台条目（NV12M-only，M8B shadow 路径只支持
   NV12M）~~ 完成（24ed700）。关键发现：m8 固件在 MPSR=1 释放处理器时快照
   AV_SCRATCH_0-7 canvas 表——须在编程表后重启处理器（vendor amvdec_start
   序列）；固件只在下一幅图开始时才完成上一幅，EOS 需 .drain 软件冲刷
   （延迟 work 等待在途图片，空 LAST 帧 + EOS 事件）。1080p30 MPEG-2：
   0.60s CPU vs 4.42s 软解（-86%），H.264 300 帧 framemd5 回归一致。
   HEVC 不走 VDEC1；旧“无 HEVC 核”判断已被固定厂商源码证伪。
   Meson8b 有独立 HEVC 解码块，V4L2 codec 已完成，见最终补全和 HEVC 验收。

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
    HEVC 已完成 Main 8-bit 移植（见最终补全）；encoder 多会话 N/A。
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
20. ~~JPEG 硬编码~~：完成。HCODEC JPEG CAPTURE + 1–100 质量控件，
    NV12/NV21/YUV420 输入，320×240–1080p 八组均可软解播放；单 open
    仲裁 AVC/JPEG，时间戳、LAST+EOS、缺失/截断固件恢复验证通过。
    H.264 ioctl 与 ffmpeg 1080p 回归通过。见
    `evidence/P6-JPEG-ENCODE-ACCEPTANCE-2026-11-14.md`。
21. ~~硬解→硬编转码管线~~：完成。共享 DOS 映射和控制寄存器锁、
    同一 PMU regmap、编码 DMA 分区；720p 与 1080p Baseline 60 帧
    可播，1080p 连续五轮，PSNR 平均 45.41 dB，独立硬解 MD5 60/60。
    1080p B 帧并发限制已修复，当前验收见 BFRAME-TRANSCODE 记录。历史依据见
    `evidence/P6-TRANSCODE-ACCEPTANCE-2026-11-14.md`。
22. ~~HEVC 可行性核查~~：完成。固定厂商源码明确 `HAS_HEVC_VDEC=1`，
    Meson8b 有独立 HEVC 核、1080p 上限；旧 N/A 结论错误。寄存器、
    微码、时钟和移植缺口见 `docs/hevc-feasibility.md`。HEVC V4L2
    Main 8-bit 实现和实机验收已完成，见最终补全。

## P5 — 上游化（可选，长期）

18. 本地 RFC 草稿和 5.15.79 additive 补丁已整理，见 `docs/linux-media-rfc.md`。
    当前主线移植、静态 DT/power-domain 设计和公开提交未完成。resume 竞态
    修复（capture_queue_fits）可独立评审；其他 GX 平台未在本机验证。

## 自动加载（原 P1-7，已完成）

- systemd unit 与校验安装、失败卸载、固件恢复已实机验证；最终包已启用。
  未重启验证开机过程。

## 风险与边界

- 内核小版本升级破坏 vermagic 绑定，需重编三个模块（构建流程在 README）。
- 目标机是在役生产机：所有验证用单会话回退路径；kill -9 残留会话后需
  rmmod+等待+重载两模块清 VDEC 状态（观测到的 abort 洪泛属此情况）。
- 测量必须无值守（nohup）：管理 SSH 经 DERP 中继污染 CPU 数据。

## 2026-11-14 最终补全

- HEVC Main 8-bit 驱动与固定微码完成：320×240、854×480 crop、1080p B 帧逐帧精确匹配，重复会话、损坏流 watchdog 和固件恢复验证。
- 1080p H.264 B 帧并发完成：原始输入 DMA 按格式尺寸分配，EOS batch 按 PTS 排序，60 帧 PSNR 最低 45.03 dB；十轮混合负载全部完整输出。
- JPEG 长时完成：3×300 1080p 图像，全部可软解播放；避免在 tmpfs 保留 900 帧原始参考。
- 自动加载与失败回滚完成：systemd 启停、故障加载卸载、恢复与移除均实测。
- RFC 草稿和 additive 内核补丁更新；尚未向 linux-media 发送，也未宣称上游合入。
- OpenList、Arcane 原容器恢复；最终五项服务在线。原内核与 /boot 未改变。
