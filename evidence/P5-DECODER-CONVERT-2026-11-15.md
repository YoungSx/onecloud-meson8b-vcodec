# P5 解码输出转换验收（2026-11-15）

OneCloud 实机，原内核 `5.15.79-meson`、64 MiB CMA，五项业务服务在运行。
测试只替换解码模块（`rmmod meson_vdec` 后 `insmod` 候选模块），结束后恢复
已安装的 `bd1cb79` 模块（`1c47538fe47c7ef7`）；没有改动 `/boot`、固件或
服务。原始日志在 [p5-2026-11-15](p5-2026-11-15/)。

| 模块 | SHA-256（前 16 位） | 内容 |
| --- | --- | --- |
| 已安装解码 | `1c47538fe47c7ef7` | `bd1cb79` |
| 标量转换＋计数 | `7f7b966ce5863ffb` | 旧 `meson8b_copy_swap64`，加 ktime/debugfs |
| 行交换实验 | `5cd0de0e45fef9fd` | 只作对照，未采用 |
| NEON 转换 | `19236d1d9e3bdb61` | NEON 转换，窗口代码仍在 .c 内（仅对照） |
| NEON＋窗口拷贝 | `ca28367da71a5818` | 有界窗口拷贝，逐帧结果见下 |
| 本提交最终 | `b8009e10c4373aaf` | 窗口拷贝移入 `meson8b-window.h`，由该源码构建 |

`convert_us/frame` 是 debugfs `convert_ns / converted_frames`，即每帧
`amvdec_meson8b_copy_capture()` 的墙钟时间（dma_sync、字节序转换、裁剪
与补零），不含硬件解码。所有输出都与同一样片的软件解码 NV12 framemd5
逐帧比较，`FAILS=0`、`dmesg warnings 0`。

## 方法

`build/p5/p5-convert.sh`（标量/NEON 对照）与 `p5-final.sh`（旧/最终对照）
在目标机上依次替换模块，每个模块跑两轮，每轮用 `/usr/local/bin/ffmpeg`
或包内测试 `ffmpeg` 经 `h264_v4l2m2m`/`hevc_v4l2m2m` 解码，`-pix_fmt nv12
-f framemd5` 输出后由 `scripts/verify-hashes.py` 与软件参考比较。

样片：HEVC Main 8-bit 1920x1080 与 854x480，H.264 High 1920x1080（300 帧）、
1360x768、1600x900，以及 `hevc_metadata` 裁出的左偏移窗口
（left64/left6/left16/left30，含奇数左偏移）。左偏移参考用
`-flags:v unaligned` 生成，与 V4L2 输出的未对齐行一致。

## 结果

每帧转换耗时（µs，两轮；`ca28367d` 测得，`b8009e10` 回归见下）：

| 样片 | 旧（标量） | 最终（NEON） |
| --- | --- | --- |
| HEVC 854x480 | 2912 | 1419 / 1633 |
| HEVC 1080p | 15236 | 8266 / 6772 |
| H.264 1080p | 16147 | 7980 / 8032 |
| HEVC left64 | 36225 | 7506 / 7300 |
| HEVC left16 | — | 8178 / 8044 |
| HEVC left6（未对齐） | 39307 | 23528 / 22555 |
| HEVC left30（未对齐） | — | 22322 / 21300 |
| H.264 1360x768 | — | 5713 / 6396 |
| H.264 1600x900 | — | 6587 / 6295 |

窗口头文件重构后的回归（`window.log`）：1080p 7496/10478 µs，
1360x768 3369/4982 µs，1600x900 8587/5178 µs，left64 7595/7867 µs，
left6 23352/23057 µs，left30 22417/24845 µs，全部哈希一致。

- 对齐窗口与整帧路径都走 NEON 字拷贝，1080p 约 7–8 ms，约为标量的一半；
  对 33 ms 的 1080p30 帧期约占 20–25%。
- 左偏移不是 8 的倍数时按字节循环，约 21–23 ms。这是可选优化点，不影响
  正确性；当前样片中没有这种流的实际用途。
- HEVC 裁剪路径原先逐字节 36–39 ms，改为按行窗口拷贝后与整帧同量级。

## 同批修复的缺陷

1. `get_output_size()` 原先返回 `ALIGN(width * height, 64 KiB)`，比
   `amvdec_set_canvases()` 编程的 `ALIGN(width,32) x ALIGN(height,32)`
   canvas 小。1360x768（1,044,480 vs 1,056,768 字节）与 1600x900
   （1,440,000 vs 1,536,000）会越界写入。现按对齐后的 canvas 尺寸分配，
   `queue_setup`、`buffer_fits`、`bytesused` 都经过同一函数；两个尺寸的
   回归哈希通过。
2. HEVC `process_rpm()` 无条件把 `sps.left/top` 写入 `sess->crop_left/top`，
   而 SPS 与当前图像的 coded size 不一致时窗口并不适用。现在每帧先清零，
   只在该 SPS 匹配时设置，并补齐 `reference_width/height` 赋值顺序。
3. H.264/MPEG/VC1/MJPEG 从不清 `crop_left/crop_top`，分辨率切换（如
   1080p→720p）后旧分支会以 1920 字节源行读入 1280 字节目标行，逐行越界
   读取最多 640 字节。现在窗口拷贝逐行校验 `left+width`、`crop_top+height`
   与目标平面容量，越界返回 `-EINVAL` 并让该缓冲以 ERROR 结束。

## GE2D 门槛结论：不接入

- 主线 `drivers/media/platform/meson/ge2d` 只匹配 `amlogic,axg-ge2d`；
  meson8b 的 DTS 与 vdec 驱动都没有 GE2D 节点或引用（`vdec/` 与 `dts/`
  内检索为空）。
- GE2D 没有处理 VDEC canvas 字节序的选项，也无法访问 shadow 参考帧语义，
  因此不能替代 CPU 转换；它只能对已转换帧做缩放/颜色转换。
- 转换本身已从约 15 ms 降到 7–8 ms，占 1080p30 帧期约 20%；没有已确认的
  缩放/裁剪用例前，新建 meson8b GE2D 驱动与电源域/时钟依赖不划算。
  若出现「解码后需缩放」的实际需求，再按 `CLKID_G2D`(49)/`RESET_GE2D`(70)
  单独评估。

## 回归

主机：`make test` 通过，新增 `tests/test_window.c` 覆盖对齐/未对齐左偏移、
行补零、平面尾部清零和三类越界拒绝（ASan/UBSan）。解码回归覆盖
HEVC/H.264 三类有效样片、左偏移裁剪、非 32 对齐尺寸；模块替换后原
`bd1cb79` 模块恢复并重新加载，`dmesg warnings 0`。
