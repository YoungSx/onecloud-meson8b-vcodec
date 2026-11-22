# 验收证据索引

日期文件保存对应实现的实际结果，不随代码演进改写成当前成功结论。
当前能力和限制汇总在[验证指南](../docs/validation.md)，原在役设备的最终
安装版本和归档 SHA-256 位于[部署快照](../docs/rollout-status.md)。

| 主题 | 主记录 |
| --- | --- |
| P0–P3 开发、恢复/性能/浏览器观察及设备中断 | [Current pass](p0-p3-2026-11-22/README.md) |
| drain 结束权、失败传播、尾帧对照及被否决实验 | [Drain follow-up](drain-2026-11-22/README.md) |
| 官方 FFmpeg 30 分钟、质量、解析竞态及主线重编译 | [2026-11-22 regression](official-ffmpeg-2026-11-22/README.md) |
| 官方 FFmpeg、连续 NV12、1080p 拷贝优化与标准控件 | [Official FFmpeg](official-ffmpeg-2026-11-21/README.md) |
| H.264 解码、CPU、第二会话、分辨率切换 | [2026-11-13 Decode](ACCEPTANCE-2026-11-13.md) |
| H.264 编码与性能 | [Encode](ENCODE-ACCEPTANCE-2026-11-13.md) |
| CBR 与 MPEG-4 | [Rate control / MPEG-4](RC-MPEG4-ACCEPTANCE-2026-11-13.md) |
| VC-1 与显示/帧尾边界 | [VC-1](RC-VC1-ACCEPTANCE-2026-11-13.md) |
| MJPEG 解码 | [MJPEG](P6-MJPEG-ACCEPTANCE-2026-11-14.md) |
| JPEG 编码 | [JPEG](P6-JPEG-ENCODE-ACCEPTANCE-2026-11-14.md) |
| Baseline 并发转码 | [Concurrent](P6-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| B 帧并发及 900 JPEG | [B frames](BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| HEVC Main 8-bit | [HEVC](HEVC-ACCEPTANCE-2026-11-14.md) |
| 编码基线、控件与码率控制（P0/P1） | [Encoder controls](ENCODER-CONTROLS-2026-11-15.md) |
| VBR 帧边界、可达/饱和目标、预排队溢出终止与工具失败 | [Rate control](rate-control-2026-11-21/README.md) |
| 编码输入格式、DMA-BUF、多分片（P2–P4） | [Encoder input](ENCODER-INPUT-SLICES-2026-11-15.md) |
| 解码输出转换、canvas 尺寸与 GE2D 门槛（P5） | [Decoder convert](P5-DECODER-CONVERT-2026-11-15.md) |
| 运行包升级、回滚与旧编码兼容（P6） | [P6 deploy](p6-2026-11-15/deploy.log)、[compat](p6-2026-11-15/compat.log) |
| MPEG/VC-1 drain 根因与帧尾修复 | [drain EOS](p6-2026-11-15/drain-eos.md) |
| VC-1 drain 改动实机复验 | [VC-1 drain](p6-2026-11-15/vc1-drain.md) |
| v0.2.0 运行包部署与 MPEG-2 帧尾复验 | [v0.2.0 deploy](p6-2026-11-15/v020-deploy.md) |
| 30 分钟持续负载（v0.2.0，545 轮 0 失败） | [Soak v0.2.0](p6-2026-11-15/soak-30min-v020.md) |
| 2 小时持续负载（v0.2.0，2205 轮 0 失败） | [Soak 2h](p6-2026-11-15/soak-2h-v020.md) |
| 过夜持续负载（7620 轮，末轮 1 次 299/300） | [Overnight](p6-2026-11-16/soak-overnight-v020.md) |
| H.264 短输出定向复现（空闲 200 次全部 300/300） | [Tail race](p6-2026-11-16/h264-tail-race.md) |
| H.264 短输出混载复现（400 轮全部 300/300，仍未复现） | [Short load](p6-2026-11-16/h264-shortload.md) |
| 五项业务服务并存（71 采样 0 失败、377 轮 0 失败） | [Coexist](p6-2026-11-16/service-coexist.md) |
| 冷启动自动加载（29 s 应用 overlay，两节点就绪，原厂内核未换） | [Reboot](p6-2026-11-16/reboot-autoload.md) |
| 优雅重启（restart 5 s，卸载再加载，节点恢复、解码 22 帧） | [Restart](p6-2026-11-16/restart-check.md) |
| MPEG-4 帧尾逐帧定位与 B 帧缺陷（幅数随流而变，B 帧丢码随内容位置而变，非固定底部） | [MPEG-4 tail](p6-2026-11-16/m4v-tail.md) |
| v0.3.0 运行包部署、回归与 30 分钟负载（525 轮 0 失败），随后换装 Release 包并复验 | [v0.3.0 deploy](p6-2026-11-17/v030-deploy.md) |
| MPEG-2 B 帧流交付 149 或 150 帧（20 次中 14 次完整，短输出缺解码序最后一幅 B 帧） | [B 帧交付](p6-2026-11-17/m2v-bf-delivery.md) |
| 30 分钟持续负载（中断于 191 轮，历史） | [Soak](p6-2026-11-15/soak-30min.md) |
| 两次硬复位与 USB 存储失效 | [Outage](p6-2026-11-15/outage-2026-11-15.md) |
| 最终包校验/服务 | [Final state](hevc-2026-11-14/verified-live-state.log) |
| HEVC 完整帧哈希 | [Content check](hevc-2026-11-14/verified-content-check.log) |
| 构建和源码复现 | [Clean kernel build](hevc-2026-11-14/reproduced-kernel-build.log) |
| 主线 v7.3.4 编译级移植（模块、DTB、binding 检查） | [Mainline port](mainline-2026-11-16/PORT.md) |
| A 阶段帧池：逐帧一致、CAPTURE 1–32、分辨率切换、并发 CMA、余量 2 的 300 轮复现 | [Phase A](phase-a-2026-11-19/README.md) |

`hevc-2026-11-14/verified-deployment.log` 与 `verified-*.md5` 对应最终
`bd1cb79` 包；`final-install.log` 和 `installed-hevc.md5` 属于已被替换的
`b8263f6` 包。`hevc-recovery-interim-failure.log` 保留失败，中间 crop/debug
输出不算最终验收。`reviewed-validation.log`/`reviewed-results.json` 为独立
FFmpeg 的九轮正常流和错误恢复检查。

`encoder-2026-11-15/` 的结果来自替换编码模块的候选版本，测试后恢复了当时安装的
`bd1cb79`；这些功能随后随 v0.2.0 运行包部署，见
[v0.2.0 deploy](p6-2026-11-15/v020-deploy.md)。

主机 CI 日志属于构建回归，不能冒充新的 S805 硬件验收。历史阶段中的
“未实现”、两模块、服务基线等描述只适用于记录当时。
