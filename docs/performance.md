# 阶段测量与 GE2D 验证

性能目标使用固定输入、完整帧数、相同质量与应用原命令。驱动计数记录
转换/拷贝的累计耗时，不能相加后当作总耗时；硬件、CPU 与工作队列会重叠。
本机历史数据不能代替候选模块的测量。

## 可复用测量入口

在具备取证/恢复条件的测试设备上，对解码、缩放、编码及完整命令分别运行
[measure-pipeline.py](../scripts/measure-pipeline.py)。脚本直接执行参数列表，
不经过 shell，不加载模块、不修改服务。输出必须是新文件；先运行命令，再
独立软件解码并核对完整帧数，超时或短输出失败。可提供相同 NV12 格式的
软件 framemd5 参考，进一步核对每幅图像；有损编码输出应另外比较 PSNR，
不能要求与输入哈希相同。

```sh
python3 scripts/measure-pipeline.py --stage decode \
  --report build/reports/decode.json --output build/reports/decode.nut \
  --frames 300 --media-seconds 10 --reference reference.md5 -- \
  ffmpeg -nostdin -v error -filter_threads 1 -c:v h264_v4l2m2m -i input.mp4 \
  -map 0:v:0 -an -pix_fmt nv12 -threads:v 1 -c:v rawvideo \
  -fps_mode passthrough build/reports/decode.nut
```

JSON 记录真实 argv、命令墙钟时间、子进程 CPU、采样到的最小普通内存/CMA、
decoder `convert_ns` 和 encoder `copy_ns` 等计数差值。软件验图在计时区间外，
失败日志与报告保留在指定报告目录。设备计数是全局值，其他会话会污染差值；
并存业务负载需单独记录。输出容器、磁盘 I/O 也在命令耗时内，不称为纯硬件耗时。
音频阶段可用原版 FFmpeg 的 `-benchmark` 单独测量，再用完整命令检查音视频
时间线；本脚本的独立内容检查入口当前针对视频。

## GE2D 第一个验收边界

