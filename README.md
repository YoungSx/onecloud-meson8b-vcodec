# meson8b-vcodec — 玩客云 (S805/Meson8b) 硬件 H.264 编解码，运行时加载、免重启

在不动目标机内核、设备树、`/boot` 的前提下，为 Linux `5.15.79-meson`（OneCloud/玩客云，
WanOS/HiNAS 底座）提供可通过 ffmpeg `h264_v4l2m2m` 使用的 V4L2 M2M 硬件 H.264 解码与编码。

**实测状态（2026-11-13，目标机 100.87.212.107）**：1080p H.264 300 帧硬解输出与软解基线
逐帧 MD5 300/300 一致；P0 零拷贝路径后整机 CPU 减半。**HCODEC 编码（P3）**：
1080p30 H.264，ffmpeg `-c:v h264_v4l2m2m` 编码可用，输出软解可播，矩阵 PSNR
42.6-45.7 dB（nv12/nv21/yuv420p，320x240-1920x1080）；1080p30 编码 CPU 5.85s vs
软编 23.89s（-75%），28.4 fps 接近实时（libx264 ultrafast 19.5 fps）。
模块可正常卸载，卸载后系统恢复原状，在役服务（OpenList、Home Assistant、
Arcane agent、sublinkPro、VoCat）全程正常。

**编码已知限制**：固定 QP=26（vendor fixed-quant 模型，码率控制 no-op）；
同一时刻单编码会话；宽 16-1920。裸流 PSNR 比较前须 `setpts=N/(fps*TB)`
重定时（elementary stream 无时间戳，否则 framesync 错配出假低值）。

## 为什么需要两个模块 + 一个固件文件

目标机运行的是发行版原版内核，没有 vdec 驱动，也没有 vdec 设备树节点；且该机是在役
生产机，不能随便换内核。本方案三件套全部运行时注入：

1. **`meson8b-runtime.ko`** — 从固件目录读取 `meson/vdec/meson8b-runtime.dtbo`，
   通过 `of_overlay_fdt_apply()` 把 vdec 设备树节点（含时钟/reset/canvas phandle）
   挥发性叠加到运行中的设备树上，然后触发平台设备 probe。卸载时完整移除 overlay。
   需要内核启用 `CONFIG_OF_OVERLAY`（见下）。
2. **`meson-vdec.ko`** — staging meson-vdec 驱动的 Meson8b 移植：新增
   `amlogic,meson8b-vdec` 平台数据、厂商复位序列、独立 shadow 参考帧缓冲 +
   CPU 字节序转换的 capture 路径（原因见下）。
3. **`meson-venc.ko`** — HCODEC H.264 编码器的 Meson8b 移植（P3）：厂商
   供电/时钟/微码加载序列 + V4L2 M2M 接口。设备树节点同样由 runtime
   overlay 注入（AO 寄存器映射两窗口 + DOS/HCODEC）。
4. **`firmware/meson/vdec/meson8b_h264.bin`**、
   **`firmware/meson/venc/meson8b_avc_enc.bin`** — 从 Hardkernel ODROID-C1
   3.10 厂商内核固定提交提取的微码（来源与哈希见
   `firmware/sources.json`）。

Overlay 里的 phandle 是针对目标机**运行中的设备树**生成的（`scripts/make-overlay.py`
读取 `/sys/firmware/devicetree/base` 解析 provider），不能直接用于其他机器。

## Meson8b capture 字节序问题的解法

上游 canvas 驱动明确标注 Meson8 及更早硬件不支持 GX 世代使用的 canvas
endian-swap 位（提交 `9a98fdf5b6e6`），而 staging 驱动的 YUV420M 输出路径依赖
`MESON_CANVAS_ENDIAN_SWAP64` 把硬件输出的 64-bit 大端字重排成标准 NV12M 字节序。

本移植不复用 GX 的 capture buffer 布局，而是：

- `vdec/meson8b-capture.c`：解码期间硬件直接写入 `dma_alloc_coherent` 的 shadow
  缓冲（独立于用户可见的 vb2 buffer，不污染仍被运动补偿引用的参考帧）；
- 帧就绪后 `meson8b_copy_swap64()`（`vdec/meson8b-copy.h`）在 CPU 上做 8 字节
  字节序交换，拷贝到 vb2 buffer 交给用户空间。
- 因此输出路径含一次 CPU 拷贝（1080p 约 3 MB/帧），不是全硬件零拷贝；
  实测该拷贝使整机 CPU 与耗时均高于纯软解（见 `evidence/ACCEPTANCE-2026-11-13.md`），
  当前价值在正确性与可用性，性能收益需零拷贝路径（ROADMAP P0-2）。

## 仓库布局

