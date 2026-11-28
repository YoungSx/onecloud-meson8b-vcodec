# 后续开发

2026-11-26 按源码、部署记录与原 issue 关闭条件核对后的任务索引。
最新在役包为 `2fea36e`，默认NEON、GE2D关闭，已部署单平面NV12直接读取；
补齐GE2D首样本重复计数，独立decoder缩放272幅、真实共享转码244幅内容
通过，包含原失败854→320及尺寸切换。此前600幅精确解码、185幅编码
及小尺寸AAC/HLS按对应二进制记录，不代表完整厂商追平或1.3×吞吐已完成。
能力、精确部署来源和版本结论分别见[能力与限制](docs/validation.md)、
[部署快照](docs/rollout-status.md)及[验证记录](docs/validation-records.md)。
下表是本次核对快照，后续状态以链接的 issue 为准。

## 首先推进的功能主线

按用户2026-11-26选择，优先把减少图像拷贝、GE2D缩放与图像处理做成有
内容/性能证据的完整数据路径。以下是开发执行顺序；P0仍表示故障严重度，
不要求等待所有历史根因关闭才开展主机实现、回归和构建。

| 顺序 | 优先级 | 工作与下一交付 | Issue |
| --- | --- | --- | --- |
| 1 | P1 | 减少拷贝：已通过 `1080p->720p` DRM dumb(4) `vdec->venc` 60/60（`direct=60, copy_ns=0, wall=6342 ms`）与 12+ `1080p` dumb 耗尽 64 MiB CMA 测界；标准 `1080p` 管道收益与长稳待完成 | [#14](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/14) |
| 2 | P1 | GE2D缩放：`1080p->720p` 60/60 已通过回压及两种 `STREAMOFF`+重开；标准客户端及 `1080p` 全链待完成 | [#32](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/32) |
| 3 | P1 | 持续测量并闭环官方工具/OpenList原命令的1080p视频+AAC/HLS收益；官方 7.0.2 `1080p` 300/300 约 `0.857×`，目标仍为 `1.3×` | [#7](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/7) |
| 随主线分阶段 | P1 | GE2D图像处理：扩展格式/矩阵/旋转及 9/9 纹理 alpha 混合（`max_err <= 1`）与单队列取消通过；硬件超时复位矩阵及生产默认启用待补 | [#31](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/31) |

#14和#32共享的DMA同步、超时停机、取消、引用/映射寿命问题随实现处理。
已完成单平面NV12直接读取、真实decoder/GE2D→encoder共享面交接的有界子集；
下一步补完整缓冲寿命/失败恢复和官方工具/应用收益验收。
这里是增量工作顺序，不要求先关闭前一个issue才开始下一个：#14先完成
可供#32使用的布局/交接增量，两者衔接推进，#7从基线起同步测量。
#31提升优先级不改变原关闭范围，其旋转/混合等后续阶段排在基础少拷贝/
缩放链收益之后。#7先记录基线并持续比较，不等最后才测，也不从阶段差值
直接推断纯硬件耗时。减少拷贝不预先承诺端到端零拷贝。

## 故障严重度与验收门槛（并行跟踪）

| 优先级 | 工作与尚未完成的门槛 | Issue |
| --- | --- | --- |
| P0 | 第二次LAN/Tailnet同时失联的独立现场、机制与修复；不套用第一轮共享时钟解释 | [#4](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/4) |
| P0 | containerd/FFmpeg/SFTP运行时异常及瞬时哈希差异取证；与网络失联分开，根因未知 | [#34](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/34) |
| P0 | 历史H.264 299/300与IRQ/LAST/EOS的因果关系；已有主机重放和300/300短测，仍按原选择暂停主动复现 | [#5](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/5) |
| P1 | 当前包冷启动、异常重启及实际v0.4.0整包降级；热升级和备份不能替代 | [#6](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/6) |

素材、软件参考、真实函数回归、故障注入、sanitizers和交叉构建在工作机
完成；WanOS只承担无法离线证明的必要有界验收。保持官方原版FFmpeg与
安全固定父钟，不修改生产boot DT/CMA。生产持续压力仍暂停，短测或新
源码不自动继承其他二进制的验收。官方FFmpeg的`-vf scale`仍走CPU；
decoder缩放接口接入完整标准链的内容与收益尚未验收。
候选实机操作前必须有相应取证和回退条件；新异常先保全现场并恢复服务。
这些门槛不阻塞无设备操作的开发，#6重启/降级另安排有恢复入口的验收窗口。

## 第二批：格式完整性与剩余验收

少拷贝/缩放基础链后，优先处理现有格式的真实错误与覆盖：#8–#11、#17
尾帧/内容，#15/#16剩余验收，再做#24码控。#33原厂资料与参考可先收集，
不以缺少原厂同机数据阻塞当前性能基线；实际切换系统必须另满足救援条件。

| 工作 | 当前进度与剩余条件 | Issue |
| --- | --- | --- |
| VC-1 advanced | 已增加 `VC1_REPEAT_COUNT` 诊断并明确 V4L2 单解码图像每缓冲语义；`SA00040` `6/7`、`SA00050` `14/15`（已交付全平面零误差），尾图与隔行/HD 仍缺 | [#9](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/9) |
| MPEG-4 Part2尾帧 | 修正 `MESON8B` 工作区后 `90` VOP 仍 `88/90`、`50` VOP `48/50`，不重放VOP或补造图片 | [#10](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/10) |
| MPEG-4 B图内容 | `testsrc2`/`vflip` `28/30` 无全零帧，但 `testsrc-q2` 行11起 MB `(6,11)` 写入停止仍在（`min_Y_PSNR 11.26 dB`） | [#11](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/11) |
| WMV3 simple/main | Annex L/RCV已部署，`320×240` 3/5 通过、`3/5` 尾帧仍缺；官方 FFmpeg 7.0.2 无 `wmv3_v4l2m2m` | [#17](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/17) |
| 码控 | 实测 `640×360 @ 500k`（`qp=32` 饱和 `57/60`）与 `1080p @ 1000k`（`qp=32` 饱和 `28/30`）；固定 `encoder.c` 无 S805 CBR 路径，继续评估 | [#24](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/24) |
| 原厂对照 | 获取原厂配置及可恢复环境，按同输入/质量/内存分别比较播放、解码和转码 | [#33](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/33) |

## 尚未移植的厂商路径与协议研究

新增格式和协议研究排在少拷贝/GE2D基础链与现有格式完整性之后。标准UAPI和同芯片微码/寄存器依据
先于能力注册，不借其他FOURCC或后代IP伪装支持。

| 工作 | 跟踪 |
| --- | --- |
| AVS JiZhun标准接口与微码移植（核对 `5.15.79` `videodev2.h` 及 FFmpeg 7.0.2 仍缺映射） | [#18](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/18) |
| RealVideo8/9接入与RV10边界（核对 `vreal.c` 需 `rmparser`/RV8 `LMEM_DMA`，`5.15.79` UAPI 缺 `RV30`/`RV40`） | [#27](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/27) |
| H.264 MVC双视图/参考及标准接口（核对 `vh264mvc.c` 需 `CPC_P=0xc00` 与双视图 DPB，FFmpeg 7.0.2 无 `h264_mvc` 包装） | [#28](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/28) |
| MPEG-4旧变体/DivX3.11的不同微码与输入接口（`5.15.79` UAPI 缺 `DIV3`，FFmpeg 7.0.2 无对应 M2M 解码器） | [#29](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/29) |
| H.264 B/CABAC/Main/High编码可执行协议研究（固定 `encoder.c` 未用 `B_Slice` 且 `HAS_VDEC2=0`，无可用路径） | [#20](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/20) |
| HEVC长期参考RPS/POC与微码协议研究（`vh265.c` 仍将 `LongTerm_*`/`HEVC_MPRED_LT_REF` 硬编码为 0） | [#21](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/21) |
| 逐slice提前交付的稳定发布边界及标准语义（`encoder.c` 仅整帧完成 IRQ 并读取 `VLC_TOTAL_BYTES`，无 slice-ready 偏移） | [#22](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/22) |

S805 HEVC编码、Main10及高级H.264编码没有可靠同芯片可用路径时，不增加
虚假枚举。研究结论与尚未移植的已知厂商路径分别处理；细节见
[厂商能力追平](docs/codec-parity.md)和[编码依据](docs/encoding-feasibility.md)。

## 已完成并关闭的批次

| 完成项 | 依据与边界 | Issue |
| --- | --- | --- |
| 单HCODEC多H.264上下文逐帧调度 | owner/参考/canvas隔离及主机错误恢复回归，两路720p/1080p各12幅内容通过；不承诺八路实时 | [#19](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/19)（completed） |
| HEVC stop、编码微码DMA及头计数三项P0错误 | 已合并、真实函数故障回归及原ARM/有界部署验收通过；不关闭历史#4/#5因果关系 | [#23](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/23)（completed） |
| MPEG-2 B帧码流 drain 交付 | 静默期锚定后 `m2v-bf-150f` 5轮 `150/150`（哈希全一致、`min_Y_PSNR 63.92 dB`）、`90/90`、回压及两种 `STREAMOFF`+重开通过 | [#8](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/8)（completed） |
| H.263 范围与固件错误恢复 | 720p/1080p `-threads 1` 全YUV、回压/`STREAMOFF`+重开、固件缺失(`ENOENT`)/损坏(`EINVAL`)/恢复与官方 FFmpeg 7.0.2 `h263_v4l2m2m` 通过；Slice Structured 明确拒绝 | [#16](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/16)（completed） |
| 独立 JPEG 图片解码与缩放旋转 | `V4L2_PIX_FMT_JPEG` 9组（4:2:0/4:2:2/4:4:4/灰度/rot90/180/270/`720p->360p`）+ `SOF2` 拒绝 + `STREAMOFF`/重开 + `JPEG->H264` 60/60 + 1295字微码核对通过 | [#30](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/30)（completed） |
| HEVC tiles 多 slice 与生命周期 | 独立/依赖 slice 与均匀/非均匀/非 CTU 裁剪 tiles（含 B 帧）、`CAPTURE_BUFFERS=4` 回压、双顺序 `STREAMOFF`+重开及 `720p->854x480` 动态切换可见全平面逐帧哈希全一致通过 | [#15](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/15)（completed） |

## 应用与平台边界

配套官方FFmpeg镜像的自动构建/发布由[#12](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/12)
跟踪；自动播放/跳转/取消/错误端到端回归由[#13](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/13)
跟踪。人工播放、小尺寸AAC/HLS或已有镜像交付不替代自动化关闭条件；应用
代码仍在OpenList后端/前端fork，禁止维护私有FFmpeg功能分支。
这两项作为#7的配套交付/验证推进，驱动开发主线仍先做#14/#32；不为铺开
发布自动化而推迟基础数据路径，也不把应用回归算成驱动硬件验收。

研究内核/跨板支持只有构建侧证据，实机与救援条件见
[移植记录](docs/research-validation.md)。历史阶段结论留在[历史目录](docs/history/README.md)，
不作为当前执行清单。原厂同机基准及完整厂商追平仍未完成。
