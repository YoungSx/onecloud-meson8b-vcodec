# P4 码率控制 + MPEG-4 解码验收记录（2026-11-13）

## CBR 码率控制（venc）— 通过

### 设计

厂商 amvenc 无硬件码率控制：`request.quant`（cmd_info[6]）由用户态
逐帧提供，经 `avc_prot_init`（QDCT_Q_QUANT_I/P）与
`avc_init_ie_me_parameter`（P_MB_QUANT_CONFIG = HENC_SCRATCH_I）生效；
ISR 在每幅完成后置 need_reset → 下一帧 prot_init 重跑 → **逐帧 QP
可变**。据此实现加法式 CBR：

- 每幅完成后比较累计比特 vs `bitrate/fps × frames`
- 超预算 QP += 4；欠超过一帧预算 QP -= 1；clamp [10, 48]
- `V4L2_CID_MPEG_VIDEO_BITRATE` = 0（默认）关闭，固定 QP 26；
  非零开启并把控制器重置到 QP 26 起步
- 预算速率来自 `VIDIOC_G/S_PARM` timeperframe（默认 30fps）

### ffmpeg 交互（重要发现）

- ffmpeg `h264_v4l2m2m` **总是**发送 `avctx->bit_rate`（默认 200000）
  → BITRATE 控件默认必须为 0（关闭），否则"固定 QP"会话被静默开启
  CBR；ffmpeg 侧 `-b:v 0` 是显式关闭通道。
- v4l2 ctrl core 对"值未变化"的 set 跳过 s_ctrl → 控件默认值不能
  与常用目标重合（首版默认 4000000 使 `-b:v 4M` 完全不生效，输出
  与纯 QP26 逐字节相同）。
- QP40 下 testsrc2 1080p30 底限约 4.27Mbps —— 2M 目标物理不可达，
  测试目标改为 5M/8M。

### 数据（1080p30 × 150 帧，testsrc2，nohup 无值守）

| 会话 | 目标 | 实测 | 误差 | PSNR vs 源 |
|---|---|---|---|---|
| fixed（-b:v 0） | — | 12848 kbps | —（= QP26 基线，逐字节一致） | 45.62 dB |
| cbr5m | 5000 kbps | 4930 kbps | -1.4% | 15.49 dB |
| cbr8m | 8000 kbps | 7978 kbps | -0.3% | 17.70 dB |

- 收敛误差 ≤1.4%；PSNR 随码率单调（15.5 < 17.7 < 45.6）。
- fixed 会话与 P3-12 基线（8.03MB / 45.6dB）完全一致 —— CBR 关闭
  时零回归。
- 全程 gateway 可达，无内核告警。

## MPEG-4 Part 2 解码（vdec）— 通过

### 固件

`vmpeg4_mc_5[]`（MPEG4_5 变体，DivX/Xvid）3196 词 —— 与 mpeg12/
avc_enc 不同，**无尾部零哨兵**（末词 0x00400000 是真代码）；
`extract-firmware.py` 的 `_words()` 增加按条目 `sentinel` 开关。
输出 `meson8b_mpeg4.bin`（16KiB 零填充）
sha256 `7a342188a26128b048371088b4d11375688df37eb2faae0aec3b90a9b7fd7710`。

### 协议（相对 vendor 3.10 vmpeg4.c 的修正）

初版按 mpeg12 惯例移植有 4 处错误，已按 vendor M8 源码逐一修正：

1. `buffer_index = ((reg & 0x7) - 1) & 3` —— 4 个解码槽
   （mpeg12 是 8 槽 `(reg & 0xf) - 1) & 7`，不可混用）
2. 回收握手：`MREG_BUFFERIN = ~(1 << idx)`（反相位掩码；
   mpeg12 是 `idx + 1`）
3. `AV_SCRATCH_F` 是 **MEM_OFFSET_REG**（`workspace_paddr -
   0x01400000`，ucode 以 ORI_BUFFER_START_ADDR+4MB 为基地址的
   换算），不是 fatal-error 标志；M8 vendor mpeg4 根本没有
   fatal-error 寄存器（GXL 4.9 移植用 AV_SCRATCH_L，M8 微码不同）
4. `MP4_OFFSET_REG`（AV_SCRATCH_C）是**固件写入**的流偏移
   （PTS 查找用），驱动只读 —— 不得在 start() 写入

其余沿用已验证的 mpeg12 模式：M8B DOS_SW_RESET0 BIT(7)|BIT(6)
复位 + 1MB workspace、POWER_CTL_VLD BIT(4)、MDEC_PIC_DC_THRESH
0x404038aa、MPSR 释放后快照 canvas 表 → resume() 中 set_canvases +
处理器重启、drain 用 visual object sequence end（0x000001B1）+
IRQ 静默冲刷。NV21 canvas 索引表不需要：set_canvas_nv12m 写入的
`v<<16|v<<8|y` 格式已被 mpeg12 验证可直接被微码接受。

### 验收数据（2026-11-13/14 目标机，重启后复测）

- 320x240 全 I 帧（g=1，150 帧）：148 帧到用户态（差 2 帧为
  drain 边界，同 mpeg12 已知行为）
- 720p30 全 I 帧（g=1，150 帧）：145-148 帧到用户态
- BUFFEROUT 槽位 0-3 轮转正常，回收握手（BUFFERIN=0）全程正常，
  无 m2m 缺 buffer、无 abort
- 720p hw 解 PSNR vs 源 28.93dB，对照软解 vs 源 28.99dB
  （差 0.06dB，IDCT 实现差异；第 112 帧起劣化是 testsrc2 源内容
  突变 + q:v 4 量化，软硬一致）
- 320x240 帧级：148 帧中 146 帧 PSNR 45+dB，末 2 帧 drain 边界
  22-25dB（与 mpeg12 相同的 EOS 冲刷边界）
- CPU（720p30×150）：软解 user 3.35s → 硬解 user 0.21s（**-94%**）
- mpeg12 回归：88/90 帧（与 mpeg4 之前基线一致，无回归）
- 卸载模块回退验证通过

### "只到 2 帧"假象的根因（排查记录）

裸 `.m4v` 流无时间戳 → ffmpeg OUTPUT buffer timestamp 全 0 →
驱动 offset 匹配后回填的 CAPTURE 时间戳全 0 → framemd5 的
framesync 视为重复帧丢弃（日志 `*** dropping frame 2 at ts 0`）。
加 `setpts=N/(30*TB)` 重定时后全部帧正常输出。与 ROADMAP P3-12
记录的裸流 PSNR 坑同源。驱动侧 offset 匹配仅 1 次 miss（首帧
0xF590C，未越过任何已登记 offset，正常）。

### 目标机事故记录（不影响验收结论）

- ~14:30 UTC：`/tmp` tmpfs（496M）被 620MB 测试 YUV 填到 90% →
  sshd exec 失败（auth 成功但 exec exit 1 零输出；web 80 正常）。
  清理后恢复。
- ~15:25 UTC：整机死机（tailscale 离线 33 分钟），重启后恢复；
  tailscaled 的 ssh incubator 亦崩过一次（Go runtime 崩溃，属
  tailscale 自身问题，重启进程即恢复）。
- 教训：大体积 rawvideo 中间文件不要落 /tmp（tmpfs 与内存共享），
  后续测试直接管道或落 /root（eMMC）。