```
runtime/    meson8b-runtime.ko 源码（overlay 注入/移除）
vdec/       meson-vdec.ko 源码（Meson8b 平台接入 + capture 转换）
venc/       meson-venc.ko 源码（HCODEC H.264 编码器移植）
firmware/   微码提取脚本与来源记录（bin 本体在 dist/）
scripts/    make-overlay.py、extract-firmware.py、wdt-guard.py、venc-bench.py
tests/      字节序转换单测（host 可跑）、overlay 生成单测、probe 工具
build/      构建产物（.ko、.dtbo、combined.dtb，gitignore）
private/    目标机实测脚本与日志（smoke/1080p 验证循环、CPU 基准）
evidence/   取回的实测证据（帧哈希对比、dmesg、CPU 基准 JSON）
config/     目标机原内核 config 快照
```

## 构建（外部交叉编译机，armhf）

前置：与目标机 5.15.79-meson 匹配的内核源码树与构建目录（本任务用的是
Armbian onecloud `ci-20221118-033748-UTC` 源码线，外层 `baseline/` + `patches/`
重建，详见外层 `README.md`）。

```sh
KDIR=/path/to/linux-5.15.79-output CROSS_COMPILE=arm-linux-gnueabihf- make modules
make dtbo          # 生成 meson8b-runtime.dtbo（也可在目标机上重生成，见下）
# host 单测
cc -O2 -o /tmp/test-copy tests/test_copy.c && /tmp/test-copy
python3 -m unittest discover -s tests
```

`CONFIG_OF_OVERLAY` 必须启用（overlay 注入依赖）。目标机原 config
`5.15.79-meson` 已启用（`CONFIG_OF_OVERLAY=y`），无需换内核。

## 在目标机上部署（手动，一次性）

```sh
# 拷贝 meson8b-runtime.ko meson-vdec.ko meson8b-runtime.dtbo 及固件
mkdir -p /lib/firmware/meson/vdec
cp meson8b_h264.bin /lib/firmware/meson/vdec/
cp meson8b-runtime.dtbo /lib/firmware/meson/vdec/
insmod meson8b-runtime.ko      # 注入 overlay，probe vdec，生成 /dev/video0
insmod meson-vdec.ko           # （runtime 模块已触发 probe，通常随后自动加载）
ffmpeg ... -c:v h264_v4l2m2m ...
rmmod meson-vdec
rmmod meson8b-runtime          # 移除 overlay，恢复原状
```

固件只在第一次 STREAMON 时通过 request_firmware 加载，probe 本身不读固件。
若想对其它同型机器使用，先在目标机上重新生成 overlay：

```sh
python3 scripts/make-overlay.py /sys/firmware/devicetree/base > my.dts
dtc -@ -I dts -O dtb -o /lib/firmware/meson/vdec/meson8b-runtime.dtbo my.dts
```

## 验证（目标机）

`private/1080p-cycle.py` 是完整验收循环：insmod → 软解基线 framemd5 →
硬解 framemd5 → 逐帧比对 → rmmod → dmesg 增量检查 → 服务状态检查。
本地跑法：`ssh root@target 'python3 -' < private/1080p-cycle.py`。
结果证据在 `evidence/`。

已知限制（详见外层 `BLOCKERS.md`）：仅 H.264 解码；分辨率切换/EOS/错误恢复路径
未做长时压测；输出含 CPU 拷贝（P0 后已解决）；编码见上节限制。

## 编码使用（目标机）

```sh
modprobe v4l2_mem2mem && modprobe videobuf2_vmalloc
insmod meson8b-runtime.ko        # overlay 含 vdec + venc 节点
insmod meson-venc.ko             # 生成 /dev/video1（HCODEC encoder）
mkdir -p /lib/firmware/meson/venc
cp meson8b_avc_enc.bin /lib/firmware/meson/venc/   # 首次 STREAMON 时加载

# ffmpeg 7：nv12/nv21/yuv420p（MPLANE 自动别名），宽 16-1920
ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=30 \
    -pix_fmt nv12 -c:v h264_v4l2m2m -f h264 out.h264

rmmod meson-venc
rmmod meson8b-runtime            # 恢复原状
```

EOS drain 按 v4l2-mem2mem 契约（`V4L2_EVENT_EOS` + `V4L2_BUF_FLAG_LAST`）；
同一时刻仅支持一个编码会话。质量/性能测量用 `scripts/venc-bench.py`
（无值守 nohup，须配合 `scripts/wdt-guard.py` 看门狗护栏运行，方法见
`evidence/ENCODE-ACCEPTANCE-2026-11-13.md`）。

## License

内核模块部分 GPL-2.0（同 staging meson-vdec 上游）。微码来自 Hardkernel
GPL 发布，`firmware/sources.json` 记录逐文件 SHA-256。
