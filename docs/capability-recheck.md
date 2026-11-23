# S805 能力否定结论复核（2026-11-23）

重新联网检索，并下载固定版本的芯片资料、厂商内核、编码库与官方
Linux/FFmpeg 源码后，**H.263、AVS、WMV3、HEVC tiles、多编码会话不能继续
归类为硬件做不到**。GE2D 也有继续实验的明确依据。HEVC 编码、Main10 和
H.264 高级编码工具则需分别判断，不能从同一个“不支持”标签推导。

本文描述开发可行性；当前运行能力仍以[验证指南](validation.md)为准。
本轮没有在设备上加载候选驱动，没有新的吞吐或稳定性验收。
[来源及本地复现](../evidence/capability-recheck-2026-11-23/README.md)保存
37 项输入的 URL、SHA256、固定提交和独立 C 检查结果。

## 重新判断

| 能力 | 复核后的结论 | 下一步需要证明什么 |
| --- | --- | --- |
| H.263 解码 | **旧“微码未定义”错误**。有 Meson8b 复用的微码、厂商入口、标准 V4L2 格式和官方 FFmpeg 包装器。 | 移植正确的固件选择、帧边界/时间戳和 drain 协议，再做实机逐帧比较。 |
| AVS JiZhun 解码 | **硬件和厂商实现有依据**；缺少当前标准接口/包装器不等于硬件不可能。 | 确认 UAPI 与上游应用落点，移植协议和样片验收；不借用错误 FOURCC。 |
| WMV3 simple/main 解码 | **属于集成缺口**。同一厂商 VC-1 驱动明确选择 WMV3 模式。 | Annex L/RCV 的序列及帧封装、尺寸、时间戳和 EOS；仅切换寄存器不够。 |
| HEVC tiles | **属于当前移植缺口**。S805 厂商路径有 tile 表、坐标、SAO 缓冲和 MPRED 更新。 | 有界 tile 几何、逐 slice 更新、地址/工作区边界、跨 tile 滤波及非均匀网格实机检查。 |
| GE2D 转换/缩放 | **值得继续验证**。厂商实现有 YUV、NV12、canvas 和源/目标 endian 控件。 | 先证实实际 SWAP64 行为及 Y/UV 精确输出，再接入 DMA 缓冲和可取消的异步任务。 |
| 多 H.264 编码会话 | **旧“N/A”过强**。厂商驱动支持多实例队列并串行执行硬件任务。 | 隔离每会话参考帧、canvas、SPS/PPS、码控和中断；测量切换开销、CMA 与总吞吐。 |
| HEVC 长期参考帧 | **未找到硬件不可能的依据**，但也没有确认当前微码的完整协议。 | 解析长期 POC/RPS、匹配微码输出、生命周期与 wrap 样片。 |
| H.264 Main/High、CABAC、B 帧编码 | **当前路径没有实现；硅片绝对不支持尚未证明**。 | 找到实际工作的微码/host 路径和输出工具证据；头文件常量或 profile 标签均不足以证明。 |
| HEVC Main10 解码 | **继续保持不支持**。S805 手册列 Main，未找到适用于此芯片的可靠 Main10 路径。 | 需要同芯片硬件及微码证据；10-bit 字段与后代芯片实现不能替代。 |
| HEVC 硬编码 | **仍无可实现的 S805 路径**。公开资料的编码器是 H.264/JPEG；已找到的 HEVC 编码库依赖另一硬件 IP。 | 除非出现同芯片寄存器/微码/实机反证，否则不投入假定移植。 |
| 严格 CBR、逐 slice 提前交付 | **当前实现缺失，不能笼统说硬件不可能**。 | 码控需区分微码、host 算法和网络发送；提前交付需确认硬件发布边界及标准客户端语义。 |

## 直接纠正的源码依据

