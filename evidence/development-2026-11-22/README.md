# Deployed development work,2026-11-22

The user explicitly requested the new driver on WanOS. Installed runtime source
is `5fedae9a74d2e6ec03c8fc2f6bf81d011ba82f33`. Original boot kernel,
boot DT,CMA,business container settings and the original v0.4.0 rollback
installation remain preserved. The overall development goal is not complete.

- [Final read-only deployment check](final-state.json): installed manifest verified,
  autoload active/enabled,all five services healthy,OpenList start unchanged.
- [Initial reversible upgrade](initial-upgrade.json) and [installed module identity](installed-driver.json).
- [Container official FFmpeg content checks](deployed-content.json): MP4 timing,
  crop and BT.709 tags via stock `h264_metadata` after explicit colorspace
  conversion; independent decode,H.264300 and HEVC30 hashes.
- [Direct buffer/early-shutdown matrix](buffer-shutdown-matrix.json): all tested
  capture counts and six STREAMOFF/reopen cases,full-range MJPEG24 pictures.
- [Actual browser](browser.json): latest clean image/frontend,real HLS playback,
  uncached forward/backward and end seek; cached seeked12→12,50→50;2846 decoded
  frames,no MediaError,cancel0.242s,poolzero. Buffering observed.
- [Pipeline comparison](pipeline-performance.json),[queue-lock comparison](pool-lock-performance.json)
  and [rejected high-priority experiment](rejected-highpri-performance.json).
  Each file records all runs and300 independently decoded pictures per run.
  Changing production load causes large variation; no1.3× realtime claim.
- [Research build](research.json),[patch order](research-series.log),
  [build tail](research-build.log),[ODROID check](research-odroid.log): full
  Linux7.3.4 kernel/modules and ARM binding checks. Runtime formats are ported,
  but no research-kernel/static-DT or second-board hardware is available.
- [Resolution-switch regression](resolution-switch-regression.json): after
  H.264 shrank back to320×240,first pictures matched later ones.
  [Mailbox isolation fix](resolution-switch-fix.json) restored all130 picture
  signatures at CAPTURE1/4; [deployed switch checks](deployed-resolution-switch.log)
  also pass H.264130 and HEVC50 pictures at CAPTURE1.
- [Decoder exclusivity](decoder-exclusivity.log): two open handles, QUERYCAP
  succeeds while busy, second STREAMON returns EBUSY, takeover after release.
- [Final deployed regression](final-regression.json): four H.264 runs match
  expected counts (300-frame runs match software hashes); MPEG-2 90/90 and
  149/150, MPEG-4 88/90 remain documented tail limits,all exit normally.
- [Rejected drain-count check](rejected-drain-count.json):20 H.264 repeats
  passed; MPEG-4 timed out before correcting submission-count interpretation.
- [Diagnostics](diagnostics.json): temporary rotating kernel log fsynced per
  line to persistent disk. Raw production logs are retained privately.

Implemented: IRQ-thread completion tracking,bounded drain failure,recycle
allocation failure,EOS after actual LAST dequeue distinct from source change,
pipelined conversion with protected slot lifetime,SPS crop/VUI and immutable
streaming frame period,firmware DMA ordering/cancellation. Conventional
commits separate the changes. High-priority workqueue tuning was rejected.

VUI verification initially compared raw `r_frame_rate` to requested fps. Raw
base rate is an estimate,not a timestamped delivery rate; MP4 duration and
average/rational frame rate provide the actual acceptance. Official V4L2
wrappers do not forward every CLI color field; unknown V4L2 metadata is not
invented. Stock `h264_metadata` provides explicit output tags when appropriate.

Further bounded legacy checks exposed a drain-accounting regression: an OUTPUT
submission is not necessarily one decoded picture,so a quiet MPEG-4 firmware
can retain a positive count. The regression is recorded before correction;
quiet firmware flush and genuinely active timeout must remain distinct.
Known MPEG/VC-1 tails,MPEG-4 B-picture content and rare H.264 output history
remain acceptance limits. Never replay pictures or invent frames to pass.

Previous two outages remain unexplained. Production sustained load is paused;
short tests do not establish long-term stability. GE2D has endian register
controls,contrary to an overly broad earlier note,but no validated S805 GE2D
integration is present. End-to-end zero-copy and per-slice early delivery are
not implemented. No HEVC hardware encoding is advertised.

中文：新驱动已部署，新版 OpenList 的浏览器播放、跳转与取消已验证；固件
DMA、IRQ/EOS、异步转换和 VUI 已补齐。性能有波动、旧格式完整性和历史离线
根因未解决。跨板／新内核运行没有实机条件，不能用构建通过代替硬件验收。

## Rollback on this installation

Stop codec clients,then run the prepared local rescue helper:

```sh
python3 /mnt/sda2/vcodec-upgrade-e82a333/rollback-v040.py
```

It verifies saved firmware hashes and idle modules,stages the original complete
installation,stops codecs and restores original firmware/unit/state before
starting v0.4.0. The replaced development package is retained separately.
The helper is syntax-checked and its backup inputs are verified; this final
rollback has not been executed because the user requested the new installation.
The regular package removal path remains in [installation](../../docs/installation.md).
