# H.264 码率控制 / Rate control

非零 `BITRATE` 且 `FRAME_RC_ENABLE=1` 时，驱动按完成帧的实际字节数调整
下一帧 QP，跟踪目标平均码率。标准 `BITRATE_MODE` 只提供 VBR；CBR/CQ
返回错误。`BITRATE=0` 或关闭 FRAME_RC 切回固定 I/P QP。

With nonzero `BITRATE` and `FRAME_RC_ENABLE=1`, completed-picture bytes feed
the next picture's QP. This tracks a target average rate. `BITRATE_MODE` exposes
VBR only; CBR and CQ are rejected. Zero bitrate or disabled frame RC selects
fixed I/P QPs.

## 配置与恢复 / Configuration and recovery

控件先保存为待应用配置，取下一帧时在控件锁内生效；当前帧始终按选帧时
的配置记账。改码率保留 I/P 复杂度、清空误差桶；改帧周期或 STREAMOFF/ON
新会话清空历史。取帧时消费 force-key 请求，因此 IDR 编码中收到的另一次
请求留给下一帧。GOP1 和密集强制 IDR 使用 I 模型与每帧预算。

Pending controls apply when a picture is selected; that picture is accounted
against the same configuration. Retargeting preserves complexity and clears
the error bucket. A period change or new STREAMOFF/ON session resets history.
Force-key requests are consumed at selection, so a request arriving during an
IDR remains pending. All-IDR and dense forced-IDR runs use the I-picture model
and a per-picture budget.

普通 P 帧保留经硅片验证的 QP32 上限（受用户 QP 范围限制）。溢出恢复使用
独立 QP 下限，每次提高 6，成功帧不撤销该下限。每个硬件会话累计第三次
溢出，或已到 MAX_QP 仍溢出时，两个队列都终止；已排队的后续帧直接返回
错误。STREAMOFF 清空恢复状态。固定 QP 会话保持用户 QP，同样有失败上限。
零字节、过大码量及没有足够空间拼 SPS/PPS 的输出都报错，不截断 slice。

Normal P-picture prediction keeps the silicon-tested QP32 ceiling, subject
to the user's QP range. Overflow recovery has a separate floor raised by six
steps; successful pictures retain it. The third overflow in a hardware session,
or an overflow at MAX_QP, terminates both queues. Already queued pictures return
errors without another hardware attempt. STREAMOFF clears recovery. Fixed-QP
sessions keep their requested QP and the same bounded failure budget. Invalid
sizes and insufficient space for SPS/PPS return errors rather than truncated
slices.

## 媒体时间与观测 / Media time and diagnostics

`S_PARM` 返回采用的帧周期。分子/分母通过有理数近似限制在 65535 内，超过
240 fps 返回 ERANGE；此范围属于 ABI 算术边界，不代表 S805 吞吐能力。
码率与编码耗时无关，按此周期计算媒体时间预算。

`S_PARM` returns the negotiated period, approximated with numerator and
denominator at most 65535. Rates above 240 fps return ERANGE. This is an ABI
arithmetic bound, not a throughput guarantee. Budgets use media time, independent
of how quickly the encoder runs.

```sh
cat /sys/kernel/debug/meson-venc/rate_control
```

| 字段 / Field | 解释 / Meaning |
| --- | --- |
| `frames`, `bytes` | 当前配置下完成的 RC 帧数与码量，包含 SPS/PPS / Completed RC pictures and bytes, including headers |
| `target_bps`, `current_bps` | 目标与约一秒媒体窗口的实际码率 / Target and measured rate over approximately one media second |
| `window_ns` | 当前窗口实际时长，启动阶段可能不足一秒 / Actual window duration, shorter during startup |
| `peak_bps` | 本配置下观测到的最大窗口值，包含启动短窗口；不是严格一秒峰值 / Maximum observed window rate, including startup windows |
| `qp`, `saturated_frames`, `saturated_ns` | 最近 QP、累计达到预测上限的帧数与时长 / Last QP and accumulated saturation |
| `overshoots`, `overflows` | 超目标窗口采样次数与码流缓冲溢出次数 / Window overshoot samples and bitstream overflows |

窗口最多 240 帧；媒体时长采用整数纳秒，边界可能多包含一帧。帧率/目标/
RC 启用状态重置时观测清零。字段在自旋锁内快照，固定 QP 不累计 RC 帧。
连续饱和至少一秒且实际超过目标 10% 时，每个配置记录一次警告。

The window holds at most 240 pictures and uses integer nanoseconds, so its
boundary can include an extra picture. Rate/period/RC restarts reset the
measurement. Readers obtain a locked snapshot. Fixed-QP pictures do not count
as RC pictures. At least one media second of continuous saturation with a rate
over 110% of target produces one warning per configuration.

## 已验证边界 / Verified limits

[2026-11-21 实机记录](../evidence/rate-control-2026-11-21/README.md)将可达目标、
饱和目标、队列终止及失败的工具实验分开记录。640×360 全 I、目标 2.5 Mbps
的稳态码率从 2.900 Mbps 改为 2.441 Mbps；30000/1001、目标 1.6 Mbps
得到 1.588 Mbps。1080p 合成运动在目标 1/2/4/8 Mbps 下仍约 10.35 Mbps，
QP32 饱和；不能把这些结果外推成所有视频的最低码率。

The [silicon record](../evidence/rate-control-2026-11-21/README.md) separates
reachable targets, saturation, queue termination and failed tool experiments.
The 640×360 all-IDR steady rate at a 2.5 Mbps target improves from 2.900 to
2.441 Mbps. At 30000/1001, a 1.6 Mbps target yields 1.588 Mbps. Synthetic
1080p motion still saturates near 10.35 Mbps for targets of 1/2/4/8 Mbps.
These samples do not establish a minimum rate for every video.

没有严格 CPB、峰值保证、重编码或 HRD/VUI。上游 FFmpeg 的 V4L2 包装不映射
`maxrate/bufsize`，因此 OpenList fork 不向该包装传这两个参数，按真实切片
时长/字节声明 HLS 带宽并提供超目标警告。历史切片也不能保证未来场景峰值。
受限网络须根据实测降低尺寸/帧率或使用软件编码。

Strict CPB/peak enforcement, re-encoding and HRD/VUI are not implemented.
Upstream FFmpeg's V4L2 wrappers do not map `maxrate/bufsize`. The OpenList fork
omits these arguments, advertises measured HLS segment bandwidth and reports
overshoot. Past segments cannot bound future scene peaks. Use measured output
to choose smaller geometry/frame rate or software encoding on limited links.
