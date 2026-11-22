# 官方 FFmpeg 适配 / Official FFmpeg compatibility

本项目以官方原版 FFmpeg 为默认兼容目标，不维护私有 FFmpeg 功能分支，
不通过私有开关实现码控。仅对明确复现的 bug 保留独立、可选的最小修复。
以前带 `v4l2_rate_limit` 的验收属于已停用的实验，不能当作官方兼容证明。

Official unmodified FFmpeg is the compatibility target. No private rate-control
extension is required or maintained. Minimal optional patches are reserved for
reproducible bugs. Earlier private-option experiments remain dated evidence,
not the current compatibility baseline.

## 标准用法 / Standard usage

```sh
ffmpeg -nostdin -c:v h264_v4l2m2m -i input.mp4 -an \
  -c:v h264_v4l2m2m -b:v 2M -qmin 20 -qmax 32 -g 30 \
  -r 30 -fps_mode cfr output.mp4
```

输入 decoder 在 `-i` 前，输出 encoder、码率、QP 和帧率在输出文件前。
`-h encoder=h264_v4l2m2m` 只列私有选项，不代表全部通用编码参数。
`num_capture_buffers`/`num_output_buffers` 是队列数量，和码率 CPB/VBV
容量不同。独立 `v4l2-ctl` 打开的是另一个 M2M 上下文，不能配置 FFmpeg
已经打开的会话。

The decoder is an input option; encoder, target rate, QP, GOP and frame rate
are output options. Encoder help lists private options, not every generic
codec option. Queue buffer counts are not CPB/VBV capacity. A separate
v4l2-ctl open does not change FFmpeg's M2M context.

检查的官方 n7.0.2、n8.1.3 和 master
`465b494ef7c8dd206f5a147344a719e3522931a2` 会传递目标码率、QP、GOP 和帧周期；
没有读取 `rc_max_rate`/`rc_buffer_size`，也没有发 BITRATE_PEAK/H264_CPB_SIZE。
参数能解析不代表设备已应用。驱动继续提供标准控件给直接 V4L2 客户端，
不把目标不可达误归因于缺少 `-b:v`。

The checked versions forward target bitrate, QP, GOP and frame period. They do
not read maxrate/bufsize or send peak/CPB controls. Direct clients can use those
standard driver controls. A saturated hardware target is not a missing bitrate
option. OpenList can downscale from measured output with those explicit limits
left blank; that is a measured average policy, not a CPB guarantee.

## Bug 修复 / Bug fixes

默认 `scripts/build-ffmpeg-test.sh` 从校验过的官方 tarball 重新解压构建，
防止旧补丁残留。显式 `--bugfixes` 才应用：

- [drain/POLLERR 修复](../patches/ffmpeg-7.0.2-v4l2-drain.patch)：队列耗尽的
  暂态不能直接当作 EOS；活动队列 POLLERR 应 dequeue 得到实际错误。
- [输入和错误修复](../patches/ffmpeg-7.0.2-v4l2-bugs.patch)：共享 NV12 软件帧
  只有 `buf[0]`，访问 `buf[1]` 会崩溃；使用 data/stride 并验证目标容量。
  编码 ERROR 和终止 dequeue 失败不能静默变为成功 EOF。

Default builds re-extract the pinned official tarball. `--bugfixes` explicitly
applies only the linked drain and input/error fixes; it adds no private options.
The shared NV12 upload is covered by ASan/UBSan against the actual patched
function. Silicon error tests remain in the dated evidence. Each build records
`BUILD_VARIANT`, copied into runtime packages as `FFMPEG_BUILD_VARIANT`.

## 社区与源码依据 / Primary references

- [FFmpeg developer: use -b:v](https://ffmpeg.org/pipermail/ffmpeg-user/2021-August/053324.html).
- [V4L2 standard options discussion](https://ffmpeg.org/pipermail/ffmpeg-devel/2020-February/256688.html).
- [M2M controls are per open](https://forums.raspberrypi.com/viewtopic.php?t=294684).
- [Proposed extra options are patches, not proof of upstream support](https://ffmpeg.org/pipermail/ffmpeg-devel/2022-March/294607.html).
- [Pinned current encoder source](https://github.com/FFmpeg/FFmpeg/blob/465b494ef7c8dd206f5a147344a719e3522931a2/libavcodec/v4l2_m2m_enc.c).

原在役 7.0.2 的三组 ioctl 跟踪确认标准参数生效；加入 maxrate/bufsize
没有额外 peak/CPB 调用。三次都输出 60 帧，生产容器与模块未变。

## 哈希输出线程 / Checksum output threads

用于 `framemd5` 验收时，设置全局 `-filter_threads 1` 及输入之后的
`-threads:v 1`，避免输出帧线程长期持有采集 buffer。原版宿主 7.0.2 在
原驱动和候选驱动上都复现默认输出提前结束，单线程配置与参考逐帧比较；
见[对照记录](../evidence/official-ffmpeg-2026-11-22/README.md)。这不增加
任何私有功能，也不意味着所有历史短输出都已归因。

For checksum diagnostics, bound filter and rawvideo output threads using
standard options. The input codec and hardware frame pool are unchanged.
This addresses a reproduced output-retention case; rare historical short
output remains a separate open investigation.
