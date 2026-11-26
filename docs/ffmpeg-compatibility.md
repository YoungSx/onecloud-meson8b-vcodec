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

## 编码 SPS 像素比例

当前driver源码提供标准V4L2的SAR_ENABLE、SAR_IDC、EXT_SAR_WIDTH/HEIGHT。
官方7.0.2 M2M编码[固定源码](https://github.com/FFmpeg/FFmpeg/blob/n7.0.2/libavcodec/v4l2_m2m_enc.c)
未转发这些控件。官方[h264_metadata](https://ffmpeg.org/ffmpeg-bitstream-filters.html#h264_005fmetadata)
过滤器可修改同一SPS字段；默认独立ARM工具已启用该官方组件，源码仍为upstream。

```sh
ffmpeg -nostdin -i encoded.h264 -c:v copy \
  -bsf:v h264_metadata=sample_aspect_ratio=12/11 corrected.h264
```

比例设置为目标输出的SAR；缩放后应使用新的输出SAR，并保持容器的显示
比例一致。不要把整幅画面的DAR直接写成像素SAR。主机6.1.1及原版7.0.2
ARM/QEMU的完整软解覆盖1:1、12:11、3:2、扩展17:13及65535/65535；各12
图的Y/U/V哈希保持一致，官方过滤器输出17:13也通过。软件Main/B样本用于
SPS字段与图片检查；硬编仍为已验证Baseline，11-24后续硬编验收见[安装记录](validation-records.md#11-24-正式安装验收)。

单独的65535/1极端比例在两种原版软件工具中触发SAR校验忽略，并出现
2/12短输出；该样本保留为失败。字段的16-bit可表示范围不等于所有客户端
均可显示/完整软解，不能据正常比例检查宣称全范围通过。

## Bug 修复 / Bug fixes

默认 `scripts/build-ffmpeg-test.sh` 从校验过的官方 tarball 重新解压构建，
防止旧补丁残留。显式 `--bugfixes` 才应用：

- [drain/POLLERR 修复](../patches/ffmpeg-7.0.2-v4l2-drain.patch)：队列耗尽的
  暂态不能直接当作 EOS；活动队列 POLLERR 应 dequeue 得到实际错误。
- [输入和错误修复](../patches/ffmpeg-7.0.2-v4l2-bugs.patch)：共享 NV12 软件帧
  只有 `buf[0]`，访问 `buf[1]` 会崩溃；使用 data/stride 并验证目标容量。
  编码 ERROR 和终止 dequeue 失败不能静默变为成功 EOF。
- [像素比例方向修复](../patches/ffmpeg-7.0.2-v4l2-sar.patch)：CROPCAP的y/x
  需要取倒数才能成为码流SAR的x/y。实际内核/驱动回调和FFmpeg 7.0.2函数
  主机链路复现：SAR 12/11对应CROPCAP 11/12，原函数错误得到SAR 11/12，
  修复后恢复12/11；另覆盖方形像素、NTSC比例、扩展比例和ioctl失败。

Default builds re-extract the pinned official tarball. `--bugfixes` explicitly
applies only the linked drain, input/error and CROPCAP-to-SAR fixes; it adds no private options.
The shared NV12 upload is covered by ASan/UBSan against the actual patched
function. Silicon error tests remain in the dated evidence. Each build records
`BUILD_VARIANT`, copied into runtime packages as `FFMPEG_BUILD_VARIANT`.

驱动内部保留码流SAR为x/y，CROPCAP回调按规范返回y/x。本轮两种静态ARM工具构建及QEMU版本检查通过，但没有
硬件播放/尺寸切换验收。未修改的FFmpeg 7.0.2在该查询路径上仍有非方形
像素方向错误，不能声明此项原版兼容已验收。以上修复为独立可选bug补丁，
没有向FFmpeg增加编码选项或私有控制。

## 社区与源码依据 / Primary references

- [FFmpeg developer: use -b:v](https://ffmpeg.org/pipermail/ffmpeg-user/2021-August/053324.html).
- [V4L2 standard options discussion](https://ffmpeg.org/pipermail/ffmpeg-devel/2020-February/256688.html).
- [CROPCAP pixelaspect and type normalization](https://docs.kernel.org/userspace-api/media/v4l/vidioc-cropcap.html).
- [FFmpeg 7.0.2 v4l2_get_sar](https://github.com/FFmpeg/FFmpeg/blob/n7.0.2/libavcodec/v4l2_context.c#L66).
- [M2M controls are per open](https://forums.raspberrypi.com/viewtopic.php?t=294684).
- [Proposed extra options are patches, not proof of upstream support](https://ffmpeg.org/pipermail/ffmpeg-devel/2022-March/294607.html).
- [Pinned current encoder source](https://github.com/FFmpeg/FFmpeg/blob/465b494ef7c8dd206f5a147344a719e3522931a2/libavcodec/v4l2_m2m_enc.c).

原在役 7.0.2 的三组 ioctl 跟踪确认标准参数生效；加入 maxrate/bufsize
没有额外 peak/CPB 调用。三次都输出 60 帧，生产容器与模块未变。

## 哈希输出线程 / Checksum output threads

用于 `framemd5` 验收时，设置全局 `-filter_threads 1` 及输入之后的
`-threads:v 1`，避免输出帧线程长期持有采集 buffer。原版宿主 7.0.2 在
原驱动和候选驱动上都复现默认输出提前结束，单线程配置与参考逐帧比较；
见[对照记录](validation-records.md#官方-ffmpeg-与性能)。这不增加
任何私有功能，也不意味着所有历史短输出都已归因。

For checksum diagnostics, bound filter and rawvideo output threads using
standard options. The input codec and hardware frame pool are unchanged.
This addresses a reproduced output-retention case; rare historical short
output remains a separate open investigation.


11-25显式GE2D采集下，原版7.0.2在低CAPTURE缓冲有短输出；独立原生
客户端同路径400幅逐帧一致。可选bugfix验收工具在CAPTURE 2恢复small/crop
和HEVC1080p完整哈希，但H.2641080p仍超时；8缓冲又可能耗尽64 MiB CMA。
生产工具保持官方原版，GE2D默认关闭；不能据上述部分对照宣称完整零拷贝链
已兼容。完整范围见[验收记录](validation-records.md#11-25-重启后格式修正与-ge2d-验收)。

新增真实dequeue函数回归复现了上述drain边界：当所有CAPTURE暂由消费端
持有，官方7.0.2提前设置done，丢掉随后归还缓冲并完成的图片；既有可选修复
保留等待，并区分空LAST和终止ioctl错误。测试fixture恢复为固定官方原文件、
校验SHA256，补丁强制forward应用，防止再次应用时自动反向撤销。该测试不
改变生产工具，也不能证明GE2D完整链已通过。固定n8.1.3源码仍保留相同的
无排队CAPTURE即结束判断；完整命令行为须单独实测。

39703c7压缩HEVC内部帧池的额外预留后，官方7.0.2使用标准OUTPUT2/CAPTURE10
并实际scale至720p，可在64 MiB CMA内完成1080p30/30精确内容。相同缓冲直接
NV12输出仍12/30；这是一条有界缩放链的通过，默认NEON和生产官方工具不变，
不是所有GE2D链、完整转码或吞吐验收。正式包重复GE2D缩放时又出现SIGSEGV、
零输出及短暂工具包校验异常，未计通过，恢复生产NEON；来源见[帧池记录](validation-records.md#11-25-hevc-sps-帧池预算)。


11-26共享GE2D中转面后，MMAP采集不再逐缓冲占CMA，官方7.0.2与OpenList
镜像内未改8.1.3在decoder CAPTURE20/encoder OUTPUT16下，完整1080p60幅
直接转码及CPU缩至720p再编码均通过独立软解/全YUV质量；7.0.2重加载重复
也通过。该有界用法不修改FFmpeg，不证明低缓冲drain错误已解决、零拷贝、
全部OpenList HLS/音频或实时吞吐，见[采集内存记录](validation-records.md#11-26-ge2d-采集内存与官方转码)。
