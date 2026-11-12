# WanOS/玩客云 V4L2 M2M 硬解使用指南（OpenList 场景）

目标机：OneCloud/玩客云，WanOS 0.3.427，内核 `5.15.79-meson`，
ffmpeg 7.0.2（johnvansickle static，armhf）。硬解三件套部署方式见
README「在目标机上部署」；本页只讲部署好之后怎么用。

## 前置状态

```sh
lsmod | grep -E "meson"          # meson8b-runtime, meson-vdec
ls /dev/video0                   # 存在即 overlay 已生效
ls /dev/video0                   # 存在即 overlay + 驱动已生效
ffmpeg -hide_banner -decoders | grep v4l2m2m
# v4l2m2m 条目静态编译于 ffmpeg，与模块无关；实际可用性以
# 能否成功 STREAMON 为准（本驱动：h264 / mpeg1 / mpeg2）
```

固件需在 `/lib/firmware/meson/vdec/`：`meson8b_h264.bin`（36864 B）、
`meson8b_mpeg12.bin`（16384 B）。

## 何时用硬解、何时用软解

实测（1080p30，无人值守 os.times 测量，3 次取中位）：

| 场景 | 软解 CPU | 硬解 CPU | 降幅 |
|------|---------|---------|------|
| H.264 300 帧 | 14.6 s | 3.5 s | -76% |
| MPEG-2 150 帧 | 4.42 s | 0.60 s | -86% |

硬解省的是 ffmpeg 进程 CPU（解码线程交还硬件 + 影子帧拷贝）。注意两点：

- 输出路径含内核态 CPU 字节序拷贝（`vdec/meson8b-capture.c`），已用
  cached vmap 别名优化，但整机 wall time 仍受拷贝支配。
- 同时只有一个解码会话（模块单 `core->cur_sess`）。OpenList 缩略图等
  并发场景需加队列/锁，避免第二个会话挂起。

## ffmpeg 命令

**H.264 → 低分辨率转码（缩略图 / 预览）**：

```sh
ffmpeg -c:v h264_v4l2m2m -i input.mp4 \
       -vf scale=640:-2 -c:v libx264 -preset veryfast -crf 28 \
       -c:a aac -movflags +faststart output.mp4
```

`-c:v h264_v4l2m2m` 放在 `-i` 前面（解码端）。编码仍是 x264 软编
（硬件编码 amvenc_avc 未移植，见 ROADMAP P3）；缩到 ≤480p 时软编开销
可接受。

**MPEG-2（.ts 录制 / DVD 转 H.264**）：

```sh
ffmpeg -c:v mpeg2_v4l2m2m -i input.ts \
       -c:v libx264 -preset veryfast -crf 26 -c:a copy output.mp4
```

**仅播放（不转码）**：

```sh
ffmpeg -c:v h264_v4l2m2m -i input.mp4 -f nut - | mpv -
```

## OpenList 集成方式

OpenList v4.2.9 本体不带视频转码（文件列表 + 直链下载/代理）。在
WanOS 上让链路吃到硬解的三种做法：

1. **预转码（推荐）**：入库/整理阶段批量转好低码率版本，OpenList 只
   索引文件。上面两条命令套 shell 循环即可；无人值守批量跑时用
   `nice -n 19` 限制对在役服务的影响。
2. **外挂流媒体服务**：OpenList 挂载存储目录给 jellyfin/emby（均支持
   ffmpeg 参数覆盖，把解码器指到 `h264_v4l2m2m`）。单会话限制依旧，
   建议 `transcoding thread count = 1`。
3. **直链 + 本地播放器**：OpenList 出直链，播放端（手机/盒子）自行
   软解——与硬解无关，仅在目标机本身作播放头时用上面的 nut 管道。

## 常见问题

- **第二个 ffmpeg 硬解进程挂起**：单会话限制，排队执行。
- **`Failed to open /dev/video0` / 解码器列表无 v4l2m2m**：模块未加载，
  回到 README 部署节。
- **硬解输出与软解逐帧有 ±3 内的亮度差**：M8B 输出位深舍入，正常。
- **MPEG-2 文件末尾少 1-2 帧**：固件只在下一幅图开始时完成上一幅，
  EOS 由驱动软件冲刷兜底；属已知行为。
- **用完恢复原状**：`rmmod meson-vdec && rmmod meson8b-runtime`。
