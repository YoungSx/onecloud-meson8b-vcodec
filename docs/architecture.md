# 架构与数据路径

项目在 Linux 5.15.79 的标准媒体框架上增加三个外部模块。目标内核已有
Overlay、canvas、时钟和 V4L2 M2M/vb2 支持，无需打开 staging 或替换启动
内核。可应用源码补丁为 [patches/series](../patches/series)，它增加同一实现
的模块 Kconfig/Kbuild 入口。

| 模块 | 职责 |
| --- | --- |
| `meson8b-runtime.ko` | 应用/移除挥发性 Overlay，持有共享 DOS 寄存器区域，提供 IRQ 安全更新锁 |
| `meson-vdec.ko` | stateful V4L2 解码；VDEC1 处理 H.264/旧 codec，独立 HEVC 核处理 Main 8-bit |
| `meson-venc.ko` | HCODEC H.264/JPEG 编码，共用一个单 open 的 V4L2 设备 |

DOS 控制位由多个引擎共享。模块使用统一 owner/lock 和 PMU regmap 更新
掩码，避免整个寄存器写入干扰另一引擎。依赖模块卸载后 runtime 才能移除
Overlay。HCODEC 使用固定 fclk_div4/2（318.75 MHz），HEVC 使用固定
fclk_div5（255 MHz），避免重编程以太网依赖的共享 MPLL。

## 解码与编码内存

Meson8b canvas 没有 GX 字节序转换控制。解码器持有帧池：槽数按码流需求
固定（H.264 为参考帧数加 2，HEVC 为 DPB 大小），每槽一块固件字节序的
CMA 块，canvas 只指向槽；用户申请的 CAPTURE buffer 只接收转换后的 NV12M
拷贝，数量与尺寸由应用决定。槽在固件不再引用后、同一把帧池锁内释放，
转换也在这把锁内进行。

v0.3 的每 CAPTURE buffer 独立 shadow 做不到这一点：resume 为分辨率变化
重建 shadow 时 `vunmap` 缓存别名，并发中的转换仍从旧别名读，实机在
`meson8b_swap64_neon` 上访问已撤销映射故障；STREAMOFF 释放缓冲与在跑的
转换之间也有同类窗口。帧池把分配、释放、转换都串行化在帧池锁内，并在
分辨率变化时整体重建，重建失败按会话错误上报。

每幅图在解码时记下自己的几何（可见尺寸、参考尺寸、裁剪偏移），交付时按它
转换，而不是按会话当时的值。分辨率变化后，旧码流排在重排缓冲里的尾帧仍按
旧尺寸交出，最后一幅标 LAST 结束旧码流，客户端据此重新协商。

不能在硬件仍作运动补偿参考的缓冲上直接更改布局。转换用内核态 NEON
（独立编译单元，`kernel_neon_begin()` 包围，非 SIMD 上下文回退标量），
1080p 约 7–8 ms；HEVC 裁剪窗口按行有界拷贝，左偏移非 8 字节倍数时走
字节循环。输出平面按 canvas 的 32 字节行宽补零。

编码输入由格式表描述：每种格式对应 MFDIN 输入模式、平面布局、字节序
（`REG9_ENDN` 置换每个 64 位字的字节）以及 RGB 转换矩阵。帧按厂商 LOCAL_BUFF
布局拷入 staging，每个平面按 canvas 宽度和 16 行对齐高度排列，可见区域以外
保持为零。staging 只按当前格式的实际布局分配，1080p NV12 比厂商私有 DCT
区节约约 3.8 MiB CMA，使已验证参考帧配置的 H.264 并发转码可行。
MMAP 缓冲仍为 vmalloc，排队帧不占 CMA；连续的 DMA-BUF NV12/NV21 帧直接作为
canvas 交给 MFDIN，其他 DMA-BUF 经 CPU 映射拷入 staging。

微码只按主机给出的 QP 量化。驱动在每帧开始前于控件锁内选择帧类型、QP 和
分片，编码完成后用实际输出字节更新码率控制；QPPICTURE（PPS 的 pic_init_qp）
固定为 26。微码只在 SPS 中写底部裁剪，驱动按需在会话开始时重写一次 SPS，
补入右侧裁剪；会话内 SPS/PPS 拼在每个 IDR 之前。码流缓冲为 1 MiB，写满时
VLC 停在末尾不越界，驱动约 150 ms 内报错。端到端零拷贝尚未实现。

## 状态与帧尾

HEVC 固件 RPM 缺少裁剪窗口，bounded SPS parser 补充可见尺寸与 DPB 信息。
CAPTURE 回压时暂停 slice；参考帧按 RPS 生命周期保留。固件 drain 完成后
送空 LAST，EOS 事件在 LAST 消费时发出。损坏流的五秒停滞 watchdog 终止
当前队列，后续会话可恢复。

Meson8b H.264 最后 drain 批次可能按解码顺序返回，驱动按输入 PTS 排序，
同时保留 packet offset 用于时间戳匹配。独立 FFmpeg 补丁修正 CAPTURE 全部
暂在用户态时的提前 EOF，以及 POLLERR 的队列状态处理。

此架构面向原在役内核。[上游结论记录](linux-media-rfc.md)列出需要进一步
处理的静态 DT 与共享电源域 owner。编译侧主线移植已完成：`patches/research/`
以 v7.3.4 为基准，可编译 `meson-vdec.ko`、全部 ARM DTB 并通过
`dtbs_check`，采集字节序转换在 `0013` 中实现、其 NEON 主体在 `0014` 中；
S805 实机验证仍缺，上游合入不在本项目范围内（见
[移植记录](../evidence/mainline-2026-11-16/PORT.md)）。
