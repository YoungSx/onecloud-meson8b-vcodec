# S805 硬件编码扩展可行性

2026-11-14 核对原始 Amlogic S805 datasheet、固定 Hardkernel 厂商源码树、
编码 host 协议与微码。**没有发现可实现的 S805 HEVC 硬件编码路径。**
该结论依据公开硬件能力与协议，不能解读为已证明任何理论混合算法都不可能。
H.264/JPEG 仍有驱动与数据路径的扩展空间。

## HEVC 的硬件边界

原始 [S805 Datasheet V0.8](https://github.com/DeciHD/amlogic_docs/blob/58fe4e1f878a1b2c4899eca6ea52e4cb6ec7cea2/s805/S805_Datasheet%20V0.8%2020150126.pdf)
第 7 页说明独立 encoder 的格式为 JPEG、H.264，H.264 最高 1080p30；
第 9 页在 Decoding 中列出 HEVC Main，在 Encoding 中列出 JPEG 与 H.264。
公开 datasheet 没有完整 codec 寄存器手册；不据此捏造未公开模式。

厂商 Linux 固定提交 `5aed95d35d252cafc75ce613a3a0052285662de2` 与上述一致：

- [Kconfig](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/Kconfig#L107)
  将 H265 定义为 decoder，encoder 配置只有 AVC 和 JPEG。
- [平台配置](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/amports_config.h#L11)
  为 Meson8b 设置 `HAS_HEVC_VDEC=1`，不能把这个宏当成 HEVC encoder 能力。
- [encoder.h](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/encoder.h#L585)
  的命令与 NAL 定义是 AVC 序列、IDR/non-IDR、宏块 header/data；
  [编码微码选择](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/m8/ucode/encoder/encoder_mc.c)
  对应 H.264 的 full/mixed 路径。
- 完整、未截断的该提交 Git tree 中没有 HEVC/H265 encoder 或 WAVE 微码
  路径。这个搜索范围和输入哈希保存在[源码清单](../evidence/encoder-feasibility-2026-11-14/sources.json)
  与[目录核查](../evidence/encoder-feasibility-2026-11-14/vendor-inventory.json)。

后代芯片的 HEVC encoder 是另一硬件 IP。例如 Amlogic GXL 的
[厂商设备树](https://android.googlesource.com/kernel/arm64/+/refs/heads/android-tv-deadpool-4.9-android11/arch/arm64/boot/dts/amlogic/gxl_sei210_1g.dts)
分别列出 `amvenc_avc` 和 `cnm, HevcEnc`，后者具有 `wave420l_irq` 与独立
`0xc8810000` 寄存器窗口。这种驱动需要 WAVE 硬件，并非 S805 HCODEC
换 compatible 或微码即可运行。

HEVC decoder 也不能反向运行成 encoder。解码器执行从码流恢复图像的
解析、预测和重建；编码需要模式/运动搜索、量化与 HEVC 熵编码等不同
处理链。现有 S805 host/微码没有连接这条编码链的接口。

因此本项目不添加虚假的 `V4L2_PIX_FMT_HEVC` 编码格式，不向 S805 加载后代
WAVE 固件。软件 x265 或外部机器/设备的 HEVC encoder 是另一种实现，
不能标为 S805 片上 HEVC 硬编。用部分 AVC 单元构造 HEVC 混合编码器仍缺乏
兼容运算、数据接口和性能证据，不能承诺为可用的硬编扩展。

## H.264/JPEG 的可行扩展

以下是源码证据支持的开发方向，尚未实现或完成对应实机验收。先用标准
V4L2 控件/API 实现，再核对输入布局、码流、延迟和回滚；不能只看 probe。

| 方向 | 已有依据和当前缺口 | 验收要求 |
| --- | --- | --- |
| 编码码率与质量控制 | 当前 CBR 为累积码量逐帧调 QP；厂商还有宏块 QP 和运动估计配置。可先改进纯软件控制，再研究微码参数 | 相同目标码率、场景切换、短窗口峰值、逐帧质量与 CPU/延迟 |
| H.264 多分片与低延迟 | 厂商 `rows_per_slice` / `FIXED_SLICE_CFG` 路径；本驱动当前固定 0。寄存器配置可能改变码流分片，但没有证明能提前回传每个 slice | NAL 数量、解码内容、I/P 帧、EOS；若宣称低延迟，须测逐 slice 的硬件完成与交付时刻 |
| RGB/RGBA、YUV422/444 输入 | 厂商 MFDIN 有格式、RGB2YUV、下采样/线性输入配置；当前驱动仅选通 4:2:0 输入 | 标准 fourcc、stride、通道顺序、色彩矩阵/量程、边缘填充、JPEG/AVC 回归 |
| 减少编码输入拷贝 | encoder vb2 接受 DMA-BUF，但仍复制到内部 staging；可评估兼容连续 DMA 布局直接送 MFDIN | 地址跨度、cache/fence 同步、canvas 生命周期、padding、流结束和错误恢复 |
| 明确 I/P QP、VUI 与头部行为 | 当前仅 QP min/max、GOP、FORCE_KEY_FRAME 等控件；header mode 被存储但未用于改变行为 | 按实际微码能力暴露控件，不伪造 profile/CABAC/B 帧支持；检查 SPS/PPS/VUI 和播放器兼容性 |
| 较低分辨率更高吞吐 | 减少每帧处理量可探索硬件现有吞吐，1080p30 是官方 H.264 编码规格 | 无帧丢失、连续吞吐、峰值延迟、温度和五项原有服务负载 |

具体厂商扩展点为
[分片和宏块量化](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/encoder.c#L596)及
[MFDIN 输入配置](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/encoder.c#L615)。
当前 staging/copy、控制器和控件处理分别在
[venc.c](../venc/venc.c)；整体内存约束见[架构](architecture.md)。

增加 B 帧、CABAC/High profile、HEVC、4K 编码不能靠更多工作量作保证。
其中 HEVC 没有已知 S805 硬件路径；其他能力也需各自微码/寄存器依据，不能
由 H.264 decoder 的 High profile 支持推导 encoder 的 profile。

推荐先完善 H.264 控件与码率控制，再推进 MFDIN 输入格式/直接 DMA 和多分片。
这次核查没有改动生产驱动、触碰新硬件寄存器或增加未经验证的编码格式。
