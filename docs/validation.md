# 当前能力与验证边界

本页汇总各版本取得的能力证据；实机为OneCloud S805/Meson8b、原版Linux
5.15.79-meson、64 MiB CMA。最新正式发行版仍为v0.4.0，开发运行包的在役
来源、模块哈希和最后健康核对以[部署快照](rollout-status.md)为准。
历史成功不作为当前实时健康或后续main候选的验收。最近一轮containerd/
工具异常恢复后暂停新硬件实验；后续同步、缩放与音视频工具候选的验证
范围逐项见[验证记录](validation-records.md)，尚未完成的实机门槛继续保留。

11-26后按用户要求部署最新`20220a8`，默认NEON600幅精确及编码185幅内容
通过，独立小尺寸AAC/HLS时间线新增有界检查；精确源码/模块及限制见
[本次验收](validation-records.md#11-26-main-20220a8-部署验收)。GE2D未启用，历史根因
与1.3×目标仍未闭环，生产持续负载不因此恢复。

后续已正式部署单平面NV12直接读取与`f053ed2`共享面验证入口；临时GE2D→
encoder检查47幅内容/计数通过，结束恢复最新默认NEON。完整解码/硬件缩放
转码链仍待验收，见[共享交接记录](validation-records.md#11-26-ge2d-到编码器共享-dma-buf-交接)。

最新`926d0c0`进一步部署实际缩放→共享编码入口，9组47幅完整内容/顺序及
相对独立bilinear参考的全YUV质量通过，41幅直接读取、6幅布局回退复制。
该入口尚不含decoder，正式GE2D仍关闭，见[缩放记录](validation-records.md#11-26-ge2d-缩放后共享编码输入)。

后续`a6f3fc8`已接入decoder：24组272幅独立缩放与18组172幅共享编码通过
完整内容/顺序/质量，包含两种提前取消顺序。854→320的尺寸切换参考质量
仍失败，中心相位候选撤回，生产GE2D关闭；见[真实链记录](validation-records.md#11-26-真实解码缩放与共享编码交接)。

`2fea36e`已修复该首样本偏移，24组272幅独立decoder与22组244幅共享编码
全内容/顺序/质量通过，包含854→320静态/切换与两种提前取消顺序。默认
NEON及正式模块字节不变，可选GE2D单独保留部署；完整官方工具与长期稳定性
门槛继续保留，见[修复记录](validation-records.md#11-26-ge2d-滤波首样本重复修复)。

## 驱动能力

| 功能 | 实现与已验收范围 | 边界 |
| --- | --- | --- |
| H.264 解码 | 重启后H.264 50/50、1080p 300/300完整帧哈希复验；切换400/400精确一致 | 上述切换有界结果按原二进制记录；已补旧序列LAST，最新包50/50复验精确一致。历史随机短输出未闭环。 |
| HEVC 解码 | Main 8-bit；当前NEON包14组250/250全像素精确与2-slice 640×384 20/20精确一致，含small/crop/1080p、DPB16、CRA、开放GOP、裁剪切换、tiles、自然EOS、慢消费和两槽DPB1；典型1080p池节省6 MiB CMA；另增 #15 tiles 组合实机 6 组 56/56 零误差（1080p 2×2 多 slice、854×480 非 CTU 裁剪 3×2、非均匀 640×384 3×2、`CAPTURE_BUFFERS=4` 慢消费回压、`720p->854x480` 动态切换及两种提前 `STREAMOFF`+重开） | 原60/60尺寸切换与三段EOS30/30按其二进制记录；首slice `dependent_slice_segment_flag=1` 拒绝仅主机回归，Main10及长期参考未支持，满预算1080p并发未保证。 |
| H.263 解码 | baseline/PLUS、UFEP续帧与自定义尺寸已部署；首次小尺寸协商缺陷及首行 `GN=0` 异常拒绝已验证，4CIF 12帧平均PSNR 66.50 dB、PLUS 164×100 3帧平均PSNR 65.16 dB | VOP收尾修正已正式部署；720p no-slice 15/15、`1280×720`（`min_Y_PSNR 74.79 dB`、`CAPTURE_BUFFERS=4` 回压 12/12 与 `STREAMOFF` 重开 12/12）、`1920×1080`（10/10、`min_Y_PSNR 79.34 dB`）、固件缺失（`ENOENT`）/损坏（`EINVAL`）/恢复与官方 FFmpeg 7.0.2 `h263_v4l2m2m` 实测通过；Slice Structured（含默认 `ffmpeg h263p` 多线程输出）仍明确拒绝，可选工具拒绝及容量限制见[追平记录](codec-parity.md)。 |
| 显示元数据 | 只读selection/CROPCAP与MPEG-4/VC-1/H.263像素比例已随包部署 | 主机真实函数通过，H.263 baseline的CROPCAP为11/12、PLUS为1/1，HEVC854×480的coded864×480与可见selection已实测；原版FFmpeg非方形像素查询有[方向边界](codec-parity.md#显示区域与像素比例)。 |
| WMV3 解码 | 已部署Annex L/RCV转换及VC-1微码；320×240实机输出3幅，顺序与Y/U/V检查通过 | 参考5幅，仅3/5（`0..2` `max_Y_MAD=0.0017`、`min_Y_PSNR=72.24 dB`），尾帧验收失败；官方FFmpeg 7.0.2无WMV3 M2M映射，不计完整支持。 |
| MPEG-1/2、MPEG-4、VC-1 advanced | 已实现固件协议、帧池和drain；drain 静默期现锚定 `max(last_irq_jiffies, drain_jiffies) + 100 ms`；`MPEG-2` 150帧 B帧流连续5轮 `150/150`（哈希完全一致，`min_Y_PSNR 63.92 dB`）、90帧 `90/90` 及回压/`STREAMOFF`+重开 `150/150` 全通过；`MPEG-4` 修正 `MESON8B` 工作区（`0x01f00000` / `2 MiB`） | `MPEG-4` I/P 90/50帧实测 `88/90`、`48/50`（尾2帧缺失）；B帧流 `testsrc-vflip`、`testsrc2-q2` 与 `testsrc-q2` 各 `28/30`，后两者及行11起宏块写入停止（`min_Y_PSNR 11.26 dB`）仍在；VC-1（`SA00040` `6/7`、`SA00050` `14/15` 已交付零误差，新增 `VC1_REPEAT_COUNT` 诊断且保持每缓冲一幅解码图像语义）/WMV3 尾图仍开放，不宣称完整兼容。 |
| MJPEG 解码 | SOF0的归一化4:2:0/4:2:2/4:4:4，十组三图共30/30顺序/全平面质量通过，含小尺寸、854×480与1080p；修正色度stride及unity滤波 | 仅8-bit三分量交错scan，尺寸需匹配协商。SOF1、渐进、灰度及重复采样因子布局明确拒绝；色度降采样不与软件逐字节相同。 |
| JPEG 图片解码 | 基于 `meson8b_jpeg.bin`、`PSCALE` 与 `V4L2_CID_ROTATE` 实现 `V4L2_PIX_FMT_JPEG`；实机通过 9 组测试（`4:2:0`/`4:2:2`/`4:4:4`/灰度、`rot90/180/270`、`1280×720` 及 `1280×720→640×360` 缩放），补齐 `SOF2` 拒绝（`frames=0`）、`640×480`（`Y_PSNR 66.16 dB`）、`STREAMOFF`+重开 1/1 与 `JPEG->H264` 60/60，主机校验 1295 字微码一致 | 仅支持 8-bit SOF0 基线图片（灰度与标准 `4:2:0`/`4:2:2`(`21/11/11`)/`4:4:4`）；S805 PSCALE 写 32B 对齐色度跨距经暂存 `swab64` 解包输出；尚未随生产安装包部署，SOF1/渐进JPEG拒绝，官方 FFmpeg 7.0.2 无 `jpeg_v4l2m2m` 解码包装。 |
| H.264 编码 | Baseline/Level 4.0；重启后11种输入布局、1080p10帧、SAR 17:13的12帧及GOP/强制IDR/多slice/重开/VBR/峰值CPB完整软解/YUV通过；标准QP/VBR/GOP/SPS/VUI接口 | 短样片不代表所有尺寸/质量；BGR24拒绝；无Main/High/CABAC/B帧编码（厂商 `encoder.c` 未用 `B_Slice`），单AU上限1 MiB。低目标饱和验证：`640×360 @ 500k` 达 `1232 kbps`（`qp=32` 饱和 `57/60`），`1080p @ 1000k` 达 `11.41 Mbps`（`qp=32` 饱和 `28/30`），无严格 CBR。 |
| JPEG 编码 | 质量85、1080p五帧跳过REF0/REF1/ANALYSIS CMA并完成软解/PSNR（平均30.73 dB） | 本轮不继承历史900张1080p检查；原版FFmpeg无本项目JPEG M2M包装，使用直接客户端。 |
| 输入 DMA / 输出转换 | 连续单平面/双平面 NV12M/NV21M DMA-BUF 编码输入直读及非连续 `lima` 回退、解码导入 DMA-BUF 栅栏等待与稳态零 CPU padding 复用均已实测；显式 GE2D `CREATE_BUFS`/`PREPARE_BUF`、停流重开清零缩放、多 RGB/YUV 布局、BT.601/709 矩阵、翻转/旋转（0/90/180/270）、`ABGR32/RGBA32/ARGB32`、`BLEND_CONST`/`BLEND_OVER`（含 `SRC2` 反向及 `90/270` 两遍中转）、多遍单次复位及缓存 `capture_stage` 通过 | 显式 GE2D 600幅原生、60 帧 1080p DMA-BUF A/B 稳态提速约 8.6%（`20.42 ms/frame`）、`1080p->720p` 60/60 缩小、`1080p->720p` DRM dumb(4) `vdec->venc` 60/60（`wall=6342 ms`, `direct=60`, `copy_ns=0`）、9/9 纹理 alpha 混合（`max_err <= 1`）与单队列取消实测全通过；硬件超时复位注入矩阵、1080p 完整管道与生产默认GE2D启用仍未闭环。 |
| 会话与内存 | 两路720p与两路1080p各12帧完整软解/YUV通过；两路1080p实测最低CMA约5.1 MiB | 短时两路通过不保证任意并发或总吞吐；第二decoder STREAMON仍为EBUSY；官方 FFmpeg 7.0.2 `1080p` 300/300 转码（`-num_capture_buffers 24 -num_output_buffers 4`）约 `11.667 s`（`0.857×`），未达 `1.3×` 目标。 |
| 加载与回滚 | 当前包安装时enabled/active、切换内容通过，诊断后恢复命令成功；旧安装外部校验备份 | 用户重启后旧包自动加载通过；最新Tailnet故障时本地网页正常。新包冷启动/异常重启、整包降级及长期稳定性未验收。 |

[验证记录](validation-records.md)按主题保留实测版本、方法、结果和失败边界。
[重启后验收](validation-records.md#11-25-重启后格式修正与-ge2d-验收)与最新
[MJPEG记录](validation-records.md#11-25-mjpeg-滤波与输入边界)保留各自二进制的内容与失败范围；不把成功退出当完整验收。
原始附件按[仓库规则](repository-policy.md)独立归档，不随源码仓库提交。

## 码率和官方 FFmpeg

以官方未修改 FFmpeg 为兼容目标，标准 `-b:v`、QP、GOP、帧率可用。
平均目标过低时会饱和在已验证的 QP32 附近，需要减小尺寸/帧率或用软件编码。
直接 V4L2 客户端可成组启用标准峰值/CPB 控件，超预算 AU 失败而不发布；
官方包装未转发这两个控件。该设计不是严格 CBR、HRD 或网络限速。
见[码控](rate-control.md)及[FFmpeg 用法](ffmpeg-compatibility.md)。
SPS/VUI已包含裁剪、已知色彩和协商帧周期；历史30、30000/1001、24 fps MP4
检查通过，本轮SAR 17:13硬编12帧完整检查通过。官方包装不转发所有颜色字段，未知信息保持未指定；显式颜色标签可
在实际 colorspace 转换后使用标准 `h264_metadata`，不能仅改标签冒充转换。

## OpenList fork 能力

本项目负责驱动，应用代码位于独立的
[OpenList fork](https://github.com/YoungSx/OpenList/tree/feat/transcode-v4l2m2m)
和[前端 fork](https://github.com/YoungSx/OpenList-Frontend/tree/feat/transcode-v4l2m2m)。

- 通过标准 V4L2 格式/尺寸与 FFmpeg 包装器能力交集选择硬件；不写死芯片或设备编号。
- 本地/独立 worker 复用 HLS 参数与单任务硬件调度；跳转取消旧任务后再启动，预取不抢占当前段。
- 保留分数帧率和跳转时间线，按实际音视频切片码量/时长报告带宽。
- 默认 `warn` 发送平均目标并提示超出；`downscale` 在峰值/缓冲留空时用短探测选定尺寸，不能预测未来峰值；显式无法兑现的预算清晰拒绝。
- 前端显示准备/错误/状态并正确释放 HLS 实例；实际浏览器有界播放、前后跳转及取消通过，取消释放硬件约 0.242 s。

在役镜像为 `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`，
FFmpeg/ffprobe 与固定官方底座逐字节相同。测试中发生缓冲，**1080p 转码达到
1.3× 实时的目标未完成**，不能以硬件编码名称推断实时性。
容器设备映射授予宿主设备访问权，不会因应用支持 V4L2 而自动取消。

## 可靠性与当前开放项

两轮 LAN/Tailnet 同时失联必须分开：第一轮高置信度指向旧255 MHz选频路径
重调以太网共享 MPLL2，本机当时仍运行；当前固定父钟保护避免该路径。
第二轮使用保护路径，但 journal 尾部缺失，根因未定。
[调查](validation-records.md#失联与故障保存)保留证据强度与缺口。
生产持续负载实验暂停，不通过盲目重跑来证明稳定。

11-23 又发现 SSD 容量耗尽、Arcane agent 无法重启；已校验清理约1.53 GiB
重复归档并恢复 agent，剩余普通可用空间约426 MiB。这是独立的当前故障，
不能倒推为两次历史离线的根因。已部署限时被动观测，见
[容量与采集](validation-records.md#失联与故障保存)。

优先级与后续验收门槛统一见[路线图](../ROADMAP.md)。编译通过、设备节点出现、
成功退出或有限轮次无失败，都不能替代完整帧数/内容或长期稳定性。

## 研究线和不承诺的能力

Linux 7.3.4 的27个研究补丁已包含独立帧池、既有解码格式、syscon/genpd、
固定父钟与 IRQ/EOS/DMA 保护；内核、模块、DT/binding 检查通过，**未实机启动**。
另一板型、另一内核、跨板与研究静态 DT 均无运行验收。

没有发现适用于 S805 的 HEVC 编码实现；AVS无当前标准接口实现/验收承诺。
多编码会话已有两路720p/1080p的有界内容验收，见[厂商能力追平](codec-parity.md)和
[编码硬件依据](encoding-feasibility.md)。
AVS 的主线 UAPI/官方 FFmpeg 格式映射缺口已按固定提交重新核对；没有
复用其他 codec FOURCC，仍不提供 AVS 解码入口。

11-23[能力复核](capability-recheck.md)纠正了若干历史否定结论：H.263/AVS
微码、WMV3、HEVC tiles、多编码实例和 GE2D 均有厂商源码依据，属于继续
移植/集成的候选。HEVC tiles、WMV3、baseline/PLUS H.263已有当前源码实现，GE2D有独立验证
入口；tiles与H.263短样片已通过，WMV3仍缺尾帧；GE2D只做临时验收，不能宣称已追平原厂能力。

此前用户选择 P0，扩展实现暂停并登记独立 TODO issues。当前按新选择恢复
性能/GE2D、HEVC tiles 与格式能力开发；独立GE2D及decoder显式接入已有
有界内容验收，默认部署和解码输出路径仍不启用它，见[性能指南](performance.md)。
[11-23 P0 离线开发](validation-records.md#11-23-p0-部署)修复 HEVC
关闭重新排工作、编码微码 DMA 和 SPS/PPS 长度边界，主机/5.15.79 ARM
构建通过；随后按用户要求部署并完成有界硬件验收，见
[部署记录](validation-records.md#11-23-p0-部署)。新冷启动/长期稳定性
未验收，历史失联和少帧的因果关系未闭环。
