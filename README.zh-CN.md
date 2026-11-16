# 玩客云 Meson8b V4L2 硬件编解码

[English](README.md) · [简体中文](README.zh-CN.md)

为玩客云 S805（Meson8b）、WanOS/HiNAS 原版 **Linux 5.15.79-meson** 提供
硬件视频解码及 H.264/JPEG 编码。三个可加载模块通过可移除的运行时设备树
Overlay 暴露标准 V4L2 Memory-to-Memory 设备，构建在外部机器完成，目标为
ARMv7 hard-float。

[2026-11-14 实机验收](docs/validation.md)覆盖 H.264/HEVC 图像比对、十轮
1080p B 帧输入转码、900 帧 JPEG、固件错误恢复及可回滚安装。最终检查时
五项原有服务均运行，原启动内核、设备树和生产 FFmpeg 保留。

## 支持范围

| 路径 | 支持及验证范围 |
| --- | --- |
| 解码 | H.264、HEVC Main 8-bit、MPEG-1/2、MPEG-4 Part 2、VC-1 advanced、MJPEG |
| H.264 编码 | 4:2:0、4:2:2、4:4:4 与 RGB 输入；固定 I/P QP 或码率控制；多分片；DMA-BUF 输入；无 B 帧编码 |
| JPEG 编码 | 质量 1–100；全量程 YCbCr 输入；标准 V4L2 客户端 |
| 并发转码 | 一个解码会话加一个编码会话；原 64 MiB CMA 下验证 1080p H.264 B 帧输入 |

HEVC Main10、tiles 和长期参考帧不支持；旧 MPEG、VC-1、MJPEG 的帧尾及
色度限制见验收记录。输出仍有 CPU 字节序转换和拷贝。自动加载已于
2026-11-16 冷启动验收：overlay 在内核启动 29 s 时应用、两个 V4L2 节点出现，
原厂内核未替换。[主线移植](evidence/mainline-2026-11-16/PORT.md)只到
编译级：研究系列以 v7.3.4 为基准可编译模块与全部 ARM DTB，未在 S805 实机
运行；[linux-media RFC](docs/linux-media-rfc.md) 已起草但未提交。
详见[能力与边界](docs/validation.md)。上表的编码扩展与 MPEG/VC-1 drain
修复于 2026-11-15 实机验证，已包含在在役机安装的 `c20a473` 包中。
S805 官方编码格式为 H.264 和 JPEG，HEVC 属于解码能力。
[编码扩展可行性](docs/encoding-feasibility.md)记录硬件依据与后续 H.264/JPEG 开发方向。

## 开始使用

1. 阅读[安装和回滚](docs/installation.md)：包须匹配内核 ABI、板型与运行
   设备树的 provider phandle。
2. 从[发版页面](https://github.com/YoungSx/onecloud-meson8b-vcodec/releases)取得
   构建产物，或[从源码交叉编译](docs/build.md)。CI 产物代表构建验证；实机
   验收对应记录的源码版本。
3. 校验归档，再使用新的备份目录安装：

   ```sh
   sudo python3 runtime-package/scripts/runtime-device.py check runtime-package
   sudo bash runtime-package/scripts/install-autoload.sh runtime-package /root/new-codec-state
   ```

结束所有编解码客户端后，移除自动加载、卸载模块并恢复安装前固件：

```sh
sudo bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh
```

## 使用示例

原生产 FFmpeg 支持 H.264 M2M。已验证的四参考帧 1080p B 帧输入使用
10 个 CAPTURE 缓冲：

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -num_capture_buffers 10 -i input.mp4 \
  -an -c:v h264_v4l2m2m -b:v 0 -fps_mode passthrough output.mp4
```

独立包 `tools/ffmpeg` 增加 HEVC M2M 与 V4L2 drain 修复，只用于文件测试；
H.264 编码使用生产 FFmpeg。FFmpeg 7.0.2 没有 JPEG M2M 编码包装，JPEG
使用 V4L2 客户端。[使用指南](docs/usage.md)提供完整命令及缓冲数量说明。

## 文档与开发

[文档目录](docs/README.md) · [架构](docs/architecture.md) ·
[构建](docs/build.md) · [验证](docs/validation.md) ·
[CI 和发版](docs/release.md) · [贡献指南](CONTRIBUTING.md) ·
[路线图](ROADMAP.md) · [变更记录](CHANGELOG.md)

CI 运行主机测试及 Sanitizer，复现固定微码，针对匹配内核交叉编译、检查
additive 补丁与包校验。版本标签运行同样检查，并生成含运行包、源码包、
校验和及构建来源的 GitHub Release **草稿**。发布草稿或安装新构建前，
按发版指南完成对应验证。

代码按各文件 SPDX 使用 GPL-2.0/GPL-2.0+；厂商微码来自固定 GPL 源码。
[固件来源与许可](docs/firmware.md)也说明独立工具的许可。
