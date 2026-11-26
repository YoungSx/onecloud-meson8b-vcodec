# 架构与数据路径

本文描述当前main实现；各版本的实机范围见[验证记录](validation-records.md)，
在役来源与最近健康核对见[部署快照](rollout-status.md)，未部署候选不继承旧版验收。

项目在 Linux 5.15.79 的标准媒体框架上增加三个基础外部模块及可选GE2D模块。目标内核已有
Overlay、canvas、时钟和 V4L2 M2M/vb2 支持，无需打开 staging 或替换启动
内核。可应用源码补丁为 [patches/series](../patches/series)，它增加同一实现
的模块 Kconfig/Kbuild 入口。

| 模块 | 职责 |
| --- | --- |
| `meson8b-runtime.ko` | 应用/移除挥发性 Overlay，持有共享 DOS 寄存器区域，提供 IRQ 安全更新锁 |
| `meson-vdec.ko` | stateful V4L2 解码；VDEC1 处理 H.264/旧 codec，独立 HEVC 核处理 Main 8-bit |
| `meson-venc.ko` | HCODEC H.264/JPEG 编码；标准 M2M 逐帧多会话调度 |
| `meson8b-ge2d.ko` | 可选的独立V4L2 M2M复制/裁剪/缩小及decoder转换供应者；默认运行包不启用 |

DOS 控制位由多个引擎共享。模块使用统一 owner/lock 和 PMU regmap 更新
掩码，避免整个寄存器写入干扰另一引擎。依赖模块卸载后 runtime 才能移除
Overlay。解码器对文件句柄开放能力查询，跨句柄用独立会话锁串行硬件启停，
第二路 STREAMON 在引擎占用时返回 EBUSY。HCODEC 使用固定 fclk_div4/2（318.75 MHz），HEVC 使用固定
fclk_div5（255 MHz），避免重编程以太网依赖的共享 MPLL。
VDEC1 新候选通过显式 `vdec_1_div` 句柄设置分频，只有父频率恰为请求
频率两倍才执行；父频率不匹配时拒绝，不允许 CCF 自动重选/重调共享父钟。
固定父钟已通过有界实机检查；新驱动与匹配 overlay 已部署，旧 overlay 不含这个句柄。

编码多会话实现将“打开句柄”和“硬件 owner”分开，V4L2 M2M
逐帧选择会话，额外 session mutex 串行硬件切换与资源清理。每个上下文保留
自己的 CMA、参考 canvas 角色、SPS/PPS、固件和码控；换 owner 时停止/同步
旧 IRQ，再重新加载当前会话的微码及寄存器，下一图片命令重写自己的帧号/
POC 与参考地址。释放非 owner 只清理该上下文，关闭 owner 先停硬件。
最多8个句柄不等于8路1080p：内存按需分配、总吞吐仍共享。已部署版本的
两路720p/两路1080p各12幅内容通过，任意并发、实时吞吐和广泛切换仍未
保证，见[追平记录](codec-parity.md)。

## 解码与编码内存

Meson8b canvas没有GX字节序转换控制。解码器持有内部帧池，每槽一块固件
字节序的CMA块，canvas只指向槽。H.264按参考帧与输出余量分配；HEVC按
当前AU的SPS声明与重排需求取`min(16, max(DPB, reorder + 1) + 1)`，保留
异步交付余量。用户申请的CAPTURE buffer只接收转换后的NV12M
拷贝，数量与尺寸由应用决定。IRQ 只登记完成图片并调度工作；转换期间槽
仍在待交付队列中，固件不能回收。队列锁只保护记账，独立交付锁串行转换与
重建/释放，使 IRQ 可在拷贝时登记其他槽。

v0.3 的每 CAPTURE buffer 独立 shadow 做不到这一点：resume 为分辨率变化
重建 shadow 时 `vunmap` 缓存别名，并发中的转换仍从旧别名读，实机在
`meson8b_swap64_neon` 上访问已撤销映射故障；STREAMOFF 释放缓冲与在跑的
转换之间也有同类窗口。帧池把分配、释放、转换都串行化在交付锁内，并在
分辨率变化时整体重建，重建失败按会话错误上报。

每幅图在解码时记下自己的几何（可见尺寸、参考尺寸、裁剪偏移），交付时按它
转换，而不是按会话当时的值。分辨率变化后，旧码流排在重排缓冲里的尾帧仍按
旧尺寸交出，最后一幅标 LAST 结束旧码流，客户端据此重新协商。

