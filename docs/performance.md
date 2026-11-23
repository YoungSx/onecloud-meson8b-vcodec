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

[GE2D 驱动](../ge2d/meson8b-ge2d.c)独立使用标准 V4L2 M2M GREY 字节平面，
MMAP/DMA-BUF 与 canvas；不接替当前 decoder 的 NEON 路径。S805 寄存器
窗口为 `0xc1106280`，使用已有 G2D 时钟和 GE2D reset，避免套用 AXG 的
直接地址寄存器。依据为 Hardkernel 固定提交
[`5aed95d35d252cafc75ce613a3a0052285662de2`](https://github.com/hardkernel/linux/tree/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d)。

驱动在 probe 时分配独立连续缓冲，硬件执行 512×32 字节图案的 big→little
endian 转换并逐字节核对。失败不注册视频设备；命令有 100 ms 上限，超时
后将 GE2D 保持在硬复位状态、拒绝新命令，先停止 DMA 再允许队列释放。
普通 GREY 客户端使用 little→little，不借 FOURCC 暗示厂商字节序。

默认运行包和 Overlay 不启用 GE2D。测试入口需要在未加载 codec overlay
的原设备树上生成带 `--ge2d` 的临时 overlay，并保留原安装/回退材料：

```sh
python3 scripts/make-overlay.py --tree /sys/firmware/devicetree/base \
  --ge2d --output ge2d-test.dts
# 在工作机编译 overlay 和模块，沿用临时 overlay 加载/恢复流程。
arm-linux-gnueabihf-gcc-11 -O2 -Wall -Wextra -Werror -static \
  tests/ge2d-copy.c -o build/ge2d-copy
# 用枚举得到的 GE2D 设备号；示例编号不代表固定设备。
./ge2d-copy /dev/video2 512 32 10
./ge2d-copy /dev/video2 1920 1088 10
```

[客户端](../tests/ge2d-copy.c)比较每幅完整字节、大小和时间戳。需要进一步
覆盖取消/重开、DMA-BUF、两平面图案和共享时钟/业务，再决定接入视频输出、
裁剪与 NV12 缩放。当前仅有主机 MMIO/错误注入和 ARM 编译；没有 GE2D
实机像素或性能验收，不能宣称已加速转码。
