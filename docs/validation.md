# 验证范围与证据

下列结果来自 **2026-11-20 及之前的 OneCloud 实机记录**，原内核
`5.15.79-meson`，64 MiB CMA。2026-11-14 完整验收对应 `bd1cb79`；
在役机 2026-11-15 起安装 `c20a473`（v0.2.0 包），2026-11-17 起安装
v0.3.0 正式发布包（CI 从 `abee6f6` 构建，驱动代码与实测的 `0a0542b`
本机构建逐字节相同；含 P1–P5 编码扩展、NEON、MPEG/VC-1 drain 修复与
EOS 超时可观测）。2026-11-20 起安装 v0.4.0 正式发布包（CI 从 `89dd558`
构建，帧池模块与 2026-11-19 的 Phase A 候选逐字节相同），安装后回归、
30 分钟 441 轮负载与五项服务并存均 0 失败，见
[v0.4.0 deploy](../evidence/p6-2026-11-20/v040-deploy.md)；帧池本身的
矩阵验收见 [Phase A](../evidence/phase-a-2026-11-19/README.md)。后续文档、
打包和 CI 变更不自动继承新二进制的硬件验收。[部署快照](rollout-status.md)
包含包哈希和服务状态。

| 项目 | 已验证结果 | 证据 |
| --- | --- | --- |
| 帧池与 CAPTURE 解耦（v0.4.0 候选） | H.264/HEVC/MPEG-2/MPEG-4/VC-1/MJPEG 在 CAPTURE 1、2、4、20、32 与 FFmpeg 默认参数下逐帧一致；分辨率切换两编解码器 12/12；1080p 解码与编码并发；30 分钟 439 轮与五项服务并存 0 失败 | [Phase A](../evidence/phase-a-2026-11-19/README.md) |
| H.264 解码 | 1080p 300 帧逐帧一致；后续独立回归 60/60 一致 | [Decode](../evidence/ACCEPTANCE-2026-11-13.md) |
| HEVC Main 8-bit | 320×240 10/10、1080p B 帧 30/30、854×480 crop 10/10 哈希一致；重复会话和恢复 | [HEVC](../evidence/HEVC-ACCEPTANCE-2026-11-14.md) |
| H.264 编码 | 三种原始格式、多尺寸、CBR、强制关键帧；1080p30 编码 CPU 对比软编降低约 75% | [Encode](../evidence/ENCODE-ACCEPTANCE-2026-11-13.md) |
| 编码控件与码率控制（候选模块） | QPPICTURE 修复后变 QP 码流可解码；I/P QP、QP 范围、GOP 0、码率目标误差 motion/scene ±1.8% 内 | [Controls](../evidence/ENCODER-CONTROLS-2026-11-15.md) |
| 编码输入、DMA-BUF、分片（候选模块） | 9 种新增输入格式逐字节/PSNR 检查；非整宏块宽度原尺寸解码；连续 DMA-BUF 直读；整行多分片 | [Input](../evidence/ENCODER-INPUT-SLICES-2026-11-15.md) |
| JPEG 编码 | 八组输入矩阵；3×300 张 1080p 图像软解；质量、时间戳、互斥及 EOS | [JPEG](../evidence/P6-JPEG-ENCODE-ACCEPTANCE-2026-11-14.md) / [Long run](../evidence/BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| H.264 并发转码 | 1080p B 帧输入十轮 60 帧完整；PSNR 最低 45.03 dB、平均 45.82 dB | [Concurrent B frames](../evidence/BFRAME-TRANSCODE-ACCEPTANCE-2026-11-14.md) |
| 解码输出转换（候选模块） | NEON 后 1080p 每帧 6.8–8.3 ms（标量 15.2–16.1 ms）；左偏移窗口 7.3–8.5 ms；1360x768/1600x900 canvas 修复后哈希一致 | [Decoder convert](../evidence/P5-DECODER-CONVERT-2026-11-15.md) |
| MPEG-1/2 drain 帧尾 | 90 帧参考流由 88 恢复为 90（99% 像素与软件参考相同，差异为舍入）；150 帧 B 帧流交付 149 或 150 帧（20 次中 14 次 150），短输出缺解码序最后一幅 B 帧，其余帧在位一致 | [Drain EOS](../evidence/p6-2026-11-15/drain-eos.md) / [B 帧交付](../evidence/p6-2026-11-17/m2v-bf-delivery.md) |
| VC-1 drain 复验 | 两个 FATE 样片唯一图 6/7、14/15，硬解图像与软解逐字节相同，比旧模块多恢复末一幅 | [VC-1 drain](../evidence/p6-2026-11-15/vc1-drain.md) |
| v0.2.0 运行包部署 | 包校验 52 文件通过，安装后服务 active；MPEG-2 90 帧复验、H.264/HEVC 解码、H.264/JPEG 编码回归通过 | [v0.2.0 deploy](../evidence/p6-2026-11-15/v020-deploy.md) |
| v0.3.0 运行包部署 | 包校验 53 文件通过，安装后服务 active、四个 HTTP 入口 200；MPEG-2 90 帧 md5 与 v0.2.0 相同，H.264/HEVC 解码与 H.264/JPEG 编码回归通过 | [v0.3.0 deploy](../evidence/p6-2026-11-17/v030-deploy.md) |
| v0.4.0 运行包部署 | 包校验 54 文件通过，编解码设备中断 21 s，安装后 unit active/enabled、两节点均在、模块指纹与包内一致、固件未变；MPEG-2 90 帧 md5 与 v0.3.0 相同，H.264/HEVC 解码与 H.264/JPEG 编码回归通过，五项服务全部 200 | [v0.4.0 deploy](../evidence/p6-2026-11-20/v040-deploy.md) |
| 30 分钟持续负载（v0.4.0） | 441 轮 0 失败；441 次 HEVC 30/30 与 88 次 H.264 300/300 逐帧哈希全部匹配；CMA 与可用内存无单调下降，窗口内 0 条内核警告；五项服务并存 106 次采样 0 失败 | [Soak v0.4.0](../evidence/p6-2026-11-20/v040-deploy.md) |
| 30 分钟持续负载（v0.3.0） | 525 轮 0 失败；HEVC 30/30 与 H.264 300/300 逐帧哈希全部匹配；CMA 与可用内存无单调下降，窗口内 0 条内核警告 | [Soak v0.3.0](../evidence/p6-2026-11-17/v030-deploy.md) |
| 30 分钟持续负载 | 545 轮 0 失败；HEVC 30/30 与 H.264 300/300 逐帧哈希全部匹配；CMA 与可用内存无单调下降，dmesg 无警告 | [Soak v0.2.0](../evidence/p6-2026-11-15/soak-30min-v020.md) |
| 2 小时持续负载 | 2205 轮 0 失败；66150 帧 HEVC 与 132300 帧 H.264 逐帧哈希全部匹配；CMA 与可用内存无下降趋势，dmesg 无警告 | [Soak 2h](../evidence/p6-2026-11-15/soak-2h-v020.md) |
| 原有服务与回滚 | OpenList、Arcane、Home Assistant、SublinkPro、VoCat 均运行；加载/卸载/固件恢复通过 | [Final state](../evidence/hevc-2026-11-14/verified-live-state.log) / [Autoload](../evidence/hevc-2026-11-14/autoload-cycle.log) |
| 设备枚举 | v4l2-ctl 枚举两个设备、格式和控件 | [Enumeration](../evidence/hevc-2026-11-14/v4l2-enumeration.log) |
| 干净源码构建 | 原配置完整构建、三个外部模块哈希一致、additive 补丁/Kbuild | [Build](../evidence/hevc-2026-11-14/reproduced-codec-build.log) / [Hashes](../evidence/hevc-2026-11-14/reproduced-module-hashes.txt) |

## 限制

- HEVC Main10、tiles、长期参考帧未支持或广告；当前 8-bit 线性路径。
- H.264 encoder 无 B 帧；一个 encoder open，共享 HCODEC 的 JPEG/AVC 互斥。
  `bd1cb79` 的 CBR 码流解码错误、设置 MIN/MAX_QP 会死锁，`cd4e6b5` 已修复。
- 编码码流为 Baseline、无 VUI；码率控制的 P 帧 QP 不超过 32，低于该码率的
  目标不可达。分片随整帧交付，没有逐片低延迟。单帧码流上限 1 MiB。
- RGB24 最宽 1376，三字节 BGR 不支持；运动搜索随 DRAM 时序变化，有运动的
  码流在多次运行间不逐字节一致。
- VC-1 advanced 的唯一图像与软解逐字节相同，RPS 重复显示帧不补发。
  MPEG-1/2 帧尾已修复（EOS 此前从未写入解析器），参考流 90/90；VC-1 同构
  改动在两个 FATE 样片上复验为 6/7、14/15 幅唯一图，仍差末一幅。MPEG-4
  当前仍缺尾帧，幅数随流而变：2026-11-16 逐帧对齐证明参考流追加末幅编码
  VOP 两次、再送序列结束码可交付 90/90 且逐帧对应软解 0–89（delta 全 0，三轮
  一致），但盒子现编的 50 VOP I/P 流要追加三次才补齐（k=2 三轮里抖动为
  49/49/50），多补一幅就多一帧已交付图像的重复。幅数不可从驱动侧观测，因此
  驱动未改，对外声明为"尾帧缺失 1–3 幅，随流而定"，不写固定帧数。
  另：B 帧与帧尾无关但同样未修——同一固件上，testsrc 的每一幅 B 帧都丢同一块
  区域（这条流上恰好是底部 4 个宏块行，y 192–239 整行为 0、y 176–191
  部分为 0），q2/q5 完全相同；同内容全 I 流与全画面运动内容（testsrc2）的 B 帧逐帧精确。
  用 vflip 把同一条流翻转后底部恢复、22 幅里只剩 1 幅出错，所以缺陷**不固定在
  底部**，也不是"B 帧"本身，而是"B 帧 + 内容位于画面下部"的组合。此前 MPEG-4
  验收只用全 I 帧流，未覆盖 B 帧。
  见 [VC-1 drain](../evidence/p6-2026-11-15/vc1-drain.md)、
  [MPEG-4 帧尾与 B 帧](../evidence/p6-2026-11-16/m4v-tail.md)。
- MJPEG 带厂商全量程色度增益，不能声称与软解 bit-exact。
- 分辨率切换由标准 V4L2 客户端验证 350 帧；FFmpeg 7.0.2 reinit 仍可能
  等待持有的 buffer ref。独立 drain 补丁不宣称修复所有 reinit 行为。
- 900 张 JPEG、十轮并发、30 分钟与 2 小时持续负载都是有界回归，非无限期
  生产 soak。过夜窗口跑满 6 h 54 min / 7620 轮，末轮出现 1 次 H.264
  300→299 帧短输出（约 1/1700 次 H.264 运行）；随后空闲单跑 200 次与
  复刻过夜顺序的混载 400 轮全部通过，根因未定，见
  [过夜记录](../evidence/p6-2026-11-16/soak-overnight-v020.md)、
  [空闲复现](../evidence/p6-2026-11-16/h264-tail-race.md) 与
  [混载复现](../evidence/p6-2026-11-16/h264-shortload.md)。
- 未替换启动内核。自动加载已在 2026-11-16 冷启动中验收：内核启动 29 s 时
  overlay 已应用、`/dev/video0` 与 `/dev/video1` 已注册，单元 `enabled`、
  `active (exited)`、退出码 0，四项业务服务端口全部返回 200；此次是复位后的
  冷启动而非优雅重启，开机后只做了 MPEG-4 解码冒烟，未跑全部 codec 回归
  （见 [重启验收](../evidence/p6-2026-11-16/reboot-autoload.md)）。优雅重启路径
  已于 2026-11-17 单独验收：`systemctl restart` 走 ExecStop 卸载（撤 overlay、
  卸三个模块）再 ExecStart 加载，5 s 完成、rc=0、两节点恢复、随后解码 22 帧
  正常（见 [重启检查](../evidence/p6-2026-11-16/restart-check.md)）。主线移植只到编译级：v7.3.4 上模块、
  DTB 与 binding 检查通过，未上 S805 实机；`0013` 实现了采集字节序转换，
  STREAMON 不再拒绝，`0014` 在调用方允许时改用 NEON，但该路径未经实机
  验证、驱动内收益未测量；上游结论记在 Discussions，不向上游提交。
- CPU 输出字节序转换与编码输入拷贝存在，非端到端零拷贝；NEON 转换
  自 `cd4e6b5` 起包含在安装包中，当前在役机的 v0.3.0 发布包同样包含。
- 解码输出按 canvas 对齐尺寸（32 字节行宽、32 行高）补零，`bytesused`
  即该尺寸；左偏移不是 8 字节倍数的裁剪窗口走逐字节路径，约 21–23 ms/1080p。

旧 codec 详细记录：[MPEG-4/CBR](../evidence/RC-MPEG4-ACCEPTANCE-2026-11-13.md)、
[VC-1](../evidence/RC-VC1-ACCEPTANCE-2026-11-13.md)、
[MJPEG](../evidence/P6-MJPEG-ACCEPTANCE-2026-11-14.md)。

## 主机 CI 与实机检查

`make test` 复现微码，运行 Python 测试及 C ASan/UBSan。CI 还交叉构建原
基线、检查补丁同步和包校验，但不连接生产设备、不宣称硬件测试通过。

实机验证时记录 source commit、模块哈希、样片 metadata、完整帧数和图像
内容。保留软解参考，对照输出帧哈希：

```sh
python3 scripts/verify-hashes.py software.md5 hardware.md5
```

持续负载用 `bash scripts/soak.sh <分钟>`：每轮 HEVC 1080p 解码比对软件
参考、每五轮 H.264 1080p 解码，每轮 H.264 与 JPEG 编码，记录温度、内存、
CMA 与设备状态，首次失败即停止。

服务共存在同一负载上跑 `bash scripts/service-coexist.sh <分钟> [采样秒]`：
它把上面的负载放到后台，按采样间隔探测 OpenList、SublinkPro、VoCat 与
Home Assistant 的 HTTP 入口，并以自身日志行推进确认 Arcane agent 在服务；
任何服务不可达或负载失败都记为失败。短输出等偶发问题用
`bash scripts/h264-tail-race.sh <次数>` 定向重复，保留首个差异行；它在空闲
机器上单跑该解码，而唯一一次真实失败发生在混载中，所以
`bash scripts/h264-shortload.sh <轮数>` 复刻负载顺序（先 HEVC 解码，再被测
H.264 解码，之后 H.264/JPEG 硬件编码），同样在首次短输出时保留
framemd5 与 dmesg 尾部。MPEG-2 B 帧流的交付帧数用
`bash scripts/m2v-bf-delivery.sh <次数>` 逐次统计：同一输入得到 149 或
150，单次 150 不能当作结论。

缺失/损坏固件应有界失败，恢复后新会话应正常。损坏流可能输出部分帧且
FFmpeg 返回 0；验收必须检查内容。完整测试后核查服务、内存回收、dmesg
增量和卸载回滚。原始失败日志保留，最终记录及历史区别见
[证据索引](../evidence/README.md)。
