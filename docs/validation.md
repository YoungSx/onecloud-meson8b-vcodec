# 当前能力与验证边界

截至 2026-11-25 UTC，实机为 OneCloud S805/Meson8b、原版 Linux
5.15.79-meson、64 MiB CMA。完整运行包`39703c7`已保留安装和开机加载。用户重启后曾确认业务正常；
最新远程检查中五容器运行、四项HTTP200；此前Tailnet故障时本地网页正常，
链路故障根因仍未闭环。
最新正式发行版仍为v0.4.0。精确模块来源见[部署快照](rollout-status.md)。

## 驱动能力

| 功能 | 实现与已验收范围 | 边界 |
| --- | --- | --- |
| H.264 解码 | 重启后H.264 50/50、1080p 300/300完整帧哈希复验；切换400/400精确一致 | 上述切换有界结果按原二进制记录；已补旧序列LAST，最新包50/50复验精确一致。历史随机短输出未闭环。 |
| HEVC 解码 | Main 8-bit；当前NEON包14组250/250全像素精确，含small/crop/1080p、DPB16、CRA、开放GOP、裁剪切换、tiles、自然EOS、慢消费和两槽DPB1；典型1080p池节省6 MiB CMA | 原60/60尺寸切换与三段EOS30/30按其二进制记录；Main10及长期参考未支持，满预算1080p并发未保证。 |
| H.263 解码 | baseline/PLUS、UFEP续帧与自定义尺寸已部署；首次小尺寸协商缺陷已修复，已输出图像全Y/U/V接近软解 | VOP收尾修正已正式部署；四种baseline/PLUS/自定义尺寸/UFEP各3/3顺序/YUV通过。新增176×144、352×288 baseline及320×240、704×396 PLUS各12/12内容通过；Slice Structured仍拒绝，HD与异常矩阵待补。可选工具拒绝，容量限制见[追平记录](codec-parity.md)。 |
| 显示元数据 | 只读selection/CROPCAP与MPEG-4/VC-1/H.263像素比例已随包部署 | 主机真实函数通过，H.263 baseline的CROPCAP为11/12、PLUS为1/1，HEVC854×480的coded864×480与可见selection已实测；原版FFmpeg非方形像素查询有[方向边界](codec-parity.md#显示区域与像素比例)。 |
| WMV3 解码 | 已部署Annex L/RCV转换及VC-1微码；320×240实机输出3幅，顺序与Y/U/V检查通过 | 参考5幅，仅3/5，尾帧验收失败；官方FFmpeg无WMV3 M2M映射，不计完整支持。 |
| MPEG-1/2、MPEG-4、VC-1 advanced | 已实现固件协议、帧池和drain；历史MPEG-2小流90/90、B帧流149/150或150/150 | 重启后MPEG-2 B流30/30顺序/YUV通过；MPEG-4 I/P与B流均28/30，VC-1/WMV3尾图仍开放，不宣称完整兼容。 |
| MJPEG 解码 | SOF0的归一化4:2:0/4:2:2/4:4:4，十组三图共30/30顺序/全平面质量通过，含小尺寸、854×480与1080p；修正色度stride及unity滤波 | 仅8-bit三分量交错scan，尺寸需匹配协商。SOF1、渐进、灰度及重复采样因子布局明确拒绝；色度降采样不与软件逐字节相同。 |
| H.264 编码 | Baseline/Level 4.0；重启后11种输入布局、1080p10帧、SAR 17:13的12帧及GOP/强制IDR/多slice/重开/VBR/峰值CPB完整软解/YUV通过；标准QP/VBR/GOP/SPS/VUI接口 | 短样片不代表所有尺寸/质量；BGR24拒绝；无Main/High/CABAC/B帧编码，单AU上限1 MiB。RGB24最宽1376，其余布局见使用指南。 |
| JPEG 编码 | 重启后质量85、1080p三张完整软解/YUV通过 | 本轮不继承历史900张1080p检查；原版FFmpeg无本项目JPEG M2M包装，使用直接客户端。 |
| 输入 DMA / 输出转换 | 连续 NV12M/NV21M DMA-BUF 编码输入可直读；默认NEON；显式GE2D的MMAP复用一个DMA中转面，连续DMA-BUF采集直接写入 | GE2D600幅原生（含73幅真实DMA-BUF）和9幅MJPEG回退通过；官方7.0.2/8.1.3完整60幅视频转码及CPU缩放转码通过。生产默认NEON；实时、硬件缩放全链、音频/HLS及历史崩溃根因仍未闭环。 |
| 会话与内存 | 两路720p与两路1080p各12帧完整软解/YUV通过；两路1080p实测最低CMA约5.1 MiB | 短时两路通过不保证任意并发或总吞吐；第二decoder STREAMON仍为EBUSY；1080p60幅解码加编码完整内容/质量通过，约0.71×，未达目标。 |
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
