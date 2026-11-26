# 编解码使用

先按[安装指南](installation.md)加载设备。编号按注册顺序分配；原验收
设备的 decoder 为 `/dev/video0`，encoder 为 `/dev/video1`。

## 设备枚举

```sh
/opt/meson8b-vcodec/current/tools/v4l2-ctl --list-devices
/opt/meson8b-vcodec/current/tools/v4l2-ctl -d /dev/video0 --list-formats-out --list-formats --list-ctrls
/opt/meson8b-vcodec/current/tools/v4l2-ctl -d /dev/video1 --list-formats-out --list-formats --list-ctrls
```

枚举只证明 ioctl 与格式暴露，完整帧数/内容才是解码验收。FFmpeg 的
v4l2m2m 列表也仅证明包装器存在。

## H.264 硬解到硬编

使用包含对应包装器的官方原版 FFmpeg。解码器自己持有帧池，应用申请的 CAPTURE 数不影响
硬件解码内存，默认参数即可：

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -i input.mp4 \
  -an -c:v h264_v4l2m2m -b:v 0 -fps_mode passthrough output.mp4
```

`-i` 前选 decoder，后选 encoder。`-b:v 0` 使用固定 QP 26；非零 bitrate
启用码率控制（见下节）。CAPTURE 缓冲仍由应用按需申请（1–32，FFmpeg
默认 20），只占普通内存。原 64 MiB CMA 下十轮 60 帧 B 帧输入完整输出，
逐帧 PSNR 最低 45.03 dB、平均 45.82 dB，见[验收](validation.md)。

已部署版本每次一个 decoder 和一个 encoder，应用侧排队。当前源码新增
单 HCODEC 多句柄逐帧调度，尚未部署/实机验收，每会话内存与重载开销独立。
`-vf scale=640:-2` 可缩图，
缩放由 CPU 完成；数据路径仍有字节序转换和输入拷贝。

## 帧哈希验收

哈希/原始视频输出应显式限制滤镜和输出编码线程，避免异步输出长期持有
所有采集 buffer：

```sh
ffmpeg -nostdin -filter_threads 1 -c:v h264_v4l2m2m -i input.mp4 \
  -pix_fmt nv12 -threads:v 1 -f framemd5 hardware.md5
python3 scripts/verify-hashes.py software.md5 hardware.md5
```

此处 `-threads:v 1` 位于输入之后，约束 rawvideo 输出编码器；不是给驱动
增加私有控件，也不需改变硬件帧池。原版 7.0.2 的默认多线程哈希输出在
原模块及候选模块上都曾提前结束，单线程配置用同一参考逐帧核查。
正常退出码不足以证明交付完整，仍须核对所有帧数与哈希。

## H.264 编码控件

| 控件 | 行为 |
| --- | --- |
| `BITRATE_MODE` | 仅 VBR；CBR/CQ 返回错误；此控件单独不启用预算 |
| `BITRATE_PEAK` / `H264_CPB_SIZE` | 直接 V4L2 客户端成组启用媒体时间预算；峰值 bps、容量 KiB，超预算终止，详见码控指南 |
| `H264_PROFILE` / `H264_LEVEL` | 仅枚举微码已验证的 Baseline / Level 4.0 |
| `BITRATE` + `FRAME_RC_ENABLE` | 二者都非零时跟踪目标平均码率：有界误差桶、独立 P/IDR 模型，普通 P QP 的预测上限为 32（受用户范围限制） |
| `H264_I_FRAME_QP` / `H264_P_FRAME_QP` | 关闭码率控制时的固定 QP（默认 26）；码率控制时 P QP 为起点，二者差值为 IDR 偏移 |
| `H264_MIN_QP` / `H264_MAX_QP` | 限制所有 QP；同一次 `S_EXT_CTRLS` 内按新的一对校验，min>max 返回 `ERANGE` |
| `GOP_SIZE` | IDR 间隔；0 表示只有首帧和强制关键帧为 IDR |
| `FORCE_KEY_FRAME` | 下一帧为 IDR |
| `HEADER_MODE` | 只有 `JOINED_WITH_1ST_FRAME`；SPS/PPS 在每个 IDR 前 |
| `MULTI_SLICE_MODE` / `MULTI_SLICE_MAX_MB` | `MAX_MB` 按整行宏块向下取整；分片随整帧交付，没有逐片低延迟 |
| `H264_VUI_SAR_ENABLE` | 新源码控制SPS像素宽高比写入，默认关闭；尚未部署 |
| `H264_VUI_SAR_IDC` | 标准菜单0–17；0为未指定，17转换为码流Extended_SAR的255 |
| `H264_VUI_EXT_SAR_WIDTH` / `HEIGHT` | 扩展SAR的像素宽/高，整数1–65535，默认各1；内核按整数控件规则规范化 |

码率/QP/GOP控件在取下一帧时生效，当前帧按旧配置记账；STREAMOFF/ON新会话重置历史。
SAR四项可通过一次S_EXT_CTRLS原子设置。SAR与帧周期在流开始前配置；流内
改变SAR返回EBUSY，相同配置重复写入可用。码流为Baseline；当前源码在
SPS/VUI中写入裁剪、已知色彩、协商帧周期和可选SAR。
官方包装不传递的未知颜色信息保持未指定，详见[能力边界](validation.md)。
FFmpeg 默认 bitrate 为 200 kbit/s；码率控制达不到的目标会停在 QP 32，
需要更低码率时应降低分辨率或帧率。单帧超过 1 MiB 码流缓冲（例如 1080p
在很低 QP 下）时该帧返回错误，下一帧为恢复 IDR，日志提示所用 QP；累计
第三次溢出或 MAX_QP 仍溢出时终止两队列，须 STREAMOFF 后重新启动。
详细语义与 debugfs 字段见[码率控制](rate-control.md)。上游 FFmpeg V4L2
包装未映射 `maxrate/bufsize`，传入这两个参数也不构成峰值约束。

原生客户端的候选接口示例（704×576、SAR 12:11，显示为4:3）：

```sh
./build/encode-test -c sar-enable=1,sar-idc=2 \
  /dev/video1 output.h264 reference.nv12 704 576 12 NV12 26 H264
