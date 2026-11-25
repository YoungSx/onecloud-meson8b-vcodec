# S805 硬件编码扩展可行性

2026-11-14 核对原始 Amlogic S805 datasheet、固定 Hardkernel 厂商源码树、
编码 host 协议与微码。**没有发现可实现的 S805 HEVC 硬件编码路径。**
该结论依据公开硬件能力与协议，不能解读为已证明任何理论混合算法都不可能。
H.264/JPEG 仍有驱动与数据路径的扩展空间。
11-23 用户要求重新核查否定结论，新增编码库/厂商源码依据仍未找到适用于
S805 的 HEVC 编码路径。完整[能力复核](capability-recheck.md)同时纠正了
H.263、GE2D 和多编码会话等旧判断；优先级见[路线图](../ROADMAP.md)。

本轮补充：H.264 SPS/VUI 的裁剪、已知色彩与协商帧周期已实现并通过有界
实机检查，详见[当前状态](validation.md)。下表是历史可行性依据，
不代表功能仍全部缺失。

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
  路径。搜索对象为上述固定提交的完整 tree；原目录响应和输入清单已独立
  归档，后续复核仍应固定该提交，不能以变化后的分支替代。

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

## 当前实现与仍需研究的方向

已实现：I/P QP与VBR、标准峰值/CPB控件、多分片整帧交付、YUV/RGB输入、
兼容DMA-BUF直读、SPS裁剪/VUI。对应限制与验收统一见[能力说明](validation.md)，
不再保留“尚未实现”的旧可行性表。

仍可研究：低码率饱和行为、减少拷贝、较低尺寸吞吐、逐片提前交付以及
共享单引擎的多编码会话。厂商多实例调度已有源码，当前开发驱动已加入
逐帧调度与上下文恢复，11-24已通过两路320×240各30帧完整软解与YUV检查；更高尺寸、并发内存与吞吐仍待验收。
H.264 B/CABAC/Main/High 的可用编码路径尚未发现，不能从没有独立
B-picture 命令进一步断言硅片绝对不支持。
这些需要固件/寄存器依据、逐帧内容、时延与服务并存实测；多分片NAL已经存在
不等于硬件能在整帧前交付。GE2D也需要Meson8b实际转换验证后才能集成。
