# 当前能力与验证边界

截至 2026-11-23，实机为 OneCloud S805/Meson8b、原版 Linux 5.15.79-meson、
64 MiB CMA。当前编解码模块源码为 `3c7d785`，runtime沿用既有二进制，最新正式版为 v0.4.0；后续文档/工具
提交不改变已安装模块。精确来源见[部署快照](rollout-status.md)。

## 驱动能力

| 功能 | 实现与已验收范围 | 边界 |
| --- | --- | --- |
| H.264 解码 | 1080p 300/300 图像哈希、4 参考帧/B 帧样片、独立帧池；CAPTURE 1/2/4/20/32 | 历史偶发 300→299 短输出未被证明彻底消除；新 IRQ/EOS 修复只通过有界回归。 |
| HEVC 解码 | Main 8-bit；1080p 30/30、crop 及分辨率切换图像检查 | 不支持 Main10、tiles、长期参考帧。 |
| MPEG-1/2、MPEG-4、VC-1 advanced | 现有固件协议、帧池和 drain；MPEG-2 小流 90/90，B 帧流 149/150 或 150/150 | MPEG-4 部分 B 帧内容损坏、尾帧缺失；VC-1 唯一图像与重复显示/尾帧有边界，不能宣称完整格式兼容。 |
| MJPEG 解码 | 固件解码、全量程输出和有界帧数检查 | 厂商色度处理，不能声明与软解逐字节相同。 |
| H.264 编码 | Baseline/Level 4.0；YUV420/422/444、RGB 输入；固定 I/P QP、VBR、强制 IDR、GOP、多分片、SPS 裁剪/VUI/帧周期 | 无 Main/High/CABAC/B 帧编码；多分片整帧交付；单帧码流上限 1 MiB。RGB24 最宽 1376，其他具体布局见使用指南。 |
| JPEG 编码 | 全量程 YCbCr，质量 1–100；历史 900 张 1080p 软解检查 | 与 AVC 共用 HCODEC，二者互斥；原版 FFmpeg 无本项目可用的 JPEG M2M 包装，使用直接客户端。 |
| 输入 DMA / 输出转换 | 兼容连续 NV12M/NV21M DMA-BUF 输入可直读；解码输出有 NEON 字节序转换、异步交付 | 仍有 CPU 转换和拷贝，非端到端零拷贝；GE2D 未集成。 |
| 会话与内存 | 一路解码加一路编码；内部帧池与应用 CAPTURE 数解耦；默认 FFmpeg 数量可用 | 1080p 并发只对已测试码流成立，不保证任意参考结构都适合 64 MiB CMA。第二 decoder STREAMON 返回 EBUSY。 |
| 加载与回滚 | 外部模块、运行时 overlay、固件及自动加载；保留原 kernel/boot DT/CMA | 旧版通过冷启动和 service restart；当前新包只通过热更新/短回归，未完成新的冷启动与异常重启验收。 |

[验证记录](validation-records.md)按主题保留实测版本、方法、结果和失败边界。
最新 11-23 P0 部署的短时内容、HEVC关闭/重开、编码和固件失败恢复通过。
原始附件按[仓库规则](repository-policy.md)独立归档，不随源码仓库提交。

## 码率和官方 FFmpeg

以官方未修改 FFmpeg 为兼容目标，标准 `-b:v`、QP、GOP、帧率可用。
平均目标过低时会饱和在已验证的 QP32 附近，需要减小尺寸/帧率或用软件编码。
直接 V4L2 客户端可成组启用标准峰值/CPB 控件，超预算 AU 失败而不发布；
官方包装未转发这两个控件。该设计不是严格 CBR、HRD 或网络限速。
见[码控](rate-control.md)及[FFmpeg 用法](ffmpeg-compatibility.md)。
SPS/VUI 已包含裁剪、已知色彩和协商帧周期；30、30000/1001、24 fps MP4
检查通过。官方包装不转发所有颜色字段，未知信息保持未指定；显式颜色标签可
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

没有发现适用于 S805 的 HEVC 编码实现；AVS/H.263、WMV3、多编码会话也无
当前实现/验收承诺。见[编码硬件依据](encoding-feasibility.md)。

11-23[能力复核](capability-recheck.md)纠正了若干历史否定结论：H.263/AVS
微码、WMV3、HEVC tiles、多编码实例和 GE2D 均有厂商源码依据，属于继续
移植/集成的候选。当前仍未提供这些能力；来源检查与本地实验不构成实机验收。

此前用户选择 P0，扩展实现暂停并登记独立 TODO issues。当前按新选择恢复
性能/GE2D、HEVC tiles 与格式能力开发；独立 GE2D 字节平面验证驱动尚无
实机验收，默认部署和解码输出路径不启用它，见[性能指南](performance.md)。
[11-23 P0 离线开发](validation-records.md#11-23-p0-部署)修复 HEVC
关闭重新排工作、编码微码 DMA 和 SPS/PPS 长度边界，主机/5.15.79 ARM
构建通过；随后按用户要求部署并完成有界硬件验收，见
[部署记录](validation-records.md#11-23-p0-部署)。新冷启动/长期稳定性
未验收，历史失联和少帧的因果关系未闭环。