```

所核对官方FFmpeg 7.0.2的M2M编码包装器未发送这四个SAR控件；原版工具可
用 `h264_metadata=sample_aspect_ratio=12/11` 写入目标输出的SAR。具体命令
和完整性边界见[官方FFmpeg适配](ffmpeg-compatibility.md#编码-sps-像素比例)。

## 编码输入格式

| 格式 | 说明 |
| --- | --- |
| NV12（连续平面）、NV12M、NV21M、YUV420M | 4:2:0；FFmpeg 的 `nv12`/`nv21`/`yuv420p` |
| YUV444M、YUYV、UYVY | 由硬件下采样为 4:2:0；FFmpeg `yuyv422`、`uyvy422` |
| RGB24 | 宽度不超过 1376；更宽的行硬件读取错乱 |
| XRGB32、XBGR32、RGBX32、BGRX32 | 全宽度；FFmpeg `0rgb`/`bgr0` 用旧名 RGB32/BGR32 也可 |

RGB 视为全量程，转换为有限量程 YCbCr：OUTPUT 格式 `ycbcr_enc` 为 709（或由
colorspace 默认为 709）时用 BT.709，否则 BT.601；CAPTURE 格式据此报告。
三字节 BGR 不受支持。JPEG 保持全量程输入，不接受 RGB 输入。宽度不是
16 的倍数，或为 1–3、65 个宏块宽时，驱动在内部补齐宏块并在 SPS 中裁掉，
解码得到原尺寸。

## 官方 FFmpeg 软件缩放

编码器枚举标准连续 `NV12`（一个内存平面）以及 `NV12M`（两个内存平面）。
官方 FFmpeg 的 `scale` 输出可直接使用连续布局，驱动按协商行宽拆分 Y/UV
进入同一硬件路径，不依赖 FFmpeg 共享多平面缓冲补丁。

```sh
ffmpeg -c:v h264_v4l2m2m -i input.mp4 -vf scale=1280:-2 \
  -c:v h264_v4l2m2m -pix_fmt nv12 -b:v 2M -g 60 output.mp4
```

H.264 profile/level 控件仅声明微码输出的 Baseline / Level 4.0；其他值被拒绝。
软件缩放在 Cortex-A5 上可能成为主要瓶颈，不能由硬件编码名称推断实时性能。

## DMA-BUF 输入

已部署的同步修正在每幅编码前等待导入输入的生产者fence（每平面最多1 s），重复直接
DMA读取前重新同步缓存；CPU回退的结束同步失败会返回错误缓冲。导出器
未提供隐式fence时，应用仍须在QBUF前等待生产者完成，并在DQBUF后再复用
输入。CPU写入导入缓冲需成对使用DMA_BUF_IOCTL_SYNC的WRITE START/END。
同步实现见[事务记录](validation-records.md#11-26-编码-dma-buf-同步事务)，当前包的有限
导入内容检查见[部署验收](validation-records.md#11-26-main-20220a8-部署验收)。

OUTPUT 队列接受 MMAP 和 DMA-BUF。NV12M/NV21M 的两个平面若各自映射为
一段连续地址、8 字节对齐且行宽等于驱动报告的 bytesperline，硬件直接读取，
不经过拷贝；例如 meson-drm 的 dumb buffer。其他导入（如 lima GEM 的分散
页）由 CPU 读入 staging，结果相同但通常比 MMAP 慢。直接路径会读取缓冲中
可见区域以外的对齐行，客户端应把这些行清零或保持一致。
`/sys/kernel/debug/meson-venc/` 下的 `direct_frames`、`copied_frames` 和
`copy_ns` 记录两条路径的帧数和拷贝耗时。

`ca0d320`候选新增标准单内存平面NV12的直接读取：连续且8字节对齐的
DMA-BUF、协商行宽等于硬件canvas行宽、可见高度为16行倍数且完整Y/UV落在
32-bit DMA范围内时，UV地址为`base + bytesperline * height`。720p可以满足；
1080高、396高等不完整宏块行仍复制，以免把UV当作Y padding或读过色度末尾。
不匹配布局、分散导入和MMAP保持复制，短payload/fence错误仍失败。staging
分配保留供回退，不能把省去复制说成释放了这块CMA。

验证客户端的`NV12-1`表示该单平面格式，原`NV12`名称继续表示双平面NV12M：

```sh
./encode-test -d dumb -p motion /dev/video1 output.h264 /dev/null \
  1280 720 8 NV12-1 85 H264