[GE2D 驱动](../ge2d/meson8b-ge2d.c)独立使用标准 V4L2 M2M GREY、连续 NV12
和双平面 NV12M，MMAP/DMA-BUF 与 canvas；不接替当前 decoder 的 NEON 路径。S805 寄存器
窗口为 `0xc1106280`，使用已有 G2D 时钟和 GE2D reset，避免套用 AXG 的
直接地址寄存器。依据为 Hardkernel 固定提交
[`5aed95d35d252cafc75ce613a3a0052285662de2`](https://github.com/hardkernel/linux/tree/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d)。

驱动在 probe 时分配独立连续缓冲，硬件执行 512×32 字节图案的 big→little
endian 转换并逐字节核对，再检查 512×32→256×16 的常量平面过滤结果。
随后检查完整 NV12 的 Y 图案、两个不同色度分量，以及 NV12 常量平面缩小。
失败不注册视频设备；命令有 100 ms 上限，超时
后将 GE2D 保持在硬复位状态、拒绝新命令，先停止 DMA 再允许队列释放。
普通客户端使用 little→little，不借 FOURCC 暗示厂商字节序。NV12 走厂商
色度格式器与独立第二输出平面，不能用 GREY blit 插值交错的 U/V 字节。

默认运行包和 Overlay 不启用 GE2D。测试入口需要在未加载 codec overlay
的原设备树上生成带 `--ge2d` 的临时 overlay，并保留原安装/回退材料：

```sh
python3 scripts/make-overlay.py --tree /sys/firmware/devicetree/base \
  --ge2d --output ge2d-test.dts
# 在工作机编译；原5.15运行树可能没有/__symbols__，不要加-@。
dtc -I dts -O dtb -o ge2d-test.dtbo ge2d-test.dts
# 模块沿用临时overlay加载/恢复流程。
arm-linux-gnueabihf-gcc-11 -O2 -Wall -Wextra -Werror -static \
  tests/ge2d-copy.c -o build/ge2d-copy
# 用枚举得到的 GE2D 设备号；示例编号不代表固定设备。
./ge2d-copy /dev/video2 512 32 10
./ge2d-copy /dev/video2 1920 1088 10
./ge2d-copy /dev/video2 1920 1088 10 1280 720
./ge2d-copy /dev/video2 1920 1080 10 1920 1080 NV12M
./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12
GE2D_DMABUF=1 ./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12M
./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12M 2 2 1280 720
```

[客户端](../tests/ge2d-copy.c)比较每幅两个平面的全部可见像素、payload、
序列号和时间戳；行尾 padding 不当作图片。缩小模式使用逐帧不同的 Y 与
U/V 常量，不能代替有纹理视频的滤波质量验证。`GE2D_DMABUF=1` 通过另一个
不运行任务的 GE2D 句柄导出连续缓冲，导入测试队列；CPU 访问使用标准
DMA-BUF 同步，不假设其他导出器支持连续物理地址。

输出可独立协商为裁剪区域的 1–1/4，使用厂商 bilinear 表与有界 phase/
prescale 配置。`VIDIOC_S_SELECTION` 支持 OUTPUT crop，NV12 坐标/尺寸必须
为偶数；排队后拒绝格式或 crop 修改。两种 NV12 内存布局可互转，保留色彩、
量程与 transfer 信息，不进行色彩矩阵转换。输入/输出各平面验证容量、32位
DMA范围、对齐及重叠；错误输出清零两个平面的 payload。

当前通过实际命令、格式、裁剪、队列和客户端的主机 ASan/UBSan 回归与 ARM
构建。取消/重开、不同 DMA-BUF 导出器、纹理缩放及共享时钟/业务仍需实机。
11-25修正Overlay导出符号、NV12源色度排列及复制路径后，独立硬件验收通过：
GREY/NV12M原样复制与裁剪的全部可见字节、常量Y/U/V平面缩小、DMA-BUF
导入及时间戳通过。默认Overlay字节不变，GE2D仍只临时加载。两组纹理缩小在修正水平分段读取后完成独立bilinear质量比较，最低PSNR约
33.5 dB；不同插值实现不逐字节相同。跨导出器生命周期、完整标准工具链及
实际转码加速收益尚未通过。精确范围见
[重启后记录](validation-records.md#11-25-重启后格式修正与-ge2d-验收)。

## 显式 decoder 接入（生产默认关闭）

当前源码增加 decoder 的 `capture_ge2d=1` 参数，默认关闭。它要求带
`--ge2d` 的 Overlay 明确引用 `amlogic,ge2d`，GE2D 驱动已加载且 probe
自检通过；缺失/未就绪时 decoder probe 失败/延期，不悄悄改变配置。
设备链接及可选模块引用保持 GE2D 生命周期，用户态 M2M 和内核转换用同一
硬件锁串行；decoder 卸载后释放引用。runtime 仍在依赖模块卸载后才移除
挥发性 Overlay，不改 boot DT。

启用后的 CAPTURE 使用 `vb2_dma_contig`，可 MMAP/DMA-BUF，而不再使用普通
vmalloc 内存。帧池保留固件字节序，逐帧通过 GE2D 写可见 NV12M，按图片
自己的 reference/crop 几何转换；只清零行尾和对齐 padding。`ge2d_frames`
统计成功硬件转换，`convert_ns` 是墙钟耗时。MJPEG 和错误路径继续 CPU
转换；DMA-BUF CPU 访问成对同步，GE2D 超时后先复位停机再回退。

连续 CAPTURE 会消耗 CMA。默认20个 FFmpeg缓冲不保证能和固件帧池一起
容纳在64 MiB中；仅在测试设备先用 `-num_capture_buffers 1` 或直接客户端的
`CAPTURE_BUFFERS=1` 做完整图片检查，再测实际内存/回压。NEON 默认路径不
要求这些限制。显式接入原生客户端取消FNV诊断读取后，400幅精确可见像素通过，含H.264
1080p300幅和HEVC1080p30幅。300幅交付不读取图片约7.27 s；原版FFmpeg
低缓冲仍有短输出/断言失败，高缓冲又可能耗尽CMA。生产默认NEON，不将短样片通过扩大为全链部署通过。

最近相同1080p首60图，默认NEON下decode/null约1.827 s，加入CPU软件scale
约7.119 s，硬解→4 Mbps硬编约2.821 s；后者完整60/60及YUV质量通过，约
0.71×，未达1.3×。各阶段有不同的解码读前，不相减推导纯硬件耗时。
