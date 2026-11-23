# 后续开发

当前能力和实机证据统一见[能力与限制](docs/validation.md)，生产安装来源见
[部署快照](docs/rollout-status.md)。不把历史阶段完成等同于所有目标完成。

| 优先级 | 开发工作 | 验收门槛 |
| --- | --- | --- |
| P0 可靠性 | 闭环第二次 LAN 失联；验证当前包冷启动/异常重启恢复；跟进偶发 H.264 短输出 | 先有可保留故障现场的入口和足够磁盘空间；记录 IRQ/任务/网络证据、完整帧数/内容、业务与回滚。持续生产压力实验仍暂停。 |
| P1 性能 | 测量解码、转换、编码、音频和完整 OpenList 命令的瓶颈，只改已测瓶颈 | 相同1080p输入与应用原命令，目标1.3×实时；不能降质/丢帧后宣称达标。当前未达到。 |
| P2 格式完整性 | MPEG/VC-1 帧尾、MPEG-4 部分 B 帧内容；低目标码率的可达性/失败行为 | 逐帧对齐软件参考，覆盖EOS/取消/重开；不重放猜测尾帧，不盲目提高QP上限。 |
| P3 平台与数据路径 | 研究线实机/跨板验收；有寄存器依据后评估GE2D、少拷贝、逐片提前交付 | 独立开发板或可靠救援入口；内容、DMA生命周期、电源/时钟与收益均有实测。仅构建不算完成。 |

11-23[联网能力复核](docs/capability-recheck.md)重新开放有厂商依据的候选，
用户随后决定先做 P0 队列/错误恢复/共享资源的离线回归。以下均作为后续
推荐 TODO 登记，当前不继续扩展；未测试的 HEVC tiles 草稿已从活动源码移出。

| 推荐 TODO | Issue |
| --- | --- |
| P3 GE2D 字节序转换与减少 CPU 拷贝 | [#14](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/14) |
| P2 HEVC tiles 的几何、SAO 和 MV 移植 | [#15](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/15) |
| P2 H.263 固件、帧边界和 EOS | [#16](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/16) |
| P3 WMV3 Annex L/RCV 集成 | [#17](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/17) |
| P3 AVS JiZhun 标准接口与固件 | [#18](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/18) |
| P3 单 HCODEC 多编码上下文调度 | [#19](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/19) |
| P3 B帧/CABAC/Main/High 编码协议研究 | [#20](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/20) |
| P3 HEVC 长期参考帧协议研究 | [#21](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/21) |
| P3 逐 slice 提前交付协议研究 | [#22](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/22) |
| P2 低目标码率可达性与严格 CBR 条件研究 | [#24](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/24) |

每项 issue 保存固定来源、计划和验收门槛。素材生成/软解、边界测试、
故障注入、sanitizers 和交叉构建优先在工作机做；WanOS 只承担离线无法
证明的短时硬件验收。候选实验不解除生产长测暂停。

下一步先推进 [H.264 #5 时序回归](docs/h264-regression.md)：工作机已经串联
实际 IRQ、PTS、帧池、CAPTURE、LAST/EOS 函数并覆盖确定的回压交错；
历史 299/300 的因果关系及直接 V4L2 实机采样仍未完成。随后再做 P1
性能拆分，格式扩展继续排后。

OpenList 继续适配官方原版 FFmpeg，保留标准发现/调度/错误上报边界；驱动负责
固件、格式、控件和队列协议。禁止以私有 FFmpeg 功能分支解决应用问题。
HEVC 编码仍无适用硬件/微码路径。High/Main/CABAC/B帧编码的可用路径
尚未找到，多会话有厂商调度依据；均不能提前宣称已支持。

历史 v0.1–v0.3 与 v0.4 阶段计划在[历史目录](docs/history/README.md)，不再作为
当前执行清单。新增二进制必须有对应的实机报告，不自动继承旧版验收。