可选GE2D接入由Overlay的明确引用和`capture_ge2d=1`启用，默认仍是NEON。
MMAP CAPTURE使用普通缓存vmalloc内存，一张coherent中转面供GE2D复用，
完成后复制给当前缓冲；连续DMA-BUF导入经vb2-dma-contig验证后直接写入。
帧池保持原参考图像，不原地改其字节序；显式GE2D模式不建立cached别名，
CPU回退直接读取coherent映射。默认NEON仍保留已验收的cached读取路径。
V4L2设备链接/模块引用保持供应者，硬件锁串行用户态与内核请求。

当前未实机的缩放候选按图片保存的reference/crop几何读取源，CAPTURE可
独立协商较小输出；尺寸变化后重新协商，已有缓冲时S_FMT返回EBUSY。
缓冲检查、DMA-BUF同步/映射失败直接报错；MJPEG、暂存面分配或GE2D引擎
故障可在原尺寸输出时回退CPU，缩放输出禁止该回退。转换失败返回ERROR和
零payload，同时保留时间戳、序号、LAST/EOS及source-change序号重置。
独立GE2D M2M等待导入面已有的读写fence，但完成/所有权仍通过QBUF/DQBUF
交接。候选的真实缓存/缩放/回压与吞吐范围见[性能入口](performance.md)。

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
`ca0d320`候选将此路径扩展到布局匹配、完整16行宏块高的标准单平面NV12，
省去输入staging复制；staging分配保留，MMAP和不匹配布局继续复制，见
[交接边界](performance.md#单平面-nv12-的编码交接增量)。

微码只按主机给出的 QP 量化。驱动在每帧开始前于控件锁内选择帧类型、QP 和
分片，编码完成后用实际输出字节更新码率控制；QPPICTURE（PPS 的 pic_init_qp）
固定为 26。微码只在 SPS 中写底部裁剪，驱动按需在会话开始时重写一次 SPS，
补入右侧裁剪、VUI 色彩信息与协商帧周期；未知颜色保持 unspecified，流中
改变帧周期返回 EBUSY，需 STREAMOFF 后重设。SPS/PPS 拼在每个 IDR 之前。码流缓冲为 1 MiB，写满时
VLC 停在末尾不越界，驱动约 150 ms 内报错。端到端零拷贝尚未实现。

码率控制的待应用配置与当前帧历史分开；取帧时应用配置并消费 force-key，
当前帧完成时按快照记账。GOP1/密集 IDR 用 I 模型闭环。溢出恢复有独立
QP 下限和会话失败上限，第三次溢出停止队列，预排队帧不再启动硬件。
debugfs 通过自旋锁快照暴露实际媒体窗口码率与饱和情况，详见
[码率控制](rate-control.md)。

## 状态与帧尾

HEVC 固件 RPM 缺少裁剪窗口，bounded SPS parser 补充可见尺寸与 DPB 信息。
CAPTURE 回压时暂停 slice；参考帧按 RPS 生命周期保留。固件 drain 完成后
送空 LAST，EOS 事件在 LAST 消费时发出。损坏流的五秒停滞 watchdog 终止
当前队列，后续会话可恢复。

Meson8b H.264 最后 drain 批次可能按解码顺序返回，驱动按输入 PTS 排序，
同时保留 packet offset 用于时间戳匹配。参数变化先暂停回收，丢弃旧帧池
槽号和清空回收邮箱，再启用新表，避免旧通知提前释放新图片。EOS 仅在带专用结束标记的 LAST
出队时发布，分辨率切换的 LAST 不产生 EOS。当前适配原版 FFmpeg；历史
可选 bug 补丁不进入官方兼容镜像。

此架构面向原在役内核。[上游结论记录](linux-media-rfc.md)列出需要进一步
处理的静态 DT 与共享电源域 owner。编译侧主线移植已完成：`patches/research/`
以 v7.3.4 为基准，可编译 `meson-vdec.ko`、全部 ARM DTB 并通过
`dtbs_check`，采集字节序转换在 `0013` 中实现、其 NEON 主体在 `0014` 中；
S805 实机验证仍缺，上游合入不在本项目范围内（见
[移植记录](research-validation.md)）。

研究线 `0016` 已移植独立帧池，`0019` 同步 IRQ/EOS 与旧格式，`0020–0022`
接入 HEVC、固定父钟及共享 genpd。27 个补丁构建与 schema 通过；新内核和
跨板运行不能继承旧内核的实机验收，见[通用性边界](portability.md)。
