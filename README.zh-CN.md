# 玩客云 Meson8b V4L2 硬件编解码

[English](README.md) · [简体中文](README.zh-CN.md)

为玩客云 Amlogic S805（Meson8b）提供硬件视频解码与 H.264/JPEG 编码。
三个外部模块通过可移除的运行时设备树 Overlay，在 WanOS/HiNAS 原版
**Linux 5.15.79-meson** 上暴露标准 V4L2 Memory-to-Memory 设备，无需替换启动内核。

## 能做什么

| 路径 | 范围 |
| --- | --- |
| 解码 | H.264、HEVC Main 8-bit 已做图像内容检查；MPEG-1/2、MPEG-4 Part 2、VC-1 advanced、MJPEG 有已知帧尾或内容限制。 |
| H.264 编码 | Baseline/Level 4.0；固定 QP 或 VBR 目标；YUV/RGB 输入、DMA-BUF、多分片、SPS 裁剪/VUI。 |
| JPEG 编码 | 质量 1–100，全量程 YCbCr 输入，使用直接 V4L2 客户端。 |
| 转码 | 一个解码会话加一个编码会话；已验收的 1080p 样片可在原 64 MiB CMA 内运行。 |

适配目标为**官方原版 FFmpeg**。OpenList fork 自动发现 V4L2 格式并串行使用
硬件；Docker 仍需映射设备。不提供 HEVC 硬编、Main10、H.264 Main/High/B 帧
编码、端到端零拷贝或实时吞吐保证。完整边界见[能力与验收](docs/validation.md)。

最新正式版为 **v0.4.0**。当前在役机安装了更新的开发构建（`5fedae9`），
保留 v0.4.0 供回滚。有界检查通过；两轮网络失联已经调查，持续负载实验仍暂停。
开发部署和 CI 通过不等于新的稳定版。

## 快速开始

1. 获取[发行包](https://github.com/YoungSx/onecloud-meson8b-vcodec/releases)，
   或在外部机器[交叉构建](docs/build.md)。
2. 阅读[安装与回滚](docs/installation.md)。包须匹配内核 ABI、板型及运行中
   设备树的 phandle；指南包含下载传输、安装前检查、内容验收、卸载和旧版回退。
3. 结束编解码客户端，使用新的备份路径安装：

   ```sh
   sudo python3 runtime-package/scripts/runtime-device.py check runtime-package
   sudo bash runtime-package/scripts/install-autoload.sh runtime-package /root/new-codec-state
   ```

使用包含对应包装器的 FFmpeg：

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -i input.mp4 -an \
  -c:v h264_v4l2m2m -pix_fmt nv12 -b:v 2M -g 30 output.mp4
```

默认 CAPTURE 数即可，无需板型专用缓冲调参。码率是 VBR 目标，低目标可能不可达；
原版 FFmpeg 未转发峰值/CPB 控件。[使用指南](docs/usage.md)包含设备枚举、JPEG
和其他输入；[FFmpeg 适配](docs/ffmpeg-compatibility.md)解释标准参数语义。

## 开发与维护

[文档目录](docs/README.md) · [架构](docs/architecture.md) ·
[构建](docs/build.md) · [故障排查](docs/troubleshooting.md) ·
[路线图](ROADMAP.md) · [CI/发版](docs/release.md) ·
[贡献指南](CONTRIBUTING.md) · [变更记录](CHANGELOG.md)

重新 clone 即可取得源码、固件、样片和公开证据；Git 无法恢复的私有备份和精确
部署产物见[工作区迁移](docs/workspace-migration.md)。Linux 7.3.4 研究补丁仅通过
构建检查，尚未在板上启动。

代码按各文件 SPDX 使用 GPL-2.0/GPL-2.0+；见[固件来源与许可](docs/firmware.md)。
