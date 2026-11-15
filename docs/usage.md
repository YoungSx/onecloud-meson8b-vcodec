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

使用目标原生产 FFmpeg。已验收的 Baseline 单参考帧样片用 7 个 CAPTURE
缓冲；四参考帧、bframes=2 的 1080p 输入使用 10 个：

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -num_capture_buffers 10 -i input.mp4 \
  -an -c:v h264_v4l2m2m -b:v 0 -fps_mode passthrough output.mp4
```

`-i` 前选 decoder，后选 encoder。`-b:v 0` 使用固定 QP 26；非零 bitrate
启用码率控制（见下节）。缓冲数量必须至少为实际流的 `MIN_BUFFERS_FOR_CAPTURE`，
10 也不是任意流的通用常量。原 64 MiB CMA 下十轮 60 帧 B 帧输入完整输出，
逐帧 PSNR 最低 45.03 dB、平均 45.82 dB，见[验收](validation.md)。

每次一个 decoder 和一个 encoder，应用侧排队。`-vf scale=640:-2` 可缩图，
缩放由 CPU 完成；数据路径仍有字节序转换和输入拷贝。

## H.264 编码控件

| 控件 | 行为 |
| --- | --- |
| `BITRATE` + `FRAME_RC_ENABLE` | 二者都非零时启用码率控制：1 秒漏桶、按 QP 26 归一化的 P/IDR 复杂度估计，P 帧 QP 不超过 32 |
| `H264_I_FRAME_QP` / `H264_P_FRAME_QP` | 关闭码率控制时的固定 QP（默认 26）；码率控制时 P QP 为起点，二者差值为 IDR 偏移 |
| `H264_MIN_QP` / `H264_MAX_QP` | 限制所有 QP；同一次 `S_EXT_CTRLS` 内按新的一对校验，min>max 返回 `ERANGE` |
| `GOP_SIZE` | IDR 间隔；0 表示只有首帧和强制关键帧为 IDR |
| `FORCE_KEY_FRAME` | 下一帧为 IDR |
| `HEADER_MODE` | 只有 `JOINED_WITH_1ST_FRAME`；SPS/PPS 在每个 IDR 前 |
| `MULTI_SLICE_MODE` / `MULTI_SLICE_MAX_MB` | `MAX_MB` 按整行宏块向下取整；分片随整帧交付，没有逐片低延迟 |

控件在两帧之间生效。码流为 Baseline、无 VUI，帧率与色彩信息由封装层携带。
FFmpeg 默认 bitrate 为 200 kbit/s；码率控制达不到的目标会停在 QP 32，
需要更低码率时应降低分辨率或帧率。单帧超过 1 MiB 码流缓冲（例如 1080p
在很低 QP 下）时该帧返回错误，下一帧为 IDR，日志提示所用 QP。

## 编码输入格式

| 格式 | 说明 |
| --- | --- |
| NV12M、NV21M、YUV420M | 4:2:0；FFmpeg 的 `nv12`/`nv21`/`yuv420p` |
| YUV444M、YUYV、UYVY | 由硬件下采样为 4:2:0；FFmpeg `yuyv422`、`uyvy422` |
| RGB24 | 宽度不超过 1376；更宽的行硬件读取错乱 |
| XRGB32、XBGR32、RGBX32、BGRX32 | 全宽度；FFmpeg `0rgb`/`bgr0` 用旧名 RGB32/BGR32 也可 |

RGB 视为全量程，转换为有限量程 YCbCr：OUTPUT 格式 `ycbcr_enc` 为 709（或由
colorspace 默认为 709）时用 BT.709，否则 BT.601；CAPTURE 格式据此报告。
三字节 BGR 不受支持。JPEG 保持全量程输入，不接受 RGB 输入。宽度不是
16 的倍数，或为 1–3、65 个宏块宽时，驱动在内部补齐宏块并在 SPS 中裁掉，
解码得到原尺寸。

## DMA-BUF 输入

OUTPUT 队列接受 MMAP 和 DMA-BUF。NV12M/NV21M 的两个平面若各自映射为
一段连续地址、8 字节对齐且行宽等于驱动报告的 bytesperline，硬件直接读取，
不经过拷贝；例如 meson-drm 的 dumb buffer。其他导入（如 lima GEM 的分散
页）由 CPU 读入 staging，结果相同但通常比 MMAP 慢。直接路径会读取缓冲中
可见区域以外的对齐行，客户端应把这些行清零或保持一致。
`/sys/kernel/debug/meson-venc/` 下的 `direct_frames`、`copied_frames` 和
`copy_ns` 记录两条路径的帧数和拷贝耗时。

## HEVC Main 8-bit

目标原 FFmpeg 未编入 `hevc_v4l2m2m`，使用运行包的独立工具：

```sh
/opt/meson8b-vcodec/current/tools/ffmpeg -nostdin \
  -c:v hevc_v4l2m2m -num_capture_buffers 8 -i input.mp4 -f null -
```

该工具只启用文件协议和验收所需功能；不能替代完整生产 FFmpeg。
支持 Main 8-bit 线性输出，Main10、tiles、长期参考帧不支持。

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

MPEG/VC-1/MJPEG 的输出及帧尾边界见[验证指南](validation.md)。MPEG-2
输入到 H.264 编码可按以下方式调用，当前并发验收集中在 H.264 输入：

```sh
ffmpeg -c:v mpeg2_v4l2m2m -i input.ts -c:v h264_v4l2m2m -b:v 0 -c:a copy output.mp4
```

OpenList 可索引预转码后的文件；应用内部自动转码链未配置，不修改
OpenList/WanOS 核心。故障、空输出或卡住按[排查指南](troubleshooting.md)
核查，不用成功退出码替代完整帧数与内容检查。