固定厂商树为 Hardkernel
[`5aed95d35d252cafc75ce613a3a0052285662de2`](https://github.com/hardkernel/linux/tree/5aed95d35d252cafc75ce613a3a0052285662de2)。
完整 tree 响应未截断；`m8b/ucode` 是指向 `../m8/ucode` 的 symlink。
因此只搜索文件名是否含 `h263`，或只看 `m8b` 目录，没有覆盖实际微码定义。

H.263 的 `h263_mc[]` 在
[`m8/ucode/mpeg4/vmpeg4_mc.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/m8/ucode/mpeg4/vmpeg4_mc.c#L1388)
中定义；[`vmpeg4.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/vmpeg4.c#L833)
明确按 `VIDEO_DEC_FORMAT_H263` 加载它。独立 C 编译与本项目提取器得到相同
8972 字节，研究 IMEM 镜像补零到 16 KiB；这只证明来源和字节布局。
官方 FFmpeg [n7.0.2](https://github.com/FFmpeg/FFmpeg/blob/n7.0.2/libavcodec/v4l2_m2m_dec.c#L265)
和 [n8.1.3](https://github.com/FFmpeg/FFmpeg/blob/n8.1.3/libavcodec/v4l2_m2m_dec.c#L265)
均有 `h263_v4l2m2m`，且格式表映射 `AV_CODEC_ID_H263`。

AVS 微码由
[`vavs_mc.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/m8/ucode/avs/vavs_mc.c)
包含 `avs_linux.h`，[`vavs.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/vavs.c#L776)
加载它。独立 C 比对得到 9024 字节。
[S805 原手册](https://github.com/DeciHD/amlogic_docs/blob/58fe4e1f878a1b2c4899eca6ea52e4cb6ec7cea2/s805/S805_Datasheet%20V0.8%2020150126.pdf)
第 9 页列出 AVS JiZhun 与 WMV/VC-1 SP/MP/AP。
厂商 [`vvc1.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/vvc1.c#L836)
区分 WMV3 (`AV_SCRATCH_4=0`) 与 WVC1 (`=1`)。
Linux 有 [VC1 Annex L 格式](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-compressed.html)，
但核对的 FFmpeg V4L2 格式表没有 WMV3/Annex L 映射，不能把 WVC1 包装器
直接当成 WMV3 支持。

多编码实例的依据是
[`encoder.h`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/encoder.h#L149)
的上限和每实例状态，以及
[`create_encode_work_queue()`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/encoder.c#L2318)、
`encode_monitor_thread()` 的排队/执行和 `amvenc_avc_start_cmd()` 的状态恢复。
这是**共享一个引擎的多会话调度**，没有增加物理编码器或总计算能力；厂商的
8 个实例上限也不代表原 64 MiB CMA 设备能同时编码 8 路 1080p。

## GE2D 和 tiles 的可执行复核

厂商 [`ge2d_hw.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d/ge2d_hw.c#L153)
写入 `GE2D_GEN_CTRL2` 的源 endian bit 7、目标 endian bit 23。
[`ge2dgen.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d/ge2dgen.c#L54)
可为同类 YUV 输入/输出关闭矩阵，并配置 NV12 第二输出平面。
源码中若干“only works on m6”注释与实际 `>= MESON6` 编译分支不一致，
应交给 S805 实测解决，不能任选一方作为硬件结论。

本地检查直接提取厂商结构/函数，以真实 Meson8b CPU 常量编译，对寄存器写入
使用数组替身。ASan/UBSan 检查证实软件配置能选择这些字段。**没有模拟
GE2D 像素运算，也没有证明 endian 位等价于 VDEC 的 SWAP64。**
先把 Y 平面和交错 UV 平面当作 S8 字节平面，关闭矩阵/缩放，比较字节图案；
确认后再测正常 NV12 与缩放。DMA blit 仍发生内存搬运，不能称为完全无拷贝。
当前 Meson8b CAPTURE 使用 vmalloc/MMAP，接入硬件输出还需 DMA 可访问的
目标缓冲及寿命/容量设计；编码器支持 DMA-BUF 不会自动补上这个缺口。

官方 [Linux 5.15 GE2D 驱动](https://github.com/torvalds/linux/blob/v5.15/drivers/media/platform/meson/ge2d/ge2d.c#L40)
明确缺少 YUV/缩放/色彩转换，且只匹配 AXG，不能改 compatible 就作为
S805 转换器。本次也核对了
[当前主线固定提交](https://github.com/torvalds/linux/blob/b6a9fa34ce4187cb69d3c588a4bbffc31ce5db8f/drivers/media/platform/amlogic/meson-ge2d/ge2d.c#L40)，
这些边界仍存在。当前 NEON 路径保留为独立内容参考和回退。

S805 厂商 [`vh265.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/vh265.c#L1719)
有 `update_tile_info()`/`get_tile_index()`，并在 decode slice 路径调用。
当前项目却拒绝 `tiles_flags & 1`，MPRED 只设置一块整图 tile。
还必须移植 SAO 地址、MV row jump、tile 切换和滤波控制，不能单独删除拒绝条件。

独立执行厂商 tile 几何函数又发现一个需要核对的边界：30 CTU 宽、4 列均匀
tile 时，厂商算法得到 `7/7/7/9`，官方
[FFmpeg PPS 算法](https://github.com/FFmpeg/FFmpeg/blob/n7.0.2/libavcodec/hevc_ps.c#L1650)
得到 `7/8/7/8`。本轮只复现 host 算术差异；还需确认微码 RPM 参数语义，
不能把旧厂商代码原样搬入并宣称所有网格可用。

## 仍没有充分支持证据的能力

S805 手册第 9 页把 HEVC Main 列在解码，编码只列 H.264/JPEG。
新检索找到的
[`khadas/libencoder`](https://github.com/khadas/libencoder/tree/bfee62dad4f7ebb6d1705df8522da871dcad861e)
也包含 HEVC 代码，但
[`vdi.c`](https://github.com/khadas/libencoder/blob/bfee62dad4f7ebb6d1705df8522da871dcad861e/amvenc_265/EncoderAPI-HEVC/hevc_enc/vpuapi/vdi.c#L25)
打开 `/dev/HevcEnc`，`wave4.c` 驱动 WAVE420L；这不是 S805 HCODEC。
S805/S805X/S805X2、Meson8/Meson8b 和 GX 后代必须区分。
HEVC 编码继续按[既有硬件依据](encoding-feasibility.md)不提供支持。

Main10 的否定证据比 HEVC 编码弱一些：原手册只承诺 Main，厂商通用
`bit_depth` 字段只是报告参数，未找到同芯片可靠 Main10 输出路径。
保留当前 8-bit 检查，不用解除检查或截断像素假装支持。

H.264 B 帧的旧推理也不充分：NON_IDR 是 NAL 分类，不等于 P slice。
厂商头文件确有 `B_Slice`/`HENC_B_PICTURE`，但核对的 `encoder.c` 没有使用
这些常量，编码库的
[`AML_HWEncoder.cpp`](https://github.com/khadas/libencoder/blob/bfee62dad4f7ebb6d1705df8522da871dcad861e/amvenc_264/bjunion_enc/AML_HWEncoder.cpp#L99)
只选择 I/P。库中的 profile 枚举、CABAC 错误常量和通用 SPS parser 同样
不能证明 CABAC/High 编码。结论改为“当前可用路径没有”，继续保留当前
Baseline 控件；只修改 SPS profile 标签不构成这些工具的实现。

长期参考帧需要[HEVC 长期 RPS/POC 语义](https://www.itu.int/rec/T-REC-H.265)
和同芯片微码输出证据。当前 `CUR_RPS` 主要按相对 POC 构建列表，SPS
检查器也没有完成长期参考字段；本轮未找到能证明芯片绝对不能做的依据。

严格 CBR 需区分“不提供”与“不可能”：当前 VBR、单帧 QP 和超预算失败不能
保证任何内容都满足目标；低目标的 QP32 饱和有实测依据。后代驱动的宏块
CBR 表在
[LibreELEC 厂商驱动](https://github.com/LibreELEC/linux-amlogic/blob/6d8fbb4ee61a7779ac57b5961e076f0c63ff8b65/drivers/amlogic/amports/encoder.c#L1557)
按 GXTVBB 及以后芯片启用，不能无条件复制到 S805。软件重编码、
填充与 HRD/发送调度属于额外设计，也不自动解决画质/吞吐问题。
逐 slice 提前交付则没有找到经过验证的 S805 发布协议；多个 slice NAL、
STREAM_WR_PTR 或 FIFO 写指针不等于稳定可消费边界。当前
[V4L2 H.264 AU 语义](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-compressed.html)
和官方客户端兼容性也需要保留。

## 开发入口和验收顺序

用户在复核后选择先做 P0 可靠性。GE2D、HEVC tiles、H.263 等方向已分别
保存为后续推荐 issues，详见[路线图](../ROADMAP.md)；当前暂停扩展实现。
GE2D 与 tiles 分别关联 CPU 开销和 HEVC 兼容性，恢复时先做独立实验；
H.263/WMV3 补固件与封装，AVS/多实例先明确标准接口和资源预算。

每个候选先有本地边界测试和交叉构建，再在可保留现场且可恢复的设备上
验证逐帧内容、EOS、取消、重开、DMA 生命周期、共享时钟和业务并存。
生产持续压力实验的暂停状态保持；源码可行性不会自动解除可靠性验收门槛。