```

此示例仅用于临时加载候选后的短测，设备号须枚举。候选编码器有界内容/
计数检查后已恢复在役`20220a8`，尚未正式部署本新增直接分支；它也不会把
官方FFmpeg的MMAP队列自动变为DMA-BUF，范围见[验证记录](validation-records.md#11-26-单平面-nv12-dma-buf-直接读取)。

## HEVC Main 8-bit

宿主原 7.0.2 未编入 `hevc_v4l2m2m`；当前 OpenList 镜像的官方8.1.3是另一构建。
先用`ffmpeg -decoders`核对所用程序。若缺少包装器，可使用运行包的独立工具：

```sh
/opt/meson8b-vcodec/current/tools/ffmpeg -nostdin \
  -c:v hevc_v4l2m2m -i input.mp4 -f null -
```

该独立工具启用文件/HTTP 和验收所需功能；没有 HTTPS/TLS 或软件 H.264/
HEVC 编码器，不能替代完整生产 FFmpeg。H.264 峰值/缓冲预算和失败语义见
[码率控制](rate-control.md)。
已部署版本支持 Main 8-bit 线性输出，Main10、tiles、长期参考帧不支持。
当前源码新增 tiles 的网格、MV 与滤波配置，尚未实机验收和部署。

## JPEG 编码

V4L2 CAPTURE 为 `V4L2_PIX_FMT_JPEG`，质量控件 1–100，输入为上表中的 YUV
格式。FFmpeg 7.0.2 没有 JPEG M2M encoder 包装，使用仓库客户端，先在外部
交叉构建：

```sh
mkdir -p build
arm-linux-gnueabihf-gcc-11 -O2 -Wall -Wextra -Werror -static \
  tests/encode-test.c -o build/encode-test
# 复制到目标机后
./encode-test /dev/video1 output.mjpg reference.nv12 1920 1080 5 NV12 90 JPEG
```

输入是全量程 YCbCr，客户端生成确定性图像并检查时间戳、KEYFRAME、互斥、
LAST/EOS。有限量程源须先转换；驱动不自动改变样本范围。长测试可把原始
参考输出设为 `/dev/null`，避免填满设备 `/tmp` 的 tmpfs。客户端还可设置
控件（`-c`）、生成 static/motion/noise/scene 测试画面（`-p`）、使用 DMA-BUF
（`-d dumb|lima`），并逐帧输出延迟；`-g` 在任何主机上生成同一参考画面，
用法见源码开头。[编码基线脚本](../scripts/encode-baseline.py)据此记录码率、
PSNR、CPU 与延迟。

## 其他格式与应用

当前源码提供 WMV3 的 `V4L2_PIX_FMT_VC1_ANNEX_L` 开发入口，尚未部署。
直接客户端输入完整 Annex L/RCV：首个 OUTPUT 包包含 36 字节序列头和
首幅图片，随后每包为 8 字节帧头加单幅图片；驱动生成厂商前缀并保留
应用提供的 V4L2 时间戳。诊断客户端可按 RCV 时间戳喂入：

```sh
./res-switch-test /dev/video0 input.rcv VC1L 320 240
```

容量、尺寸或非 simple/main 输入失败，不截断图片。官方 FFmpeg 没有
WMV3/Annex L 的 M2M 映射，不使用 WVC1 包装器冒充它。H.263 的开发入口
接收原始 baseline I/P 单 AU，驱动有界转换为 h263_mc 中间流；支持 128×96、
176×144、352×288、704×576，拒绝 PLUS/custom/可选工具和不匹配尺寸。
候选内容、EOS、取消/重开必须实机验收。

MPEG/VC-1/MJPEG 的输出及帧尾边界见[验证指南](validation.md)。MPEG-2
输入到 H.264 编码可按以下方式调用，当前并发验收集中在 H.264 输入：

```sh
ffmpeg -c:v mpeg2_v4l2m2m -i input.ts -c:v h264_v4l2m2m -b:v 0 -c:a copy output.mp4
```

OpenList fork 已通过标准 V4L2 自动选择设备，并按实际切片码量与时长声明
HLS 带宽；接入边界见[通用性说明](portability.md)。故障、空输出或卡住按[排查指南](troubleshooting.md)
核查，不用成功退出码替代完整帧数与内容检查。
