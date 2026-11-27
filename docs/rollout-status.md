# 生产部署快照 / Deployment snapshot

2026-11-27 UTC最后核对的精确来源。用户重启后验证恢复；后续Tailnet故障期间
本地网页正常，远程现已恢复，链路根因仍未闭环。本文是部署记录；新设备按
[安装指南](installation.md)，历史版本按[验证记录](validation-records.md)复核。

22:22 UTC后的故障状态：根分区因ext4校验错误保持只读，Tailscale SSH命令
失败，现有备用终端可用。临时GE2D已卸载，原模块按SHA256恢复NEON；
OpenList通过原freezer解冻，四项HTTP200恢复。运行时缓存/直接读取差异页
已保全，定向释放两个文件缓存后containerd恢复。11-27 02:01 UTC核对时，
containerd元数据临时使用RAM副本，Docker、原五业务和四项HTTP200已恢复；
更新后的元数据另存SSD和设备外校验备份。根分区仍只读，codec模块为手动
加载，自动加载unit处于inactive，不能把这个临时恢复视为正常部署验收。
下表只记录安装来源；硬件实验继续暂停，无救援入口时不重启设备，见
[故障现场](validation-records.md#11-26-根分区只读与缓存字节差异现场)。

11-27 完成 30 分钟主机侧只读健康轮询（30 轮 HTTP 200、6 轮 SSH 正常）并再次
复核设备状态不变。工作机随后补齐解码器导入 DMA-BUF 栅栏等待并消除无 padding
导入缓冲与稳态 MMAP 暂存面的冗余 CPU 访问/清零，通过 150 项主机测试、
严格 checkpatch、3 项故障注入及 5.15.79 ARM 外部/additive 双构建；之后又在
工作机完成 #14 编码器多平面 DMA-BUF 32 位边界校验与 JPEG 会话 CMA 缩减，
并补齐 #32 同 fd 重开清零旧缩放几何与 GE2D `PREPARE_BUF`/`CREATE_BUFS` ioctl。
并行核对 `make test`（152 项主机测试 + ASan/UBSan + 文档检查）、严格 `checkpatch.pl`
及 5.15.79 ARM 外部/additive 双构建；之后继续在工作机完成 #24 码控测量上限修正
（首帧 IDR 沿用 `qp_max` 且 `qp_min` 高于 32 时与 P 限幅共享 `venc_rc_p_ceiling()`）、
#15 首 slice 禁止 `dependent_slice_segment_flag` 校验与 #16 禁止 `y == 0` 的 `GN=0`
GOB 头校验。各批候选当时均未部署实机，在役模块与参数保持 `2fea36e` 恢复状态，见
[解码同步候选](validation-records.md#11-27-解码器-dma-buf-栅栏与-padding-清理)、
[编码 CMA 与 DMA 边界候选](validation-records.md#11-27-编码器-dma-buf-边界与-jpeg-cma-缩减)、
[缩放重开与创建缓冲候选](validation-records.md#11-27-同-fd-重开清零与-ge2d-create_bufs)
与[码控/HEVC/H.263 边界候选](validation-records.md#11-27-码控上限测量与-hevch263-切片边界)。

11-27 后续已完成上述候选（#14/#15/#16/#24/#32）实机 Stage 1（默认 NEON）与
Stage 2（临时加载 `meson8b-ge2d.ko` + `capture_ge2d=1`）验收；验收期间定位并修复
同 `fd` 停流后未及时清零 `sess->capture` 与 `sess->dpb_size` 的问题，通过
153 项主机测试、严格 `checkpatch.pl` 与 5.15.79 ARM 外部/additive 双构建。
目标机已于此前恢复可写根分区及启用 `meson8b-vcodec.service`，在保留 `2fea36e`
设备外校验备份后，升级部署本轮默认 NEON 运行包并完成部署后复验与业务健康核对
（五容器运行、四项 HTTP 200、`pool_bytes=0`、生产 GE2D 继续关闭），详见
[停流清零与阶段一/二验收](validation-records.md#11-27-停流缩放清零修复与阶段一二实机验收)。

随后完成 #31 GE2D 扩展格式、色彩矩阵、翻转/旋转、缓存 `capture_stage` 与二阶段
`ABGR32/RGBA32/ARGB32` + `BLEND_CONST`/`BLEND_OVER` 实机临时验证，并补齐 #7
1080p 300 帧官方 FFmpeg 7.0.2（`0.857×`）与单引擎基线测量（测试后均已卸载
`meson8b-ge2d` 并恢复在役默认 NEON、原业务容器与四项 HTTP 200 正常，
`pool_bytes=0`；1080p 下 `tailscaled` 持续写爆 syslog 导致高负载并曾记录一次
`eth0` `NETDEV WATCHDOG` 复位，生产 GE2D 与 1080p 持续压测继续保留关闭），见
[GE2D 扩展实机验收](validation-records.md#11-27-ge2d-格式色彩矩阵翻转旋转与缓存暂存验收)
与[1080p 拆解与 alpha 混合验收](validation-records.md#11-27-1080p-阶段拆解ge2d-alpha-混合与网络超时现场)。

| 组件 | 当前记录 |
| --- | --- |
| 内核 / boot DT / CMA | 原版5.15.79-meson；启动文件未替换，CMA64 MiB。 |
| 最新正式版 | v0.4.0，源码`89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f`；原安装及回滚资料保留。 |
| 已部署开发驱动 | 完整运行包（含 #14/#15/#16/#24/#32 与停流缩放清零修复），默认NEON、GE2D关闭；单平面与双平面连续 NV12M/NV21M 直接读取已启用。 |
| OpenList 镜像 | `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`。 |
| 镜像内应用代码 | Backend `6c77764f6e940c218f08576ae67782376467d54c`；frontend `a845b348e7967e4a76f2ef07b9e840709def96cf`。后端后续提交为文档更新。 |
| FFmpeg | OpenList镜像保持官方底座8.1.3、字节未改；新运行包附官方原版7.0.2独立工具，启用HLS输入，不替换宿主或容器FFmpeg。 |

## 已安装模块 SHA256

| 文件 | SHA256 |
| --- | --- |
| meson-vdec.ko | `3b6308f0ce7ecb2fa57a2ebf1f8567dd2d9cfb638d55eb47f47431d79f8fba38` |
| meson-venc.ko | `e6b1aa08d4c071f1b0507f9303dbca7f8d25bd3e6660a36133a02bba530e9595` |
| meson8b-runtime.ko | `e88dd31947056dcc416357ac25cfdbd1eb99a43d53d5c4e65a6be3e1b28c1b02` |
| runtime overlay | `72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e` |

后续按用户“每步完成都部署最新”要求，将单平面NV12增量正式安装为
`6925c3f`，102幅编码复核及H.26450/HEVC10冒烟通过，新state在设备外校验。
再完成GE2D→编码共享DMA-BUF入口`f053ed2`，7组47幅内容/顺序/质量通过，
35幅编码直接读取、12幅非整宏块高度按预期复制。该增量仅改客户端/测试，
包更新前核对所有模块/固件字节不变，未再次重启业务；对应客户端已部署。
现有原五容器及另一个无关容器均保留，四项HTTP200、unit enabled/active、
26个启动文件及containerd状态核对通过，GE2D测试模块已卸载、帧池归零。
具体版本/失败与恢复边界见[共享交接记录](validation-records.md#11-26-ge2d-到编码器共享-dma-buf-交接)。

再完成并保留`926d0c0`实际缩放入口，9组47幅完整软解/顺序/独立bilinear
全平面质量通过，41幅直接读取、6幅非整宏块高度复制；覆盖1080p→720p、
非等比/四倍缩小、裁剪宽、同fd重开与JPEG。模块/固件字节未改，包与客户端
刷新无须再重启业务。临时GE2D已卸载，默认NEON、原五业务/四项HTTP200、
26个启动文件、原state与containerd状态通过。未加入decoder或证明完整链
收益，见[缩放交接记录](validation-records.md#11-26-ge2d-缩放后共享编码输入)。

最新`a6f3fc8`补齐真实decoder→GE2D→共享NV12M→encoder诊断入口，独立解码
缩放24组272幅及完整共享转码18组172幅内容/顺序/质量通过，后者编码输入
copied/copy_ns=0。两组尺寸切换48幅的854→320段仍未通过参考质量/顺序，
相位实验未改善并已撤回。正式内核/固件字节未变，运行包和客户端已保留
部署，GE2D关闭；各批测试恢复原业务、HTTP、启动文件/state和containerd
状态，见[真实链与失败边界](validation-records.md#11-26-真实解码缩放与共享编码交接)。

`2fea36e`随后定位并修复该滤波缺口：补厂商首样本重复计数，decoder272幅
和真实共享转码244幅完整内容/顺序/全平面质量通过，含原失败比例/尺寸
切换。完整包与修复的可选GE2D已部署保留；正式三个模块/固件字节未改，
GE2D继续关闭。OpenList曾超过首次HTTP等待窗口，后续轮询及完整健康核对
通过，containerd启动/重启计数未变，见[首样本修复](validation-records.md#11-26-ge2d-滤波首样本重复修复)。

此前13:19 UTC升级并保留`20220a8`完整包，原`7991ba4`包/state/固件87项
先设备外校验备份。默认NEON16组600/600精确、MJPEG9幅全平面及两种提前
关闭通过；编码22组185幅顺序/全YUV质量通过，含四种DMA-BUF导入。官方
7.0.2直接/CPU缩放转码各60/60，另小尺寸视频50/50加AAC/HLS完整时间线
通过。完整包/固件、unit enabled/active、五容器、四项HTTP200、帧池归零、
GE2D关闭和26个启动文件哈希通过，containerd累计重启数未变。新state已
另存设备外，回退timer已取消；无重启或GE2D验收。完整范围见
[本次部署](validation-records.md#11-26-main-20220a8-部署验收)。下文是此前版本的历史快照，
其中“当前包”按对应日期/源码理解。

[11-25重启后验收](validation-records.md#11-25-重启后格式修正与-ge2d-验收)记录此前修复及各自二进制范围；
最新包的独立复验见下文及[MJPEG修正记录](validation-records.md#11-25-mjpeg-滤波与输入边界)。
H.263四份三图短流（baseline、PLUS、自定义尺寸、UFEP续帧）全部3/3且顺序/
YUV通过；H.264 50/50与1080p 300/300、HEVC small/crop各10/10及1080p
30/30复验一致。S805 SAO scratch修正让均匀/非均匀tiles分别4/4、3/3逐帧
精确一致。16槽DPB 32/32、CRA接入24/24、开放GOP 36/36通过；裁剪切换
使用独立libde265的每图真实尺寸参考与原生客户端为33/33精确一致。

GE2D独立修正已完成GREY、NV12/NV12M复制/裁剪、常量平面缩小与DMA-BUF
有界检查；显式decoder接入原生客户端small/裁剪/1080p累计400幅精确一致，含H.264
300/300；取消逐字节FNV读取后300幅交付约7.27 s。原版FFmpeg低缓冲提前
结束、八缓冲HEVC1080p出现CMA不足。水平缩放分段修正后，两组纹理输出与独立
bilinear的质量比较已完成；完整标准转码链仍未验收。GE2D只做临时加载，正式运行包仍为三个模块、默认NEON，不启用它。

当前HEVC池按SPS上限加一个交付槽，典型1080p内部参考面由24降至18 MiB。
正式NEON14组250/250内容及两种提前关闭顺序通过；两槽DPB1为6/6精确。
候选GE2D的HEVC1080p→720p曾30/30精确，但正式重复验收进程SIGSEGV，
恢复服务先遇工具包校验失败，随后重复哈希又一致、差异原件未取得。已按
原清单恢复工具字节和NEON服务，保留GE2D失败与未知根因，不继承候选通过。
此前包Host与ARM/package CI通过，详见[帧池记录](validation-records.md#11-25-hevc-sps-帧池预算)。

11-26当前包修正显式GE2D帧池的缓存别名，CPU回退读取coherent映射；默认
NEON路径保留。候选GE2D原生16组600/600精确、MJPEG回退9幅内容通过；官方
FFmpeg三条缩放链90幅及重加载后的两次HEVC共60幅逐帧精确，未再遇工具
校验异常，但历史崩溃根因仍不认定闭环。正式安装默认NEON16组600/600
精确、三组MJPEG9幅全平面及两种提前关闭通过；恢复后H.26450/HEVC10精确。
包校验、unit enabled/active、五容器、四项HTTP200、帧池归零及26个启动文件
哈希不变。当前源码130项主机/干净源码包测试与ARM双构建通过，GE2D继续
只作临时验收，不代表完整转码或实时收益。见[coherent帧池记录](validation-records.md#11-26-ge2d-coherent-帧池)。

11-26随后部署编码参考面预算修正：720p两张NV12参考面减少约3.36 MiB
CMA。正式19组148幅编码顺序/全YUV质量及两条NEON转码各60/60通过，完整
63项包、服务/五容器/四项HTTP200、帧池归零及26个启动文件哈希不变。当前
131项主机/干净源码包测试与ARM双构建通过。GE2D完整转码仍因CMA或低缓冲
短输出失败，未达到实时目标，详见[编码预算记录](validation-records.md#11-26-编码参考面预算与完整转码边界)。

最新包把显式GE2D的MMAP采集改为缓存普通内存和共享3 MiB中转面，避免
1080p20个采集缓冲占约60 MiB CMA；DMA-BUF仍直接写入。候选600幅精确，
含73幅真实连续DMA-BUF导入；官方7.0.2及镜像8.1.3的直接转码和CPU缩放
转码各60/60内容通过，7.0.2重加载再测各60/60。正式包默认NEON600/600精确、
MJPEG9幅和两种停流通过，包校验/五容器/四项HTTP200/帧池归零及启动文件
哈希不变。133项主机/干净源码包和ARM双构建通过；实时目标、硬件缩放、
音频/HLS仍未完成，生产GE2D关闭。见[采集内存记录](validation-records.md#11-26-ge2d-采集内存与官方转码)。

编码复验覆盖11种支持的输入布局、1080p、GOP/强制IDR、多slice、同fd重开、
VBR、峰值/CPB及SAR，独立完整软解/YUV通过；BGR24按既有接口拒绝。两路
720p及两路1080p各12幅也通过，不能扩大为任意多路或实时吞吐保证。

自然HEVC EOS握手修正已随当前包部署，串接两段20/20、三段30/30均精确
一致；不代表所有序列组合通过。仍开放：WMV3为3/5，MPEG-4两份30幅流为28/30。1080p60幅解码→编码约2.82 s，完整
软解60/60与YUV质量通过，未达1.3×实时，音频/HLS与GE2D端到端收益未验收。

当前包修正MJPEG的色度stride/容量与PSCALE unity滤波，十组SOF0的归一化
4:2:0/4:2:2/4:4:4共30/30顺序/全平面质量通过，含320×240、854×480和1080p。
正式包五类错误输入拒绝、两种提前关闭顺序通过；SOF1、渐进、灰度和重复
采样因子布局仍不支持。原版FFmpeg复验H.26450/50、HEVC10/10、tiles4/4和
自然EOS20/20精确一致；该driver提交的Host与ARM/package CI通过。

用户重启时`30989c8`自动加载成功，四项HTTP200；随后升级并保留当前包。
最近完整健康检查：unit enabled/active、五容器运行、四项HTTP200、帧池归零、
26个启动文件哈希未变。此前旧格式临时试验后SSH断连，用户确认本地网页正常，
并安排修复远程链路；轮询恢复后已完成上述正式包验收和服务刷新。上次journal不可用、
pstore为空，不据此排除或确认旧内核故障；本次局域网正常的链路故障独立记录。
没有由代理重启、更换内核/boot DT或调整64 MiB CMA。

后续`b8c37a8`/`8d7ad11`格式协商候选已完成主机/ARM和两套原生矩阵，但
官方8.1.3重复测试中containerd异常、OpenList停止，故本次没有替换在役包。
已取消实验timer并恢复默认驱动、containerd及五容器，四项HTTP200、完整包
校验、两个视频节点、帧池归零、GE2D关闭及启动文件哈希不变。故障时二进制
哈希异常随后又一致，根因未知；不把单次恢复当稳定性闭环。
当前仍为表中`7991ba4`，见[格式协商边界](validation-records.md#11-26-解码格式协商边界)。

后续编码DMA-BUF同步修正`48a41e3`/客户端`7673d62`同样只完成工作机
136项回归与ARM构建，未安装或加载实机。生产包没有继承该同步修正的
验收，范围见[同步事务记录](validation-records.md#11-26-编码-dma-buf-同步事务)。

decoder GE2D独立缩放候选`06504f3`/客户端`834b7e3`也只在工作机验证，
没有安装。138项回归与ARM构建不等于真实缩放内容/吞吐通过；本轮未重查
实机健康，生产来源仍沿用表中记录，见[缩放候选](validation-records.md#11-26-decoder-ge2d-缩放候选)。

后续`dcbc890`修正CAPTURE转换失败时丢失LAST/EOS和图片元数据，仅完成139项
主机/独立源码包回归与ARM双构建，未安装或连接实机。新硬件实验继续暂停，
在役健康未重查；该修复不关闭历史少帧或运行时异常，见[结束边界记录](validation-records.md#11-26-capture-转换失败的结束边界)。

`e4d73b1`随后修正显式GE2D把DMA-BUF同步/映射错误掩盖为CPU回退成功的问题，
139项Host/独立源码包与ARM双构建通过，仅为离线候选。未连接或安装实机，
生产状态不继承该验证，见[同步错误传播](validation-records.md#11-26-decoder-capture-同步错误传播)。

独立GE2D M2M的`4e426d6`候选新增有界导入fence等待，139项Host/独立源码包与
ARM双构建通过，未连接或安装实机。生产GE2D仍关闭，未新增真实导出器或
内容/性能验收，见[fence记录](validation-records.md#11-26-ge2d-导入缓冲-fence)。

后续`d972b87`只扩展工作机音视频/HLS验证入口与原版ARM工具配置；142项
Host/源码包及软件HLS检查通过。没有安装工具或修改驱动，生产未继承新的
音频/HLS验收，范围见[工具记录](validation-records.md#11-26-音视频-hls-验证入口)。

12:34 UTC再次只读复核：在役仍为`7991ba4`，包校验、五容器/四项HTTP200、
两个节点、帧池归零、GE2D关闭与26个启动文件哈希通过；containerd启动/
累计重启计数未变。46份旧封存journal经设备外逐文件SHA256归档并在设备
重验后清理副本，根分区普通可用空间约603 MiB。现有被动观测器限时运行
六小时并已取得新样本；硬件实验仍暂停，见[取证复核](validation-records.md#11-26-存储余量与被动取证复核)。

当前与升级前`799d2cc`、`ffddcf0`、`39703c7`、`41773bb`、`f581f98`、`de20e87`、`042573f`、`b33e48e`、`30989c8`、`1cf0e71`完整包/固件state已有设备与外部
构建机的SHA256备份，更早资料保留。新state位于持久系统分区；未演练整包
降级。`remove-autoload.sh`不自动切回旧驱动。原始材料仅本地校验归档，无永久
公开附件；恢复入口见[安装指南](installation.md)和私有[迁移清单](workspace-migration.md)。

此前`ca0d320`单平面NV12 DMA-BUF直接读取仅临时更换encoder完成23组166幅
内容/计数检查，已恢复上表`20220a8`，GE2D关闭。新分支未正式安装，严格
位流等价和完整转码收益未证明，范围见[交接记录](validation-records.md#11-26-单平面-nv12-dma-buf-直接读取)。

更早的安装记录移到[历史部署](history/deployments-through-v0.4.md)，其中的
“当前”“最终”只对应当时版本。
