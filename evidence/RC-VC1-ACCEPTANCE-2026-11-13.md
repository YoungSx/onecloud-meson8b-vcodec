# P4-17 VC-1 / AVS 解码验收记录（2026-11-13/14）

## AVS — N/A（集成层无落点）

- 内核 uapi（5.15 videodev2.h）无 `V4L2_PIX_FMT_AVS`，V4L2 无
  AVS 落点可注册。
- ffmpeg（Ubuntu 20.04 4.2 / v4l2m2m wrapper）无 avs 的 v4l2m2m
  解码包装，用户态亦不可达。
- 结论：即便移植 codec_avs.c 也无法被任何用户态消费。搁置。

## VC-1 advanced profile — 通过

### 固件

`vc1_vc1_linux.h` 的 `MicroCode[]`（3293 词，plain
`const unsigned[]` 声明 —— 经 vc1_mc.c `#define MicroCode vc1_mc`
include 进来，与 h264/mpeg12 的 `u32[] __attribute__((aligned(8)))`
不同），16K 零填充。`_words()` 正则扩展接受两种声明形式。
输出 `meson8b_vc1.bin`（16KiB）
sha256 `af8166265eccd516d3a6bf43aaa58c2e6bc35e5869634f39d43c1c30555e1976`。

### 协议（vendor M8 vvc1.c + GXL 4.9 vvc1.c 交叉移植）

寄存器映射与 mpeg4 同族：PIC_RATIO=SCRATCH_0、SOS_COUNT=SCRATCH_7、
BUFFERIN=SCRATCH_8、BUFFEROUT=SCRATCH_9、OFFSET=SCRATCH_C、
MEM_OFFSET_REG=SCRATCH_F。缓冲握手同 mpeg4：4 解码槽
`((reg&7)-1)&3`、回收 `~(1<<idx)`、隔行标志 BIT(7)/BIT(6)。

VC-1 特有（初版遗漏第 1 条即 0 帧输出）：

1. **AV_SCRATCH_4 是格式选择**：1 = advanced profile（WVC1，
   annex-G 裸流），0 = simple/main（WMV3）。必须在微码启动前写入。
   首版漏写 → 微码按 WMV3 解析 annex-G 流，无任何 IRQ。
2. VIFIFO 水位：厂商仅对 vc1/avs 抬高
   `VLD_MEM_VIFIFO_CONTROL = (2 << MEM_FIFO_CNT_BIT) | (8 << 18)`
   （mpeg12/mpeg4 不动）。漏写未复现失败，按厂商保持。
3. workspace **独立 2MB DMA**（GXL 模型，M8 厂商是塞在保留区
   解码槽后面），`MEM_OFFSET_REG = paddr - 0x01f00000`
   （DCAC_BUFF_START_ADDR；mpeg4 是 1MB @ 0x01400000）。
4. M8B 复位序列与 mpeg4 不同：`BIT(7)|BIT(6)|BIT(4)` 脉冲
   **两次**，再 `BIT(9)|BIT(8)` 脉冲。

drain：VC-1 end-of-sequence（0x0000010A）+ IRQ 静默冲刷，同
mpeg12/mpeg4 已验证模式。

### 验收数据（2026-11-13/14 目标机）

5 个 fate 样片，`vc1_v4l2m2m` 硬解 vs 目标机软解（同 ffmpeg）：

| 样片 | 分辨率 | 特征 | hw 帧 | sw 帧 | hw 唯一哈希 |
|---|---|---|---|---|---|
| SA00040 | 176x144 | I/P/B, RPS 重复 | 5 | 15 | 5 |
| SA10091 | 320x240 | I/P | 12 | 30 | 12 |
| SA10143 | 176x144 | I/P | 28 | 30 | 28 |
| SA20021 | 1920x1080 | I/P | 30 | 60 | 30 |
| ilaced_twomv | 720x480 | 隔行 two-MV | 11 | 13 | 11 |

- **唯一帧哈希集合 hw ⊆ sw，hw_only = 0（全部样片 bit-exact）**
  —— 远强于 mpeg4 的 0.06dB PSNR 判据。
- sw 多出的帧是 VC-1 RPS 重复显示帧（SA00040：15 显示帧 = 7
  唯一图）：软解按 repeat 输出多份，硬件每幅解码图恰好出一帧。
  厂商用 VC1_REPEAT_COUNT 补发重复帧，属显示特性非解码正确性，
  未移植。
- 每片固定差 2 帧：EOS drain 边界，同 mpeg12/mpeg4 已知行为。
- 内核零错误（dmesg 仅 "loaded firmware"），隔行标志正确传递
  （BT/TB）。
- 干净模块（去除调试 trace）重测同样结果。

### 回归（VC-1 加入后 mpeg12/mpeg4 无回归）

320x240 × 3s@30 testsrc2（g=30 mpeg2 / g=1 mpeg4）：

- mpeg2 88/90 帧、mpeg4 88/90 帧（差 2 帧 drain 边界，与基线
  一致；逐帧 PSNR hw vs sw 全部 ≥40dB，仅末 2 帧 19-21dB drain
  边界 —— 与 P4-16 mpeg4 验收记录同型）
- 帧数有 88→74 的运行间波动（drain 冲刷时序），内容判据不受影响
- dmesg 零错误

### 未选通项

- **WMV3（simple/main）**：翻转 AV_SCRATCH_4=0 即可选通，但
  fate 无 WMV3 样片、无处验收，留待有真实样片时补。
- **Annex L（打包 WMV9 in ASF）**：同上，且 V4L2 fourcc 只有
  VC1_ANNEX_G 一个落点。

### 工程记录

- DERP 中继 scp 大文件（>400KB）静默截断（无报错、连接正常
  关闭）：ilaced_twomv.vc1 2.4MB 传成 2.0MB、regress.m2v 传成
  160KB。解法：`split -b 400k` 分块 + 每块 md5 校验重传 + 目标机
  `cat` 重组。后续传输超过 400KB 一律分块。
- 唯一哈希集合比较是 VC-1 验收的正确判据：帧数对比（5/15、
  12/30、30/60）初看像"丢帧"，实为 RPS 重复帧 + drain 边界的
  展示层差异。与 P4-16 的 ts-0 假象同属"验证内容而非数量"。
