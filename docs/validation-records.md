# 验证与故障记录摘要

以下是自 2026-11-13 起的历史验证结论，不替代后续构建的实机验收。环境为
OneCloud S805/Meson8b、原 Linux 5.15.79-meson、64 MiB CMA，除非另行说明。
当前模块精确来源以[部署快照](rollout-status.md)为准。

2026-11-23 按[仓库规则](repository-policy.md)将原始日志、逐帧结果、试验脚本
及报告 JSON 独立归档；旧记录的基线是 `bd47ef8`。原件现仅在本地校验归档
和既有 Git 历史中保留，没有新的永久公开附件。下文保留成功、失败、修正
与未验证范围，不需要原始附件才能理解结论。

## 解码与帧池

11-13 H.264 解码完成 1080p Baseline 与 High/B 帧逐帧软件参考比较、重复
会话、尺寸切换和损坏输入检查。缓存别名加字节序转换降低了 CPU 成本；
这些是早期路径的结果，不能自动替代当前帧池验收。

11-14 HEVC Main 8-bit：小流 10/10、1080p/B 帧 30/30、854×480 crop 10/10
完整图像哈希匹配；缺失/截断固件错误后新会话恢复，损坏流由五秒 watchdog
限制等待。中间八帧 stride/EOS 试验不算最终通过。早期独立 FFmpeg 带
可选 drain 补丁；后续原版工具检查见下文。HEVC handler 来源为 unifreq
`e7a67bbcd082c6038daa4ff1ab863cb648fa32b9`，S805 RPM/scratch 协议与 GX 不同。

11-15 输出转换修复 canvas 对齐容量、裁剪与旧 crop 状态越界。对齐 1080p
NEON 转换约 7–8 ms，标量约 15–16 ms；非 8 字节对齐窗口仍约 21–23 ms。
这只是转换段耗时，不是完整转码吞吐。旧“GE2D 没有 endian 选项”的否定
判断已被[厂商来源复核](capability-recheck.md)纠正。

11-19/20 帧池候选 decoder MD5 `15d5c77d25225e8ba7a42519ee5da152`：

- H.264/HEVC CAPTURE 1/2/4/20/32 与软件参考一致；四参考 B 帧流 120/120。
- H.264 130 图、HEVC 50 图尺寸切换在 CAPTURE 1/2/4/8/20/32 通过。
- 1080p 解码/编码并发完成；HEVC 最低 CMA 8 KiB，四参考 H.264 最低
  636 KiB，说明余量非常小，不能保证任意码流并发。
- H.264 额外槽由 5 减到 2，每 1080p 会话节省 9 MiB。VIFIFO 2 MiB
  只通过尺寸切换，收益不足且缺少全矩阵验证，因此未采用，保持 4 MiB。
- 余量 2 曾出现一次零字节 FFmpeg 输出，失败时 stderr 未保存；随后
  300 次定向复验完整通过、30 分钟 439 轮通过。样本不足以区分槽数退化
  与工具/旧路径问题，不能把这次故障等同于历史 299/300。

11-22 `5fedae9` 部署修复 IRQ 完成等待、异步交付/槽生命周期、EOS 出队、
DMA/VUI 和共享资源边界。H.264 回收邮箱代际修复后，CAPTURE 1/4 的
130 图切换签名恢复，部署后 CAPTURE 1 的 H.264 130/HEVC 50 通过。
第二 decoder 句柄能力查询可用，忙时 STREAMON 为 EBUSY，释放后接管正常。

## 旧格式完整性

MPEG/VC-1 结束包必须先按 ESPARSER 的 4 KiB 最小包及搜索码要求填充。
该修复曾让 MPEG-2 小流从 88 恢复到 90 图，但不保证所有尾图释放：
150 图 B 流可为 149/150 或 150/150，完整情况才与软件图像/顺序一致。
11-17 的 20 次 B 流检查有 14 次完整，短流丢的是最后解码的一幅 B 图。

MPEG-4 检查中有 88/90 输出及 B 图局部损坏；追加同一 VOP 可以诱使固件
释放尾图，但所需数量随流而变，可能重放/污染图像，因此未采用。早期按
整个 canvas 推断“底部四个宏块行损坏”的分析已修正；有效图像比较仍确认
内容错误。VC-1 advanced 有尾图/重复显示边界；WMV3 与 AVS 仍是独立集成项。

11-22 common LAST 不再由输入包计数猜测 custom drain 的完成，结束码
写入失败传播到两队列。三次 H.264 300/300、三次 HEVC 30/30 通过；
CAPTURE 1/8 的旧格式仍有 MPEG-2 89/90 或 149/150、MPEG-4 88/90。
重排 MPEG-2 结束标记导致同帧数但图像错序/多图，已撤回；增加 100 ms
quiet delay 也未消除尾图缺失，已撤回。不能重放或伪造图片来通过计数。

后续工作机核查发现旧比较器可错误通过乱序或双空输出，并默认使用64字节
stride，而当前驱动使用32字节。现已修正并添加全可见Y/U/V MAD/PSNR检查；
工具入口和边界见[全平面验图](codec-parity.md#旧格式验图入口)。原批320×240
的两种stride相同，不据该工具缺陷撤销或扩大历史结论；没有新硬件验收。

## H.263 PLUS 工作机验证

11-23 源码 `5f4a60d`，ARM构建目标为原5.15.79-meson配置；外部与additive
入口decoder均为SHA256
`761d26afe9d3398112964a264cf587fcafb0296b654e35d700affdd1eed5acf0`。
新增18幅官方FFmpeg 6.1.1生成的PLUS I/P中间流与固定libamcodec参考逐字节
一致，覆盖128×96、164×100、320×240、704×396/404/576、GOB、自定义时钟
及扩展像素比例。164×100三图改写一幅为UFEP=0后，官方软解三幅哈希不变。

主机真实解析/转换/尺寸协商函数通过C ASan/UBSan及96项Python回归，
包含失败头状态隔离、容量、截断、可选工具拒绝、1916×1080全宏块和GOB
行号边界；完整内核/两个模块入口、补丁零fuzz应用和严格checkpatch通过。
默认Overlay的既有数字phandle警告仍存在。记录标识为`h263-plus-5f4a60d`，
原件完成工作区外本地SHA256归档校验，无永久公开附件。此次未部署/重启，
没有硬件像素、EOS、CMA/吞吐或迅雷出厂系统对照结果，不构成追平验收。

## 显示元数据工作机验证

11-23 driver源`8ca1717`，5.15.79-meson原配置外部/additive的decoder均为
SHA256 `104e695233bad1fe4b2e34c9d3b6db0bd9e03d573e526199b38fdb639de150c1`。
原内核CROPCAP链路在缺selection回调时为ENOTTY，只补selection仍因旧
pixelaspect类型判断为EINVAL；完整修复后两种CAPTURE类型均返回规范y/x。
MPEG-4/VC-1标准/扩展/保留比例、H.263头状态与失败隔离、H.264原表行为，
以及coded crop与COMPOSE原点，通过实际回调和内核函数ASan/UBSan回归。

FFmpeg修复源`d2fa162`仅在已有`--bugfixes`下启用。固定7.0.2原函数将
CROPCAP 11/12直接作为SAR；标准方向应为12/11，独立最小修复后通过。
99项Python回归、C sanitizer检查、完整ARM内核/双模块构建和补丁零fuzz/严格checkpatch，
以及两种FFmpeg静态ARM构建通过；QEMU版本检查不执行硬件。默认upstream
源码逐字节核对官方tarball，保持默认产物为upstream；原版非方形像素查询
仍有上述bug。默认Overlay既有phandle警告及静态工具DNS链接警告未改变。

记录标识`legacy-aspect-8ca1717`，原件和两种工具在工作区外本地SHA256归档
校验，无永久公开附件。本轮未部署/重启；无新硬件图片、EOS、CMA/吞吐
和迅雷原版对照，不继承旧模块的验收，不宣称追平完成。

## 11-24 编码 SAR 工作机验证

driver源`6197af5`，原5.15.79-meson配置外部/additive encoder均为SHA256
`48bfa619e3a392418ed5f1f358cdbed20a85a172b9fdf16f8d484c7cac748aa4`。
标准SAR四控件与cluster接入SPS，菜单Extended=17转为NAL值255，整数经
内核规范化到1–65535；默认关闭，流内变更为EBUSY，相同配置可重复设置。
实际control/default/事务/缓存SPS函数及所有标准IDC、扩展边界、crop/色彩/
帧周期共存通过C ASan/UBSan，101项Python回归及严格checkpatch通过。

build源`0213736`仅启用官方h264_metadata组件，7.0.2源码文件核对官方
校验tarball，默认产物为upstream；ARM静态工具和完整内核/双模块入口通过。
6.1.1主机9路、7.0.2 ARM/QEMU 8路软件Main/B测试流，各12幅完整Y/U/V
图像哈希相同，含854×480非整宏块、1:1/12:11/3:2/17:13和满字段65535/
65535，官方过滤器输出17:13也一致。这些流由软件生成，用于SPS写入保真，
没有执行HCODEC或证明Main/B硬编码。控件关闭不写SAR字段。

65535/1单独测试在两版原工具中被忽略SAR并出现2/12短输出，原件与stderr
保留为失败，不计入上述通过流；不能声明全部16-bit比例组合兼容。内核
Overlay既有phandle警告及静态工具DNS链接警告仍保留。
记录标识`encoder-sar-6197af5`，原件、模块、工具在工作区外本地SHA256归档
校验，无永久公开附件。未部署/重启，无新增硬件图片/EOS/CMA/吞吐或迅雷
原版对照，完整追平目标未完成。

## 11-24 HEVC 参考状态工作机验证

源`981d8a6`，原5.15.79-meson配置外部/additive decoder均为SHA256
`ea60026d935ac80bbae5b4cd2328dd368628c76e5c4e5469864459ec125e1ea4`。
真实函数修复前可复现三项失败：负POC被误判过去/未来、前导/非参考图片
更新pocTid0，以及MPRED长期参考状态未清零；修复后回归通过。14,976组
POC/基准组合与固定官方FFmpeg函数一致，覆盖4/5/8/16位LSB、正/负POC、
回绕、三个temporal层及VCL/IRAP类型。102项Python及C sanitizer回归、
严格checkpatch、完整ARM内核/双模块入口和补丁零fuzz/源一致性检查通过。

固定S805厂商代码把三个LongTerm字段设为0/TODO；当前只清零已支持路径的
长期状态，没有移植未知协议，不宣称长期参考可用或硬件不可能。
记录标识`hevc-reference-state-981d8a6`，原件在工作区外本地SHA256归档校验，
无永久公开附件。默认Overlay既有phandle警告仍在。未部署/重启，未进行
硬件图片/EOS/CMA/性能或迅雷原版对照；主机组合不构成前导图片实机验收。

## 11-24 HEVC 每slice校验工作机验证

源`03f10f1`，5.15.79-meson原配置外部/additive decoder均为SHA256
`b44e069181c8024f1610c26256bcb7f9eaf72a2daa14604efdd8499c7814b715`。
旧RPM路径在非首slice快速返回前不校验后续POC/MPRED使用的字段；实际
函数修复前在损坏POC位数测试中错误返回成功，修复后拒绝九类损坏字段，
包括空几何通知。正常独立/依赖slice保持几何，首slice的格式校验保留。

104项Python/C sanitizer回归、严格checkpatch、完整ARM内核/双模块入口
和补丁零fuzz/源一致性检查通过。实际MV地址函数检查12槽、三种CTU尺寸、
首/尾地址；最大每槽261120字节，固定区域262144字节，没有跨入下一槽。
这只核对已声明预算，未证明DPB选择、MV内容或真实多slice图片。
记录标识`hevc-slice-validation-03f10f1`，原件在工作区外本地SHA256归档校验，
无永久公开附件。默认Overlay既有phandle警告仍在。未部署/重启，没有新
硬件图片/EOS/CMA/吞吐及迅雷原版对照，不构成追平验收。

## 11-24 HEVC DPB预算工作机验证

源`9cde55c`，原5.15.79-meson配置外部/additive decoder均为SHA256
`435bfc6e15345cc933b46e9d5b036c99a92d511ff9076afaf509ee519fae7d08`。
修复前实际RPM函数对零重排/DPB 10仅选择6槽，回归失败；修复后纳入SPS
声明上界。真实SPS解析/输入检查覆盖DPB 10→5预读、不降低上界及超过本路径上限的DPB 11
拒绝；实际IRQ/地址表函数覆盖6→10增长、既有引用保留、不交付LAST/尺寸
切换、重复IRQ无额外分配、较小预算不缩池及ENOMEM中止当前片段。

106项Python/C sanitizer回归、严格checkpatch、完整ARM内核/双模块入口
及补丁零fuzz/源一致性检查通过。会话内最大声明可能提前占CMA，预算不
等于所有码流/并发均可分配；精确参数集/crop选择、低延迟多引用图像、EOS
和实际CMA/吞吐仍需硬件验收。原MV12槽及重排余量没有扩大。
记录标识`hevc-dpb-9cde55c`，原件与模块在工作区外本地SHA256归档校验，
无永久公开附件。默认Overlay既有phandle警告仍在。未部署/重启，无迅雷
原版硬件对照，不构成完整追平。

## 11-24 HEVC 引用完整性工作机验证

源`e0df12f`，原5.15.79-meson配置外部/additive decoder均为SHA256
`7fff6316ddad59d007651a01b1bb5f91494afe1a335e74a80d9e4321f5aa7ff1`。
旧引用列表遇到缺失图片后continue，后续条目会占错硬件索引；实际函数回归
修复前失败，修复后先解析全部所需引用再写表。覆盖正常P/B顺序、L0/L1
缺失、未保留引用、越界槽、自引用/空列表、TMVP索引/图片缺失和I slice
旧col指针重置。当前slice类型同步，错误向调用链传播，阻止后续硬件阶段。

108项Python/C sanitizer回归、严格checkpatch、完整ARM内核/双模块入口
及补丁零fuzz/源一致性检查通过。正常程序化参考表保留原顺序，错误路径
不写部分参考表。主机替身未执行MC像素运算，不宣称参考损坏的图片恢复或
正常多引用HEVC实机内容通过。记录标识`hevc-reference-integrity-e0df12f`，
原件/模块在工作区外本地SHA256归档校验，无永久公开附件。默认Overlay
既有phandle警告仍在。未部署/重启，无新增硬件图片/EOS/CMA/吞吐及迅雷
原版对照，不构成完整追平。

## 11-24 HEVC 随机接入工作机验证

源`703e95c`，原5.15.79-meson配置外部/additive decoder均为SHA256
`897841d06aaff07051712cac3255d3f51096dcd8e76b6fcabc9ce085976dbcc6`。
固定S805厂商CRA/BLA路径在前导RASL处跳过图片；旧移植缺失这一步。
实际IRQ回归修复前错误进入参考/像素阶段，修复后只确认固件继续搜索，
首slice消耗对应时间戳，保留旧重排图片与后续记录。覆盖有符号POC、两类
RASL、RADL保留、连续CRA、BLA、CAPTURE协商/重试及字段错误拒绝。

110项Python/C sanitizer回归、严格checkpatch、干净完整ARM内核/双模块
入口及补丁零fuzz/源一致性检查通过。官方FFmpeg 6.1.1软件参考：320×192
的36图open-GOP流，从CRA开始的27 AU丢弃3幅初始RASL后输出24图，各图
SHA256与完整解码对应后缀一致，后续CRA的3幅RASL保留。该参考不执行
移植驱动的固件或像素硬件。记录标识`hevc-random-access-703e95c`，原件、
源码及模块在工作区外本地SHA256归档校验，无永久公开附件。默认Overlay
既有phandle警告仍在。未部署/重启，硬件跳转、EOS、CMA、吞吐及迅雷
原版对照均未新增验收，不构成完整追平。

## 11-24 HEVC 16槽容量工作机验证

源`3e32ca5`，原5.15.79-meson配置外部/additive decoder均为SHA256
`5bb5b142471a8a3fd47a51414b3382a71695ec43e37b11d6f7a20f0ce1ef3d59`。
固定S805厂商`vh265.c`有16个PIC及`0x40000*16` MV区域；旧移植最多12槽，
SPS只接受DPB≤10。真实320×192 Main流声明DPB=16/reorder=0，旧检查返回
EINVAL，修复后通过；46字节最小SPS与来源/许可已纳入独立C回归。

110项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性检查通过。覆盖16槽全部MV首尾地址、16/32/64
CTU、重排上限、同几何扩容保留引用、分配失败和SPS预读预算。官方ARM
FFmpeg 7.0.2未修改构建与官方6.1.1软件参考均完成32图，逐帧SHA256一致。
软件参考不执行移植的固件或MC像素运算，16引用及高重排硬件内容未验收。

固定workspace增长1 MiB；16槽1080p参考面本身约48 MiB，不保证64 MiB CMA
下的完整解码/编码并发。记录标识`hevc-capacity-3e32ca5`，原件、源码和模块
在工作区外本地SHA256归档校验，无永久公开附件。默认Overlay既有phandle
警告仍在。未部署/重启，无新增硬件EOS、CMA、吞吐或迅雷原版对照，
不构成完整追平。

## 11-24 HEVC 几何与引用生命周期工作机验证

源`7d574fc`，原5.15.79-meson配置外部/additive decoder均为SHA256
`6b7bcebf78a342d26c98bc90827e9bd3c1f7fa7e2338705edb5d05e88e7e2ed9`。
编码320×192→384×192而裁剪后均为320×192时，旧判断漏掉参考池切换，
可能沿用较小缓冲。当前编码尺寸/CTU布局与显示尺寸独立判断；编码变化
交付旧图后释放引用，显示变化保留引用直到RPS更新。等容量不同几何不再
只遗忘描述符而留下槽占用；flush清当前/col指针，旧尾图只计数一次。

实际函数回归修复前失败，修复后覆盖编码/显示变化、预算增长、引用释放、
空flush及指针清理。112项Python/C sanitizer回归、严格checkpatch、原配置
完整ARM内核/双模块入口及补丁零fuzz/源一致性检查通过。主机替身未执行
固件或像素运算，不证明实机尺寸/窗口切换、尾帧或CMA恢复。
记录标识`hevc-geometry-lifetime-7d574fc`，原件、源码和模块在工作区外本地
SHA256归档校验，无永久公开附件。默认Overlay既有phandle警告仍在。
未部署/重启，无新增吞吐或迅雷原版对照。相同编码尺寸的SPS/AU关联仍未
完成，不构成完整追平。

## 11-24 HEVC 参数集与AU关联工作机验证

源`b9ebf68`，原5.15.79-meson配置外部/additive decoder均为SHA256
`353e34e2c3ab0f48f249260e4a6c7b9e6f60ef6cab31d1bd50940aa182a197cc`。
旧按编码尺寸选择SPS，预读后一个窗口会把已排队的320×192图片误裁为
316×192；实际函数回归修复前失败。当前按PPS→SPS ID解析窗口，并为每个
AU保存VIFIFO位置和不可变快照，首slice消费、后续slice/CAPTURE重试保留。
取消/EOS清未消费记录；准备后的时间戳/填充失败在DMA前中止会话。

114项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性检查通过。覆盖相同ID替换、不同PPS/SPS映射、
缺失/截断/越界参数、后续slice、快照取消/分配失败、输入位置32-bit回绕
和提交错误。官方6.1.1软件参考的合法新IRAP/SPS/PPS切换流完成33图：
编码均320×192，首图显示320×192，其余316×192；使用标准`flags +unaligned`
取得精确左裁剪，软件无解码错误。实际输入前缀函数对33 AU逐一匹配窗口。
软件参考/主机替身均未执行移植固件或像素硬件，不证明实机参数切换内容。

记录标识`hevc-parameter-association-b9ebf68`，原件、源码和模块在工作区外
本地SHA256归档校验，无永久公开附件。会话SPS最大DPB仍保守预留CMA；
默认Overlay既有phandle警告仍在。未部署/重启，无新增硬件窗口、取消、
EOS、CMA、吞吐或迅雷原版对照，不构成完整追平。

## 11-24 HEVC 当前图片DPB预算工作机验证

源`9a76f63`，原5.15.79-meson配置外部/additive decoder均为SHA256
`543d7cb23bff1616d4de2d0a93a0051ee01e46a404dda71453f8c85d5794f2cb`。
旧会话SPS最大值让未来DPB=16的预读影响当前DPB=5图片，实际RPM回归
修复前预算为16，修复后为6（保留原重排余量）；真正激活DPB=16才增长。
同几何已分配槽保持，不缩减旧引用。较小预算的CAPTURE重协商过去漏写
保留槽地址，实际地址表回归复现并修复，当前重写全部实际保留槽。

114项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性检查通过。官方6.1.1软件参考完成38图：先6幅
1080p/DPB=3，再32幅320×192/DPB=16，无解码错误；软件及主机替身未执行
固件或硬件CMA分配。记录标识`hevc-active-dpb-9a76f63`，原件、源码和模块
工作区外本地SHA256归档校验，无永久公开附件。默认Overlay既有phandle
警告仍在。未部署/重启，无新增实机内容/EOS/CMA/吞吐或迅雷原版对照。

## 11-24 HEVC dependent slice MV指针工作机验证

源`1caca5e`，原5.15.79-meson配置外部/additive decoder均为SHA256
`584e357abf15bd0752af24eb8be440bbb836ea2b38ac7b0d61760edf16b8f2fe`。
固定S805厂商`config_mpred_hw()`只在首图或新独立slice更新MV读指针；旧
移植每个dependent segment重写指针，实际寄存器函数回归修复前失败。
当前dependent segment保留已推进指针，首slice/独立slice原路径保持。
全部16槽及16/32/64 CTU、尾CTU地址和指针保持检查通过。

114项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性检查通过。
[FFmpeg FATE DSLICE A/B/C](https://fate-suite.ffmpeg.org/hevc-conformance/)
样片由官方6.1.1软件解码，各50/50图，逐帧CRC与固定FFmpeg 7.0.2 FATE
参考完全一致；驱动实际前缀解析器接受全部150 AU，1920×1080、DPB=5、
reorder=3。A流trace确认dependent flag；A/B/C来源及SHA256随原件保存。
样片仅作为外部验证输入留在归档，没有假定其可再分发许可或提交为fixture。

记录标识`hevc-dependent-mv-1caca5e`，原件、源码和模块在工作区外本地
SHA256归档校验，无永久公开附件。主机替身没有执行固件、MV数据或像素
硬件；未部署/重启，无新增实机slice/EOS/CMA/吞吐或迅雷原版对照。
默认Overlay既有phandle警告仍在，不构成完整追平。

## 11-24 HEVC 图片输出语义工作机验证

源`043940e`，原5.15.79-meson配置外部/additive decoder均为SHA256
`75f73338714674972a7c2ec9b01c5fbd0a1320ab86a469b1ea33039f909dfca6`。
旧代码无条件show=1，隐藏参考图错误输出；实际图片分配回归修复前失败。
当前PPS/首slice输出标志随AU快照传播，隐藏图片继续解码/引用而不入待输出
计数，时间戳消费一次。RPS释放回归保证其最终回收。IRAP先完成前图正常
重排，再丢弃旧待输出或排空引用，POC=0不再强制绕过重排；50图控制流
回放正确丢3幅/出47幅。尺寸切换无旧尾图时空LAST只重协商、不标EOS，
现有H.264事件时序及故障注入仍通过。

117项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性通过。主机解析测试覆盖0–7额外头位、PPS=63、
输出标志缺省/存在、IRAP及截断。
[FATE OPFLAG A/B/C、NoOutPrior A/B](https://fate-suite.ffmpeg.org/hevc-conformance/)
由官方6.1.1软件解码，分别500/98/96/40/47图，各图CRC与固定FFmpeg 7.0.2
参考一致；驱动前缀函数对全部800 AU的隐藏/旧图标志与官方trace一致。
仅B/C两流的pic_output=0分别2/4幅，NoOutPrior B有一次旧图丢弃标志。
这些软件/前缀检查不证明驱动固件图片数或像素正确；NoOutPrior A含码流内
EOS/CRA，当前仍无该继续路径的固件验收。第三方样片保留外部归档，未作为
可再分发fixture提交。

记录标识`hevc-output-flags-043940e`，原件、源码和模块工作区外本地SHA256
归档校验，无永久公开附件。默认Overlay既有phandle警告仍在。未部署/重启，
无新增实机输出标志/取消/EOS/CMA/吞吐或迅雷原版对照，不构成完整追平。

## 11-24 HEVC 码流内EOS序列继续工作机验证

源`6f07fb4`，原5.15.79-meson配置外部/additive decoder均为SHA256
`d862a8b40ac69e98d4d0280ae3c3d19af142c40726f96b0916bbb5dea7a6db77`。
旧固件EOS处理不区分码流序列边界与STOP终止符，提前LAST并清空后续AU。
实际IRQ回归修复前失败。当前预读计数自然EOS，驱动drain标记在parser
提交前单独发布；即使STOP已请求，旧自然EOS仍先消耗而不结束CAPTURE。
自然边界保持后续快照和重排尾图，确认前图完成，清当前/col指针及slice
位置，下一CRA重新随机接入并推断不输出旧尾图。最终drain才排空与LAST，
未知/重复终止通知中止而不重复LAST。

STOP在会话锁下等待已取走的最后OUTPUT提交，再追加终止符，后续数据等
START显式唤醒。线程屏障回归移除该锁即复现终止符先于最后输入；保留
parser失败、取消、INPUT错误及既有H.264回归。119项Python/C sanitizer、
严格checkpatch、原配置完整ARM内核/双模块入口和补丁零fuzz/源一致性通过。
标准NoOutPrior A在第26个packet含EOS/VPS/SPS/PPS/CRA；50图控制流回放
丢3幅旧尾图、跳7幅RASL、输出40。官方ARM FFmpeg 7.0.2与主机6.1.1
软件各40图SHA256相同；此前FATE CRC一致。五流800 AU前缀回归确认仅该流
含一次自然EOS，输出标志保持一致。第三方输入仅外部验证，不作为fixture。

记录标识`hevc-inband-eos-6f07fb4`，原件、源码及模块工作区外本地SHA256
归档校验，无永久公开附件。主机回放不执行固件或像素硬件，ACTION_DONE后
真实搜索继续、序列切换图片/EOS/START/CMA/吞吐均未实机。默认Overlay
既有phandle警告仍在。未部署/重启，无迅雷原版对照，不构成完整追平。

## 11-24 压缩OUTPUT偏移与扩展输入工作机验证

源`fad2e18`，原5.15.79-meson配置外部/additive decoder均为SHA256
`cb40f0384f1ee454eb72630823e5fcee9684d985812c828c78e1d28dab043883`。
Linux 5.15.79 UAPI明确bytesused包含data_offset，旧输入路径忽略offset，
实际填充函数回归复现前缀/有效载荷被改写。当前检查、封装转换、填充与
DMA使用同一有效区间；H.263/WMV3转换保留原前缀并更新包含offset的
bytesused，HEVC快照从有效NAL开始。非法区间在buf_prepare拒绝。

122项Python/C sanitizer回归、原配置完整ARM内核/双模块入口与补丁零fuzz/
源一致性通过。实际函数覆盖13/17/19/23/32/37字节偏移、转换容量不足、
前缀不变和DMA地址/长度；原零偏移回归保持。共享VP9封装修复只覆盖既有
非S805路径，不开放S805 VP9。checkpatch仅有新文件的MAINTAINERS提醒，
本独立仓库无该文件；忽略FILE_PATH_CHANGES后的代码风格检查零告警。

另核对12份固定FFmpeg 7.0.2 FATE 8-bit样片：10份尺寸范围内的实际前缀
解析通过，软件共947图CRC与参考一致；其中NUT_A有缺失参考诊断并保留，
其余9流913图无软件错误。两份PICSIZE流为1056×8440/8440×1056，按当前
尺寸限制拒绝；拒绝不代表格式完整性缺陷。该检查不执行硬件解码。

记录标识`decoder-input-offset-fad2e18`及`hevc-conformance-input-fad2e18`，
原件、源码/模块工作区外SHA256归档校验，无永久公开附件。第三方样片未
作为fixture提交。默认Overlay既有phandle警告仍在。未部署/重启，任意
offset真实DMA、图片/EOS/CMA/吞吐及迅雷原版对照均无新增实机验收。

## 11-24 解码输入DMA同步工作机验证

源`ad05151`，原5.15.79-meson配置外部/additive decoder均为SHA256
`d6bd936713c26cd85b4250fe36b0f237726628503b0ad6c36d33d27b907e9e09`。
核对原Linux 5.15.79 vb2 dma-contig：导入缓冲同步在排队前，后台输入函数
随后仍读写封装/填充；旧代码缺少DMA-BUF CPU访问配对及最终发布屏障。
实际队列回归在旧路径的DMA启动处失败。当前begin/end包围全部后台CPU
准备，end成功才继续，dma_wmb在修改完成后、parser kick前发布数据。

122项Python/C sanitizer回归、严格checkpatch、原配置完整ARM内核/双模块
入口及补丁零fuzz/源一致性通过。覆盖MMAP/非零offset、导入成功、begin/
end错误、封装检查/填充失败、后续时间戳分配失败、无inspect的填充路径与
原错误优先保留。独立移除begin、end、屏障的三种故障注入全部被断言检出；
所有失败不启动DMA且成功begin只配一次end。

记录标识`decoder-input-dma-ad05151`，原件、源码和模块工作区外本地SHA256
归档校验，无永久公开附件。主机替身仅验证调用/状态顺序，不执行exporter
缓存或真实DMA fence。默认Overlay既有phandle警告仍在。未部署/重启，
导入缓冲实机图片/EOS/CMA/吞吐及迅雷原版对照仍未验收，不构成完整追平。

## 编码、质量与码率

11-13/15 编码源 `bd1cb79` 到 `53cd705`/`2811493`：固定 QP、I/P、GOP、
force-key、控件事务及溢出恢复通过。修复 QPPICTURE 常量 26 的码流错误、
非默认 QP 范围锁死与 1 MiB 溢出快速报错。运动内容受 DRAM 时序影响可能
码流不逐字节相同，需用完整软解、码率和 PSNR 检查；不能要求 motion 二进制一致。

YUV420/422/444、RGB、BT.601/709、非整宏块宽度、连续 NV12/NV21 DMA-BUF
及多分片验证过；多分片是整帧后交付，不证明逐 slice 提前输出。
历史 JPEG 900 张 1080p 及质量/尺寸矩阵通过独立软解；JPEG full-range
行为与 AVC 不同。MJPEG 解码采用厂商色度处理，不承诺与软件逐字节相同。

11-21 码率源 `9843894791732b8fc07f9ee0b40398cd323d82d1` 的最终候选
SHA256 `366d0653cb3f34a4d7f3c5d876463a28fab857e012ba9e8a991e5ac96bd916fe`：
640×360 GOP1/2.5M 稳态由 2.900 降至 2.441 Mbps，分数帧率/1.6M 得
1.588 Mbps；同 fd 重启 100/100，运行中 force-key 未丢。1080p 合成 motion
的 1/2/4/8M 目标仍约 10.35 Mbps、QP32 饱和，不是所有影片的固定下限。
预排四幅 noise 仅发生三次溢出，终止队列后不继续启动第四次，随后恢复。

11-21 峰值/CPB 源 `00749671b7f0f1887f323cfadb278ea6f29aad04`、encoder
SHA256 `e67a83367410c859f44151c774b2290124ca1f5eb3751c1161350556a92d84e8`：
150/150 图的 11325 个连续 AU 区间符合预算；raw NV12 两次 90/90，低预算
0.358 s 内非零失败；改目标/容量并重启 180/180，JPEG 不受 AVC 预算影响。
30 分钟 1808.014 s、26 轮、78000 图完整软解，117039000 个区间符合预算，
55 次服务采样通过。该工具使用当时可选 FFmpeg 控件补丁；当前官方路径
不转发 peak/CPB，不能把历史工具结果当成生产 OpenList 原命令的预算保证。

## 官方 FFmpeg 与性能

11-22 官方未修改 7.0.2 编码候选 SHA256
`e72e3a6697b5f7296520f8b339b7c7e6af9147b299d0639e92c5e42128f4747b`：
1813.749 s、99 轮、11880 图全量软解/计数、99 次服务检查通过。640×360
九项 static/motion/scene 质量矩阵平均 **Y PSNR** 34.089–55.166 dB，
不是全图 PSNR，低目标可能饱和、简单内容可能少于目标，不证明严格 CBR。

默认多线程 rawvideo/framemd5 曾在原模块上也截成 145/146 图，并报告全部
CAPTURE 被用户态持有；标准 `-filter_threads 1` 和输出 `-threads:v 1`
避免该独立工具边界。原模块 3×300、候选 15×300 图哈希通过；不能将此
与稀有 299/300 故障混为一谈。动态链接到 GLIBC_2.34 的诊断工具启动失败
也不算驱动失败，目标工具应在工作机静态构建。

11-22 生产官方 FFmpeg 8.1.3 镜像和前端 `a845b34` 浏览器有界播放、前后
跳转、取消通过，取消释放约 0.242 s、pool 归零；仍观察到缓冲。300 图
内容核验的性能对照波动很大，高优先级 worker 实验无可靠收益已撤回。
同质量、完整 OpenList 命令 1080p **1.3× 实时目标尚未达到**。

## 历史少帧与持续负载

v0.2.0 源 `c20a4732c4017a1ff9b30262b36f99d5a6491943`：30 分钟 545 轮和
两小时 2205 轮无失败；过夜窗口 7620 轮、24855 s
在末轮发生一次 H.264 300→299，必须记为失败。没有留下 IRQ→交付→出队
时序，也未确定少的是哪幅。随后空闲 200 次和混载 400 轮未复现。
v0.3.0/v0.4.0 后续短负载通过不能消除这项历史缺口。

11-23 工作机提取真实函数的 72 组 300 图片时序通过，四种故障注入被检出。
这是合成图片/调度，未执行固件或证明像素正确。历史输入为 Baseline、
0 B 帧；PTS 排序修复早于故障，不能作为该故障根因。详见
[H.264 回归](h264-regression.md)，[#5](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/5)开放。

## 失联与故障保存

11-15 曾两次硬复位，随后 USB SSD 一次未枚举；日志时钟不可用，只能确认
顺序，不能宣称已知供电或 USB 根因。与 11-22 两轮失联分开分析。

11-22 第一轮旧 255 MHz VDEC 选频高置信度导致 Ethernet 共用 MPLL2 被
重调：源码 CCF 取整优先选 MPLL2 255 MHz，可把原约 125 MHz RGMII TX
降到 63.75 MHz；本机仍在运行、LAN/Tailnet 同时不可达与此吻合。
没有故障瞬间寄存器快照。固定父钟/精确 divider 保护后 MPLL2 约 500 MHz、
Ethernet TX 约 125 MHz。只读查询未重调时钟，不冒充破坏性实机复现。

第二轮使用原厂约 182 MHz 固定父钟路径，混载中失联并本地重启：最后
journal 尾部约 347.75 s 未持久化，ext4 `commit=600`；ramoops 没有有效
reserved-memory，不能由缺少 panic 日志排除锁死。内核/DMA、网口、供电
与存储阻塞仍无法区分。[#4](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/4)开放。
ShellCrash 覆盖 Tailscale socket mark 是独立配置冲突，LAN 已 RETURN，
不能单独解释 LAN 故障；Ethernet watchdog 重协商也未确定为根因。

11-23 SSD 普通可用空间为零，Arcane restart 明确报 ENOSPC。校验后清理
1646747597 字节重复归档约 1.53 GiB，恢复约 426 MiB 普通空间并启动 agent。
81 次网关和四项 HTTP/codec 检查通过。这是该时点的容量故障与恢复，
不能倒推两轮失联原因。限时被动观察在系统分区保存轮转记录，不是完整
六小时验收，也不提供堆栈/寄存器证据。独立 LAN 观察与救援仍待准备。

## 11-23 P0 部署

已合并 [#25](https://github.com/YoungSx/onecloud-meson8b-vcodec/pull/25)，部署
driver `3c7d7854bcab3b93a716b7da84002015913082f0`；沿用 runtime、Overlay、
固件，启动 kernel/DT/CMA 和生产容器配置未改，保留上一 `5fedae9` 和
v0.4.0 安装及回退材料。

三项修复为 HEVC stop 丢弃待交图、HCODEC 微码 DMA 屏障/超时取消，以及
SPS/PPS 零/倒退/越界长度拒绝。实际函数修复前失败/修复后通过，原批次
60 项 Python、C sanitizer、ShellCheck、原配置 5.15.79 完整内核/外部与
additive 模块构建通过，两入口 codec 二进制一致。GCC plugin 不匹配的
旧 headers 尝试未用于最终模块，已重建匹配 GCC11 内核。

有界硬件结果：H.264 50/50；HEVC 小片/crop/1080p 10/10、10/10、30/30
图像哈希一致；CAPTURE 1/4 与两种关闭顺序四项 reopen 完整 10 图；
H.264 12 图含同 fd restart、JPEG 3 图；缺失/截断 AVC 固件报错后各恢复
3 图。所有参考、工具和编码输出独立软解在工作机准备/完成。12 图 H.264
全图平均 PSNR 43.318 dB、最低 42.486 dB，只适用于该 320×240 测试源。

现有官方 FFmpeg 8.1.3 容器硬解→硬编 50 图通过。模块哈希见部署快照；
pool 归零、四项 loopback HTTP 200、SSH 可用，共享时钟未变，无新增保留
窗口内 BUG/Oops。loopback 检查不独立证明 LAN 在线。较大传输期间曾有
两次新 SSH 超时，发生在模块切换前，不归为候选驱动回归。

新冷启动、异常重启和长稳定性未验收；没有建立三项修复与 #4/#5 的因果
关系。生产持续负载继续暂停。[#23](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/23)
记录该批修复完成项，恢复验收由 #6 跟踪。CI 绿色只绑定对应提交，后续
源码或本地测试改动不自动继承。

## 11-24 正式安装验收

UTC 2026-11-24，OneCloud S805、Linux 5.15.79-meson、64 MiB CMA。
按用户要求将完整包`1cf0e719a149e93bafa25352f980cc24445847eb`保留安装；
三个模块及Overlay的精确SHA256见[部署快照](rollout-status.md)。默认GE2D
未启用。此前`9e8f50b`候选暴露H.263首次选择格式后沿用旧最小尺寸，以及
HEVC终止drain后重启NAL搜索、再次通知中止采集队列的问题；修正真实函数
回归先失败后通过，122项Python/C sanitizer检查、严格checkpatch、原ARM
内核/外部与additive构建、干净匹配baseline补丁零fuzz与源码一致性通过。

固定测试输入为仓库H.264烟雾/1080p及HEVC small/crop/1080p样片，设备端
复用原件前校验SHA256，提取ES也与工作机逐字节一致。未修改的官方FFmpeg
7.0.2使用`-filter_threads 1`及输出`-threads:v 1`、passthrough时间线：
H.264为50/50、300/300，HEVC为10/10、10/10、30/30，完整NV12可见帧
MD5均与独立软解一致。首轮漏限filter线程的失败输出保留，不算驱动通过；
生产镜像官方8.1.3复验H.264 50/50、HEVC 10/10。HEVC CAPTURE 1两种
STREAMOFF顺序后重开10/10一致，第二decoder忙时EBUSY及释放接管通过。

直接V4L2编码客户端生成320×240运动图H.264 30帧、SAR 17:13的12帧、
质量85的JPEG 3张，以及两路不同图案并发H.264各30帧。工作机官方6.1.1
逐一完整软解，帧数/尺寸、SAR与Y/U/V检查通过：H.264三平面PSNR分别
至少38.88/46.50/41.93 dB，JPEG为52.04/54.99/55.12 dB。没有据此
宣称1080p多路、Main/B帧编码或总吞吐已达标。

新增能力明确失败，不能由退出码0判定完整支持：

- HEVC均匀4×2、1920×1080为4/4，非均匀3×2、640×384为3/3，
  但逐帧像素不一致；不计tiles内容验收通过。
- H.263 baseline 128×96、PLUS 164×100、704×576及UFEP续帧四份三图
  短流均为2/3；WMV3 Annex L/RCV 320×240为3/5。已输出帧按顺序与
  全可见Y/U/V比较，MAD低于0.04，但缺失尾图仍是失败。WMV3软件参考
  必须使用passthrough，默认按1/1000时间基重采样的短参考已纠正。
- CAPTURE 1拼接尺寸切换当时摘要显示H.264 392/400、HEVC 40/60，并有H.264
  drain超时。11-25复核原始哈希文件实际已保存400/60：客户端只在主循环计数，
  漏算切换drain中的图片。该摘要不构成少帧证据；后续流式全可见像素比较
  H.264 400/400、HEVC 60/60一致，另补H.264缺失的序列LAST，见下文。

标准安装迁移先校验旧整包/固件state，完整旧部署已在设备和外部构建机核对
压缩包与解压tar的SHA256；本轮没有演练整包降级。新state位于持久系统分区。
service stop/start后H.264 50/50、HEVC 10/10再次一致，最终unit为
`enabled/active`，包清单及安装固件哈希正确，decoder帧池slots/bytes归零。
OpenList恢复，原五个容器运行、四项HTTP均200；26个启动文件哈希不变，
新包测试后dmesg未见BUG/Oops/WARNING。SSD接近满盘，输出和state未放SSD；
未清理业务数据、未改内核/boot DT/CMA、未重启。

记录标识`wanos-deploy-1cf0e71`；旧安装、正式运行包、原始输出、逐帧比较及
日志保存在工作区外本地SHA256校验归档，无永久公开附件。冷启动、异常重启、
长期稳定性、原厂迅雷系统同机对照及上述失败路径仍未完成，不宣称已追平原版。


## 11-25 切换修正与新增路径诊断

原Linux 5.15.79-meson/64 MiB CMA，正式运行包
`30989c8a1d943e62205d5a815b9de638165f8046`已保留部署，模块SHA256见
[部署快照](rollout-status.md)。客户端修复漏计drain图片、同时可读写时饿死
CAPTURE、输入完成计数及末尾STOP时序；拒绝截断超容量AU，并实际区分
STREAMOFF_CAPTURE_FIRST=0/1。H.264固件参数变化时补旧CAPTURE序列LAST，
真实源函数回归修复前失败、修复后通过。124项Python及C sanitizer、ARM原
内核/外部和additive入口、补丁零fuzz/源码一致性通过。

新包直接客户端CAPTURE 1下，H.264 320×240(50)→1920×1080(300)→320×240(50)
为400/400；HEVC small(10)→1080p(30)→854×480 crop(10)→small(10)为60/60。
用FIFO逐帧剥离32字节stride及64 KiB容量padding，全部可见NV12样本MD5与
工作机官方FFmpeg独立软解一致，H.264两个5秒drain等待均消失。旧包原始哈希
文件已有400/60，先前392/40是工具漏计，不能归因为驱动丢图。

HEVC均匀4×2、1920×1080的4幅与非均匀3×2、640×384的3幅完整输出，
仍有少量像素错误：前者每图9–12个Y/U/V样本、后者6–19个，集中在tile边界
附近且可传播到参考图片。固定Kvazaar 2.3.1生成512×256、2×2 tiles、各2幅
全I流，关闭/单开SAO与去块滤波共四种配置仍与软解不一致；不把低MAD当作
逐字节正确。RPM实测均匀宽7/8/7、非均匀宽2/5与标准PPS一致，不回退为
厂商旧均分算法，也不覆盖错误像素伪造通过。

H.263新候选`0b24b63`在原结束码之后追加不含像素的VOP起始边界，释放固件
最后一幅，不重复输入图片。临时诊断模块中baseline 128×96、PLUS 164×100、
704×576各3/3、顺序与全可见Y/U/V比较通过，最大MAD约0.053，最低PSNR
约60.60 dB。正式源码回归先失败后通过、ARM外部/additive构建与124项主机
检查通过；尚未正式安装，UFEP及完整尺寸/错误恢复矩阵未补。WMV3空帧前缀
诊断仍3/5，未纳入正式实现；MPEG-2/MPEG-4/VC-1其余内容问题仍开放。

GE2D第一次试验只到运行时Overlay加载即报EINVAL，GE2D模块/自检尚未运行。
核对原5.15.79 overlay.c：导出`__symbols__`要求目标运行树已有同名节点。
构建去掉`-@`后保留`__local_fixups__`内部引用，新增主机回归修复前失败、
修复后通过；默认Overlay仍为原SHA256，125项主机检查通过。修正未实机，
不宣称GE2D像素/缩放/吞吐可用。安装启动内核、boot DT及CMA均未改。

GE2D加载失败后清理脚本成功启动正式codec service及OpenList；随后远程
SSH/Tailnet超时，Tailscale将设备标离线，尚无本地LAN或串口证据。不能从
这一先后顺序推断内核崩溃、GE2D寄存器故障或复用历史失联根因。未执行
重启/断电或持续压力，当前在线与业务状态无法刷新。设备恢复后才继续
补验DPB16/前导图片/自然EOS、编码输入/控制矩阵、多会话与CMA、性能/GE2D；
冷启动、异常重启和迅雷原厂对照仍缺恢复/对照条件。

记录标识`parity-completion-30989c8`；已取得的运行包、tiles原始输出、诊断
源码/日志和逐帧结果在工作区外本地SHA256归档，无永久公开附件。H.263新增
原始输出及本轮最终设备日志尚未从离线设备导出，现保留已取得的全平面检查
日志；不把未导出的设备文件称作外部备份。正式安装与临时诊断
版本分开记录，不将候选内容通过写成生产能力已通过。

## 11-25 重启后格式修正与 GE2D 验收

用户重启后，原Linux 5.15.79-meson、64 MiB CMA，`30989c8`自动加载成功，
五容器运行、四项HTTP200；26个启动文件哈希不变。上次boot的journal未保留、
pstore为空，无法据此定位此前失联。随后标准安装升级至`b33e48e`，再保留
`042573f13deb06db7e5324143029b1bc7ad5c261`，最后部署至
`de20e8704556da7af8ede2d52aed2b43022ab719`；当前精确模块哈希见[部署快照](rollout-status.md)。
升级前整包、unit、固件及安装state均在设备和外部构建机SHA256核对备份。

H.263 CAPTURE 1：baseline 128×96、PLUS 164×100、704×576与UFEP续帧
四份各3/3，顺序与全部可见Y/U/V比较通过；不重复输入图片。重启后H.264
50/50、1080p 300/300，HEVC small/crop各10/10、1080p30/30精确哈希复验
通过。HEVC16槽DPB流320×192为32/32，从CRA开始为24/24，开放GOP为36/36，
全部与软件逐帧一致。这些不构成满预算1080p并发或所有前导图片类型验收。

S805厂商scratch协议为`((CTU/8)*2+4)*16`，16/32/64 CTU分别128/192/320字节；
旧移植使用另一组9/14/24单位，破坏tiles边界像素。修正同时用于编程和容量
校验，真实SAO函数/三种CTU主机回归及ARM构建通过。原均匀4×2、1920×1080
四图和非均匀3×2、640×384三图现在完整逐帧MD5一致。独立libde265 1.0.15
对原样片的软解也与官方FFmpeg相同，不能用“软件参考不同”解释旧像素缺陷。

裁剪切换流先前通过原版FFmpeg得到32/33、固定316宽输出，软件framemd5
又因固定输出画布为320宽，二者不能直接比较。独立libde265给出每图真实
尺寸：首图320×192、其后32图316×192；原生V4L2客户端按该几何从FIFO逐帧
剥离padding，33/33可见NV12 MD5完全一致。记录原版FFmpeg的失败，不能
据固定画布短输出宣称驱动丢了该图片。

自然EOS拼接两份10图HEVC最初失败：原生客户端观察同一shift偏移重复EOS
通知；仅忽略重复或清scratch的候选只得首10图并暂停，未采用。后续核对同芯片
SEARCH_DONE握手，使用消费NAL的DISCARD_NAL（0xf0）后，临时模块20/20且
全部可见像素一致；真实源回归修复前失败、修复后通过，正式源码de20e87已
完成125项主机检查与ARM双入口构建，已正式部署；官方原版7.0.2串接两段
20/20、三段30/30全部精确一致，核心H.264/HEVC与tiles复验通过。WMV3仍3/5。新生成320×240、30图的MPEG-2 B流
30/30顺序/全平面通过；MPEG-4 I/P与B流各28/30，已输出帧内容通过但尾图
缺失。空前缀及非coded VOP诊断未证明有效，不将重复/补造图像作为解决方法。

GE2D Overlay去掉导出符号后加载通过。初次硬件自检定位到big-endian NV12
源的UV排列；源色度映射按字节序选择，避免U/V反转。单纯复制/裁剪走两次
GREY平面命令，绕过色度插值；真正缩小仍走硬件色度格式器。自检改用不同
Y/UV字节图案，主机回归覆盖两平面调用、地址重叠、色度阶段超时后保持复位。
严格checkpatch、125项Python/C sanitizer及原ARM外部/additive构建通过。

独立GE2D实机检查各3幅：GREY 512×32、1920×1088，NV12M 1080p原样复制、
裁剪至1280×720全部可见像素及时间戳通过；连续NV12缩小、NV12M DMA-BUF
缩小的不同Y/U/V常量通过。后者是常量平面，不能扩大为纹理插值质量通过。
显式`capture_ge2d=1`原生decoder累计110幅：H.264 small50/50、1080p短片
10/10、HEVC small/crop各10/10、1080p30/30，全部精确MD5，GE2D计数110，
结束帧池归零。300幅诊断带逐字节FNV读取时90秒超时、145幅，未计通过；
原版FFmpeg低CAPTURE缓冲也有短输出/用户态断言失败，不能宣称完整标准工具
链已验收。所有GE2D均临时加载后恢复，生产继续三个模块/NEON默认路径。

当前encoder短矩阵：NV12/NV21/YUV420/YUYV/UYVY/YUV444、RGB24及四种
32-bit RGB共11布局各8幅，1080p10幅，854×480/SAR17:13十二幅，GOP/强制IDR、
多slice、同fd重开、VBR、峰值CPB和非法profile拒绝后恢复均完成。工作机官方
FFmpeg完整软解、帧数/尺寸/YUV比较通过，最小亮度PSNR约38.83 dB；三字节
BGR按原接口拒绝，未列为支持格式。NV12输入的`-m709`只选择客户端转换参数，
不据它声称驱动写出了709色彩标签。JPEG质量85的1080p三幅全量程软解通过。

两路720p和两路1080p分别以motion/gradient并发各12幅，独立软解及YUV通过，
最低亮度PSNR约38.91 dB；前者采样最低CMA约11.9 MiB，后者约5.1 MiB。
只证明这些有界会话，不保证任意码流、八路1080p或实时总吞吐。相同1080p源
首60图的阶段命令：decode/null约1.827 s、CPU软件缩放至720p约7.119 s、
硬解→4 Mbps硬编约2.821 s；转码输出完整60/60，Y/U/V PSNR约40.67/39.93/
39.29 dB，约0.71×，未达1.3×目标。解码读前分别转换61/81/81幅，阶段计数
含读前和应用命令开销，不当作纯引擎吞吐；无音频/HLS及GE2D完整收益结果。

最后完整健康检查在`de20e87`：包清单通过、unit enabled/active、五容器运行、
四项HTTP200、decoder帧池归零、26个启动文件哈希不变；没有由代理重启或
修改boot DT/CMA。临时GE2D已移除，默认NEON。此前SSH/Tailnet断连期间，
用户确认本地网页正常并安排修复，远程随后恢复；不能混同历史LAN同时
失联或据此认定内核崩溃，远程链路根因仍未闭环。冷启动/异常重启、长期稳定性、
原版迅雷同机对照及剩余协议能力仍未完成。

记录标识`post-reboot-de20e87`，GE2D水平分段候选为`db13f30`；已取得的原始
输出、模块/工具、源码与逐帧比较在工作区外本地SHA256校验归档，无永久公开
附件。正式部署、临时GE2D和未采用的诊断版本分开记录。

11-25后续原生GE2D复验取消逐字节FNV读取，仅保留FIFO可见帧MD5，H.264
small50/50、1080p300/300，HEVC small10/10、crop10/10、1080p30/30全部
一致，计数400；原生客户端不读取图片、交付至/dev/null时1080p300幅约7.27 s。
这不是完整转码或OpenList吞吐。原版7.0.2在CAPTURE 2/8仍可提前终止，8缓冲
HEVC1080p日志明确CMA分配失败（8槽参考面只取得7槽）；保留失败，不默认启用
GE2D。以前145幅超时含诊断FNV读取，不再将它单独归为引擎性能故障。

显示元数据实机读取：H.263 baseline128×96的CROPCAP y/x=11/12（SAR12:11），
PLUS164×100为1/1；HEVC可见854×480、coded bounds864×480，crop/compose
及bounds与驱动约定一致。原版FFmpeg对非方形SAR的已知方向bug未改变。
新增176×144、352×288 H.263 baseline各12/12顺序/YUV通过；试验中两份
structured_slices样片主机转换明确-EOPNOTSUPP，驱动队列错误后旧客户端未退出，
现已补POLLERR/ERROR/DQBUF终止错误处理，主机故障注入和实机拒绝→重开正常流
通过。此项不将尚未支持的Slice Structured列为移植成功。

后续官方工具对照使用独立的可选`--bugfixes`7.0.2验收程序（生产程序未替换），
CAPTURE 2下H.264 small50/50、HEVC small/crop各10/10、1080p30/30精确一致；
H.2641080p仍40秒超时，未计通过。该工具只用于隔离已复现的drain/POLLERR等
用户态缺陷，不把可选补丁结果标作未修改官方FFmpeg通过，也不改变默认构建。
八缓冲原版测试在HEVC1080p明确参考面CMA不足，相关输入不能靠简单增缓冲解决。

GE2D纹理缩放后续发现常量平面不能覆盖的缺陷：缺少水平分段读取使输出按
固定64像素段重复，独立软件bilinear比较仅约13–16 dB。补齐S805厂商的
`hsc_div_en`、`(124<<24)/phase_step`分段长度与advance后，640×384的三幅
不同NV12纹理缩至320×192，Y逐字节相同，U/V PSNR约36.16/33.50 dB；缩至
480×288，Y/U/V约52.32/43.80/41.32 dB，最大平均绝对差约0.72。边缘色度
插值仍不与软件逐字节相同，不宣称所有纹理/比例/质量通过。主机真实MMIO
函数新增分段及非缩放清零检查，125项回归/严格checkpatch/ARM双入口和补丁
一致性通过。此GE2D候选仅临时验证，不加入生产默认模块/Overlay。

不启用Slice Structured的H.263 PLUS320×240、704×396各12/12顺序/全平面
比较通过；两种提前STREAMOFF顺序后新会话正常。连同baseline176×144、
352×288各12/12及四份三图流，仍按本次样片范围声明，未扩大到可选编码工具。

## 11-25 MJPEG 色度行跨度修正

固定S805厂商vmjpeg使用三平面canvas和PSCALE。新增四组三幅图案流复验：
所有输出均3/3，但4:2:0的854×480在奇数色度行出现条纹，U/V平均绝对差约
12.3/16.3；仅清除公共启动路径的两平面位，四组输出逐字节不变，未采用。
PSCALE的色度行跨度需独立按32字节对齐，不能直接使用对齐后亮度宽度的一半。
修正两个色度canvas、帧池容量及逐行SWAP64/交错拷贝，保持标准NV12M输出
stride，清零padding。真实函数回归在旧源码失败、新源码通过，覆盖854、1440、
320、1920和34宽及不足容量；126项主机检查、sanitizers、严格checkpatch、
ARM外部/additive构建与补丁一致性通过，干净源码包回归通过。

修正后854×480三图U/V平均绝对差降至约1.0–1.1/1.7–1.8，最低PSNR约
33.5/30.4 dB，顺序/全平面检查通过；1920×1080三图也通过，最低Y/U/V
PSNR约37.8/36.2/32.6 dB。320×240的4:2:0图案虽顺序正确，最大平面
平均绝对差约3.8，超过既有2.0门槛；4:2:2样片图像严重异常。两者保留失败，
不以完整帧数或厂商滤波差异为由扩大支持范围。

源码`f581f98a24f53402bb91cec7f1c6b2026a527b0a`经标准安装保留部署，
decoder SHA256为`f9569fd03ebd213a40a5bddb73f0a4b7cba6d5b62418a57bea5fd0c82a7adc73`；
encoder/runtime及默认Overlay字节未变。正式包MJPEG854×480/1080p各3/3
再次检查；原版FFmpeg的H.26450/50、HEVC10/10、tiles4/4与自然EOS20/20
精确一致。上一de20e87整包/unit/固件state先在设备及外部校验备份，
安装使用全新state；无内核/boot DT/CMA变更、重启或整包降级演练。
最后包清单通过、unit enabled/active、五容器运行、四项HTTP200、decoder帧池
归零、启动文件哈希不变。该源码的Host及ARM/package两项CI均通过。
记录标识`mjpeg-stride-f581f98`，原始图像、诊断源码、逐帧质量及安装资料
在工作区外本地SHA256归档，无永久公开附件。

同期MPEG-4 I/P与B流各三种不含图像数据的VOP/sequence-end边界顺序，
六组仍28/30，未采用新结束标记。上游2026-10的
[MPEG-4提案](https://lkml.iu.edu/2610.3/01574.html)使用S4的frame-based微码和
私有stateless参考控件，不是当前S805 stream-based固件的可直接替换协议。

## 11-25 MJPEG 滤波与输入边界

源码`41773bbae5edf8e932f8da914c68a9ef6163cd8e`改用固定S805
[jpegdec.c的双点滤波表](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/jpegdec.c#L197)。
旧MJPEG三角表在1:1输出时仍平均邻近像素，导致小尺寸纹理超过既有误差门槛；
新表在整数相位保留原样本。微码、canvas、颜色范围与输出几何保持各自协议。
真实BMEM编程函数回归在旧表失败、新表通过；SOF/SOS检查覆盖截断、全部
16-bit段长度、采样/精度/尺寸、重复分量与scan绑定。128项Python、C sanitizer、
严格checkpatch、原ARM外部/additive构建、补丁一致性及干净源码包回归通过。

同一候选十组SOF0输入各三图（含同内容的头部对照），共30/30顺序与全平面
质量通过：FFmpeg的4:2:0小图、854×480、1080p，独立libjpeg/Pillow的
4:2:0/常见4:2:2纹理及渐变、4:4:4，以及直接DQT和带COM的相同JPEG。
4:2:0最大全平面平均绝对差0.025，最低PSNR约64.2 dB；4:2:2与4:4:4
向NV12M的硬件色度采样仍不逐字节等于软件，最大平均绝对差分别约1.19/1.29，
最低PSNR约35.7/37.3 dB，未放宽原2.0门槛。

现有失败4:2:2样片为Y=2×2、Cb/Cr=1×2；常见成功布局为2×1、1×1、1×1。
两者虽有同一采样比例，固件并非都能处理。SOF1扩展顺序JPEG也输出3/3但
图像错误，渐进JPEG花屏，灰度仅0/3；不以帧数宣称支持。当前仅接收已实测
的8-bit SOF0三分量交错scan及归一化4:2:0/4:2:2/4:4:4，以上失败组合明确
返回EOPNOTSUPP，协商尺寸不符返回EINVAL；该检查不改码流或做软件解码。
五类错误均在零输出时终止队列，随后有效输入可重开；两种提前STREAMOFF
顺序各交付一图后正常退出，帧池归零，正式service/OpenList恢复。

本机直接DQT开头的输入与带COM版本软件/硬件内容均一致；没有套用另一芯片
[S4提案](https://lkml.iu.edu/hypermail/linux/kernel/2610.3/01570.html)中补COM或仅接受
4:2:0的条件。正式decoder SHA256为
`950643c882a8b44cc52d103154db0e3565b81f73b6704002735c1bcf7d7c4151`。
正式包已按标准安装保留部署，encoder/runtime与默认Overlay字节未变；十组
30图的内容/质量、五类拒绝及两种提前关闭顺序在安装后再次通过。原版FFmpeg
H.26450/50、HEVC10/10、tiles4/4及自然EOS20/20精确一致。包校验、unit
enabled/active、五容器、四项HTTP200、帧池归零与启动文件哈希不变均通过。
该提交的Host与ARM/package CI通过；上一f581f98整包/unit/state已在外部
SHA256备份，新安装用独立state，无重启、boot DT/CMA修改或整包降级演练。
记录标识`mjpeg-quality-41773bb`；原始输入/输出、逐帧质量、安装状态及工具
在工作区外本地SHA256归档，无永久公开附件。
本次范围不证明所有JPEG采样组合、渐进/灰度或迅雷原厂同机对照已完成。

固定厂商另有[S805静态JPEG固件](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/amports/m8/ucode/jpeg/jpeg_mc.c)
及独立jpegdec的头部/配置握手，允许报告单分量。它尚未移植/实机验证，不能
据当前MJPEG固件失败断言S805硅片缺少这些JPEG能力。

## 11-25 旧格式尾图现场与 GE2D 官方工具链边界

基于83b1881的两轮临时诊断分别读取四个内部帧槽、终止drain时的寄存器和
LMEM；生产模块仍为41773bb。MPEG-4 I/P、B流均28/30，WMV3为3/5，但内部
槽中可找到对应最后两幅完整Y/U/V：MPEG-4的参考28/29、WMV3的参考3/4
（从0计数），不是仅有相似网格签名。两组MPEG-4槽号排列不同，不能固定
挑槽补齐。VIFIFO接近耗尽、bit count为0，固件仍保留当前/参考图状态。
这把调查缩小到完成/显示消费协议，尚不能证明硬件最终写入已经同步完成，
也未闭环START续播会否重复交付；因此未合入读取LMEM补图或改写微码的方案。
正常原件与诊断模块/寄存器/逐平面比较在工作区外SHA256归档。

同一已部署decoder加临时GE2D db13f30，官方原版7.0.2使用标准参数：
OUTPUT由默认16减至2、CAPTURE设4时，三份NV12哈希仅11/50、4/10、4/30；
强制转planar仍为8/50、4/10、4/30。CAPTURE12时H.2641080p短流10/10精确，
小流仍29/50，HEVC1080p明确CMA不足、八参考槽仅分配四槽，并触发原版工具
的错误断言。增加缓冲或改像素格式都不能单独认定解决。

真正加入scale后，OUTPUT2/CAPTURE12的H.264小流320×240→160×120为50/50，
1080p短流→1280×720为10/10，两份全可见NV12哈希均与独立软件scale完全
一致；后者约6.20 s，只是短链内容验收，不是完整转码/1.3×吞吐通过。
HEVC1080p使用CAPTURE8缩至720p仍8/30，已输出前缀精确，整链失败。
所有检查有界完成并恢复生产三个模块/NEON，不以此启用GE2D默认部署。

新增真实FFmpeg dequeue函数回归证明：上游drain在CAPTURE暂被消费端持有时
提前done，既有可选修复继续等待随后完成的图片，并区分空LAST与终止错误。
原fixture已带drain补丁，再次应用会自动反向撤销；现恢复固定官方原文件、
锁SHA256、强制forward，129项主机检查通过。生产FFmpeg未替换；软件状态机
回归不能证明上述实机短输出全部归因于同一个bug。

期间系统分区普通可用空间降至0，新脚本传输失败。仅将本轮生成的诊断结果、
临时包移出设备：180+1088+205个文件先在构建机和外部归档，逐文件SHA256
与设备核对后删除，合计释放399305226字节（约381 MiB），普通可用空间恢复
约138 MiB。安装包、state、原始回退备份和业务数据保留；传输失败的缩放
任务未实际运行，补齐脚本后才记入上述有效验收。记录标识`legacy-tail-ge2d-upstream`。

## 11-25 HEVC SPS 帧池预算

源码`39703c70bc50be183e6f7c9d74fb4adf46468016`以当前AU绑定的SPS DPB
上限替代“重排数+6”的固定余量，保留重排数加一的下界，再加一个异步交付
槽，16槽封顶。平台最小帧池从6改为2；引用仍持有decoder位，待交付图片仍
持有waiting位，满池按原路径等待，未降低码流声明的DPB或伪造参数集。
真实RPM函数覆盖DPB1–16及所有136组合法重排组合，新增回归旧源码失败、
新源码通过；129项主机、C sanitizer、严格checkpatch、原ARM外部/additive
构建、补丁一致性和干净源码包检查通过。

候选原生CAPTURE1的13组输入共244/244全可见NV12哈希精确：small/crop/1080p，
DPB16 32幅、CRA24幅、开放GOP36幅、裁剪切换33幅、自然EOS20幅、tiles4/3幅、
全I/低参考各6幅及故意慢速消费的1080p30幅。慢消费保留完整内容、两种提前
STREAMOFF顺序正常；1080p池峰值由8槽24 MiB降至6槽18 MiB。DPB16仍用
16槽。额外全I流仅把SPS DPB声明从3改为1，独立官方软件-xerror完整软解
确认6幅像素不变，实机最小两槽为196608字节、6/6精确；该控制素材不是正式
输入的改写或放宽要求。

同一候选显式GE2D、官方未改7.0.2、OUTPUT2下：CAPTURE9加实际720p scale
仍9/30，CAPTURE10完成30/30且与独立软件scale逐帧哈希一致，约16.82 s。
CAPTURE10直接NV12仍12/30，说明CMA节省只使该缩放链可以容纳足够缓冲，
未消除原版工具的暂态drain错误。两槽DPB1的GE2D直接客户端也6/6精确。
不据此宣称全部官方工具链、实时吞吐或默认GE2D部署通过。

正式包已按标准安装保留部署，decoder SHA256为
`6bcc728e07cecd29e9de4937536b54ed3e169f1863d8d736534064e0df1b2c9a`；
encoder/runtime及默认Overlay字节未变。正式NEON矩阵14组250/250全像素
精确（含两槽DPB1），两种提前关闭顺序通过；核心原版FFmpeg的H.26450/50、
HEVC10/10、tiles4/4及自然EOS20/20精确。该源码Host与ARM/package CI通过。

但正式包临时GE2D重复验收失败：两槽DPB1仍6/6，随后原版FFmpeg缩放进程
SIGSEGV、零输出；清理后的service因`tools/ffmpeg`校验不符拒绝启动，OpenList
因缺设备节点未启动。后续独立全包校验及重复读取又与既有清单完全一致，
备份比较差异为0字节；未得到持久损坏原件，不能认定文件系统或DMA根因。
仍按已验证的原始工具字节原子恢复，通过原包清单后启动默认NEON和OpenList。
GE2D继续关闭，本次正式GE2D复验保留失败，不把之前候选30/30扩大为正式全链
通过。最后包校验、unit enabled/active、五容器、四项HTTP200、帧池归零及
启动文件哈希不变均通过；没有重启或调整boot DT/CMA。

## 11-26 GE2D 命令排空

源码`f321f6d`补齐`STATUS0`的缓冲命令有效位bit2和流水线命令有效位bit4，
与原有busy、当前命令和目的写响应计数一起等待归零，避免仅命令尚在队列
时提前归还DMA缓冲；初始化复位也使用相同空闲条件。依据为固定
[Linux 5.15.79寄存器定义](https://github.com/gregkh/linux/blob/v5.15.79/drivers/media/platform/meson/ge2d/ge2d-regs.h#L134)。
实际命令函数的主机回归依次注入队列、流水线、运行、写响应与空闲ready状态；
旧代码提前成功而失败，修正后通过。两种有效位各自卡住时都超时复位、拒绝
后续命令；ready位不阻塞空闲。129项主机测试、C sanitizer、严格checkpatch、
原5.15.79 ARM外部/additive构建及补丁源码一致性通过。

独立候选模块SHA256为
`510d5cc33f77a62f97add811eadc8b9f3422742e35788abcac7080723495c1af`。
WanOS原5.15.79-meson/64 MiB CMA下临时加载，仅运行GE2D、不加载decoder：
probe自检通过，六组三幅共18/18内容与时间戳通过，覆盖GREY复制/缩小、
1080p NV12M复制、NV12缩小、NV12M裁剪及DMA-BUF NV12M缩小。复制/裁剪
核对全部可见像素；缩小使用常量Y/UV平面，不扩大为纹理滤波或解码转码验收。
首轮客户端因宿主glibc版本过新未执行，恢复服务后改用同源静态客户端重跑，
不把该兼容性失败计为GE2D内容结果。

临时模块已卸载并恢复`39703c7`默认NEON；恢复后H.26450/50、HEVC10/10
逐帧哈希精确，完整安装包校验、unit enabled/active、五容器运行、四项HTTP200
及帧池归零通过。未改变内核/boot DT/CMA或启用生产GE2D；完整
官方FFmpeg解码缩放链未重跑，11-25的SIGSEGV及瞬时校验异常根因仍未知。
本次只证明完成状态保护和上述独立有界检查，不能据此关闭历史崩溃问题。

此前被撤销的`dma_alloc_noncoherent()`帧池候选已核对失败路径：Linux 5.15.79
的`dma_direct_alloc_pages()`不允许HighMem，拒绝该CMA页后退到高阶普通页
分配；原始日志中的1080p/部分裁剪与tiles启动分配失败与此一致。目标配置
启用HIGHMEM，不能用小流通过或Host allocator桩证明大帧可用。当前保留
原分配器，coherent分配与cached别名的替换仍未解决；未采用缺乏内核CPU
映射保证的`DMA_ATTR_NO_KERNEL_MAPPING`变通。原始失败及本次报告均仅在
忽略目录和经校验的工作区外归档保留，无永久公开附件。

## 11-26 GE2D coherent 帧池

源码`ffddcf0c6b3653ce3c148d4a5ba807decacd6e07`让显式`capture_ge2d=1`
帧池在coherent分配后直接返回，不再建立cached别名；CPU回退及裁剪统一
使用coherent地址，不对该地址调用streaming同步。默认NEON的cached读取
保留，不能据此宣称所有帧池缓存映射已替换。目标原5.15.79未导出CMA分配
接口、未开启DMA heap，noncoherent仍拒绝HighMem，不采用私有内核符号。

实际分配/释放函数的Host回归覆盖直接/重映射coherent地址、两种输出模式及
分配/页数组/vmap失败释放；旧代码因GE2D仍创建cached映射而失败，修正后
通过。真实转换函数覆盖cached/coherent普通NV12、裁剪、畸形窗口与MJPEG
色度，coherent回退不执行streaming同步。130项主机、C sanitizer、严格
checkpatch、原ARM外部/additive构建和补丁源码一致性通过。

候选decoder SHA256为
`5dec820fa46d20bdafbbe0ddae911ea1192482243e6fbf3abbaa784a72397b45`，
GE2D仍为`510d5cc33f77a62f97add811eadc8b9f3422742e35788abcac7080723495c1af`。
WanOS原5.15.79-meson、64 MiB CMA下，显式GE2D/CAPTURE1的16组共600/600
可见NV12逐帧哈希精确：H.26450/300、HEVC small/crop/1080p10/10/30、
DPB16 32、CRA24、开放GOP36、裁剪切换33、自然EOS20、tiles4/3、全I/低参考/
两槽DPB1各6，以及慢消费1080p30幅。两种提前STREAMOFF顺序通过。

三组MJPEG 4:2:0 small/crop和4:4:4共9幅触发CPU回退，GE2D计数不增加；
完整顺序及Y/U/V质量对照通过。首轮在完成600幅后因设备端旧素材metadata
缺失中止，已恢复服务；补传三份校验过的最小素材后完成该回退检查，不把
脚本素材错误误记为驱动失败。

原版未改FFmpeg7.0.2、OUTPUT2下，H.264小流CAPTURE12→160×120为50/50，
1080p短流CAPTURE12→720p为10/10，HEVC1080p CAPTURE10→720p为30/30；
全部与独立软件scale逐帧哈希精确，命令约0.93/5.98/17.29 s。重新加载候选
后，同一HEVC命令两次各30/30精确，GE2D计数各增加30，约17.09/16.95 s。
不据此宣称实时吞吐、硬件缩放全链或任意缓冲配置；本命令scale仍由CPU执行。

上述临时检查均恢复`39703c7`默认NEON，安装包和工具哈希一致、五容器及
帧池归零；恢复后H.26450/HEVC10逐帧精确，四项HTTP200。未复现11-25崩溃
及瞬时校验异常，但没有捕获其故障时状态，仍不认定根因已闭环。无生产
GE2D默认启用、内核/boot DT/CMA变更、重启或持续压力实验。

随后以同一源码完整运行包保留正式部署，生产继续默认NEON。升级前完整
`39703c7`运行包、固件、systemd和安装state已在设备外逐文件核对，使用新state
标准安装。正式decoder哈希与候选一致，encoder/runtime及默认Overlay字节不变。
正式16组600/600可见像素哈希精确、三组MJPEG9幅全平面质量及两种提前关闭
顺序通过，GE2D计数为0；原版FFmpeg的H.26450/HEVC10再次逐帧精确。
完整63项包校验、unit enabled/active、五容器运行、四项HTTP200、帧池归零及
26个启动文件哈希不变；未演练整包降级。130项主机及干净源码包检查通过。
完整报告和安装/回退资料仅经校验归档在构建机，无永久公开附件。

## 11-26 编码参考面预算与完整转码边界

源码`799d2cc`按参考canvas实际NV12容量分配两张参考面：32字节行宽、16行
高度对齐，再按页向上取整；与既有canvas配置和色度地址一致，独立于输入
像素格式。替代所有尺寸各3 MiB的固定分配，不改变workspace、码流和analysis
区域协议。两张参考面在320×240减少6053888字节，在1280×720减少3522560
字节（约3.36 MiB），1920×1080减少24576字节；没有减少有效参考行。

真实分配/释放函数Host回归覆盖32×32至1920×1088、参考Y/UV最后字节及
任一DMA区域申请失败时的释放，旧代码不能满足参考面预算断言、新代码通过。
131项主机及干净源码包测试、C sanitizer、严格checkpatch、原5.15.79 ARM
外部/additive构建与补丁一致性通过。候选encoder SHA256为
`219da47d296e71525b2c816f7f85c5c2f8663c38d0585586c89e78983716a32c`。

WanOS原5.15.79-meson/64 MiB CMA下临时候选19组共148幅全部独立软解、
顺序和完整Y/U/V质量通过，最低平面PSNR约39.02 dB。覆盖11种支持输入布局、
320×240/854×480/720p/1080p、GOP/强制IDR、多slice、同fd重开以及两种尺寸
JPEG；BGR24仍按既有接口拒绝。候选NEON的1080p直接/缩至720p再编码各
60/60完整软解及全YUV质量通过，最低平面PSNR约39.29/41.90 dB。

本次完整转码输入为既有1080p30样片的前60幅，先在工作机以stream-copy
截取，独立软解与两尺寸参考也在工作机完成；设备生成的60幅ES逐字节核对
同一SHA256。采用输入自然结束，不以提前`-frames:v`截断掩盖尾部。官方未改
FFmpeg7.0.2的8个decoder CAPTURE/2个encoder OUTPUT组合，四条命令都
返回0，但实际只有NEON直接11幅、NEON缩放8幅、GE2D直接/缩放各8幅，独立
软解已确认，全部保留失败且不计算有效吞吐。

decoder CAPTURE20、encoder OUTPUT16的默认NEON组合完成两条60/60及
全平面质量，命令约3.42/7.38 s；候选同命令约3.91/7.62 s，不宣称加速或
达到1.3×实时。输入压缩OUTPUT均为2、encoder CAPTURE为4。GE2D的decoder
CAPTURE12下，即使缩容后720p参考面已降至1384448字节，仍因CMA不足而
申请失败；CAPTURE10能申请参考面，最后的1114112字节analysis区域失败。
CAPTURE9能够启动编码，但仅9/60、返回0，仍失败。不能把单独解码加CPU
scale的150幅通过扩大为解码、缩放和编码同时运行的完整转码通过。

缩容候选不改变原版FFmpeg的缓冲保留/drain行为；源码默认帧线程队列为8，
V4L2编码入口复制软件帧，没有DMA-BUF直读交付。后续必须同时解决有效
缓冲数量、64 MiB CMA预算与消费端持有帧的时序，不能仅凭退出码或局部
GE2D计数宣称零拷贝、内容完整或性能达标。历史H.264少帧问题不据此关闭。
临时模块均恢复默认生产包，包校验、五容器、帧池归零及随后四项HTTP200
通过；OpenList启动瞬间的5245拒绝连接已保留，随后复核为200。

同一源码完整运行包随后通过标准安装保留部署，默认NEON。上一版`ffddcf0`
63项运行包与12项安装state文件逐一匹配设备外校验备份，使用全新安装state；
正式encoder哈希与候选相同，decoder/runtime和默认Overlay字节不变。正式
19组148幅再次完整软解、顺序/全YUV质量通过，两条转码各60/60内容通过，
约3.62/7.49 s；不宣称速度提升。完整包校验、unit enabled/active、五容器、
四项HTTP200、帧池归零及26个启动文件哈希不变，GE2D未加载。没有重启、
改变CMA或演练整包降级。完整输出、失败现场及安装/回退资料仅经校验保存
于工作区外构建机归档，无永久公开附件。

## 11-26 GE2D 采集内存与官方转码

驱动源码`0ebf549`、DMA-BUF测试客户端`7991ba4`。显式GE2D的MMAP采集改用
缓存vmalloc内存，只按显示几何分配一张coherent中转面；交付锁保护GE2D
写入与CPU拷贝。1080p20个MMAP缓冲从约60 MiB CMA变为共享3 MiB，不含
原参考池。DMA-BUF导入仍由原`vb2_dma_contig`验证连续范围并直接写入；
MMAP导出可能分散，不保证连续导入。尺寸切换/停流/关闭释放中转面，
pool_bytes包含它；MJPEG和错误路径保留CPU回退。默认NEON路径与4 MiB
VIFIFO保持，未重新采用旧2 MiB FIFO候选。

实际分配适配器和转换函数的Host回归覆盖两种内存模式、导出/同步/释放、
失败清理、中转面复用/重分配、padding、超时回退与停流释放。客户端新增
DRM dumb-buffer导入、CPU同步及清理。133项Host/干净源码包、C sanitizer、
严格checkpatch、ARM外部/additive构建及补丁一致性通过。decoder SHA256：
`6d499aa75f39959186401d9fd97afc398fd962632d98ad9b63f64498c8adf058`。

WanOS原5.15.79-meson/64 MiB CMA下，显式GE2D16组600/600可见NV12逐帧
精确，范围沿用前次small/crop/1080p、DPB16、CRA/开放GOP、裁剪切换、自然
EOS、tiles、两槽DPB1和慢消费。HEVC small/crop、裁剪切换、自然EOS共73幅
经Meson DRM连续DMA-BUF导入，其余MMAP；三组MJPEG回退9幅完整顺序/全YUV
质量通过，两种提前停流正常。首次误选Lima节点不支持dumb-buffer，恢复后
核对Meson节点重测通过，初次失败不计为DMA-BUF验收成功。

同一1080p30前60幅输入、自然EOF、decoder OUTPUT2/CAPTURE20、encoder
OUTPUT16/CAPTURE4、4 Mbps/GOP30：官方7.0.2直接转码和CPU缩至720p再编码
各60/60，独立完整软解/全YUV质量通过，最低平面PSNR约39.29/41.91 dB。
GE2D/编码器计数各加60，墙钟约3.09/7.72 s；重加载后再次各60/60内容通过，
约3.86/7.51 s。波动不构成加速结论，仍未达到实时或1.3×。

同一OpenList镜像内未改官方8.1.3在独立临时容器重测两条命令，各60/60
内容/质量通过；docker命令约10.02/12.06 s包含启动，宿主docker客户端CPU
时间不代表容器FFmpeg CPU。临时容器root访问私有目录；初次默认用户被目录
权限拒绝、未解码，保留环境失败。生产容器用户、配置和FFmpeg字节不变。
仅覆盖视频/CPU scale/现有复制编码，不扩大为音频/HLS、硬件缩放、零拷贝、
低缓冲drain修复或默认生产GE2D。历史崩溃根因仍未知。

取报告时远端SFTP子进程Go运行时SIGSEGV，SSH/业务检查正常；改SSH字节流
并核对SHA256，不归因驱动。工作机ENOSPC曾中断下载/源码包；逐文件匹配
外部归档后只清理完全重复的旧源码包，未匹配副本保留，重新取报告和生成
源码包。新现场和生产回退资料保留；环境错误不计为编解码内容通过。

同一源码完整包`7991ba41c35f088348cc86eaf416ad7499a7ca0a`已标准安装保留，
默认NEON。升级前63项运行包与12项state文件逐一匹配设备外校验归档，使用
新安装state。正式decoder哈希与候选相同，encoder/runtime和默认Overlay
字节不变。正式16组600/600精确、三组MJPEG9幅全YUV质量及两种提前停流
通过，GE2D计数0；完整包、unit enabled/active、五容器、四项HTTP200、
帧池归零及26个启动文件哈希不变。没有重启、修改CMA、启用生产GE2D或
演练整包降级。原件、完整源码包/运行包与新旧state经校验仅在构建机归档，
没有永久公开附件。

## 11-26 解码格式协商边界

源码`b8c37a8`及验证客户端`8d7ad11`修正格式协商：尚未提供decoder缩放时，
CAPTURE TRY_FMT/S_FMT按当前源宽高返回，不再让采集请求覆盖源几何。S_FMT
与队列分配共用session锁；CAPTURE已有缓冲时返回EBUSY，改OUTPUT时还要求
OUTPUT无缓冲。OUTPUT已有缓冲而CAPTURE为空仍可设置CAPTURE，保留标准
source-change流程。非法队列类型返回EINVAL，不在失败前改写会话状态。
Linux5.15.79的S_FMT由video_device锁序列化、队列ioctl由session锁序列化，
回调显式取得后者以免分配与格式修改竞态；不是对已持有队列锁再次加锁。

真实回调Host回归覆盖两队列全部分配状态、几何/像素格式/色彩/容量不变和
错误类型；旧代码接受任意CAPTURE几何而失败，另将busy检查移除后回归也
失败。133项Host与干净源码包测试、ASan/UBSan、严格checkpatch、原ARM
外部/additive构建和补丁一致性通过。候选decoder SHA256：
`c459ff61d9a455d957f86cf2ecfab5860078ed0b456a1df122968dc3217cbc3a`。

客户端在每次CAPTURE配置时主动请求不同尺寸，验证TRY_FMT/S_FMT归一化，
REQBUFS后验证CAPTURE/OUTPUT格式修改为EBUSY；OUTPUT初次分配后也验证
EBUSY。候选默认NEON与显式GE2D各16组600/600可见NV12逐帧精确，含尺寸/
裁剪切换、DPB16、CRA/开放GOP、tiles、自然EOS、最小池及慢消费；GE2D其中
73幅经真实连续DMA-BUF导入。两路径各三组MJPEG9幅全平面质量和两种提前
停流通过。这些检查不开放decoder缩放，不把CAPTURE尺寸变化冒充缩放。

同一完整1080p60幅输入，官方7.0.2显式GE2D直接/CPU缩720p转码各60/60完整
软解和全YUV质量通过，命令约4.34/8.22 s，不代表加速。随后官方8.1.3直接
转码输出虽有60幅且内容通过，但docker命令超时35 s；接着containerd拒绝
连接，缩放命令未启动（exit125）。8.1.3两项保留失败，不继承上轮通过。

containerd进入自动重启/Go运行时异常，OpenList停止，已中止后续新硬件实验。
现场两次读取containerd曾与包md5不同，取回的二进制又与固定软件包完全
一致、差异0字节，软件包SHA512已独立校验；停止崩溃循环后复读也一致。
对单一二进制执行page-cache recheck后启动服务，但它在操作前已校验一致，
不能将恢复归因于缓存操作，更不能据此断定DMA、磁盘或格式修正是根因。
没有替换containerd二进制、内核/boot DT、CMA或重启设备。

恢复过程中先前失败脚本的定时恢复任务再次卸载了模块，systemd仍显示旧
active状态；已取消该实验的timer，显式重启默认驱动后恢复设备节点与
OpenList。最终在役仍为`7991ba4`，本次候选没有安装；故障现场和未知根因
保留。后续先解决可靠取证与磁盘余量，再继续decoder缩放及硬件实验。

恢复核对包含63项包校验、containerd与驱动unit active/自启、两个视频节点、
五容器/四项HTTP200、帧池归零、GE2D关闭及26个启动文件哈希不变。另将旧
失败候选包64项和5个已封存journal共约200 MiB先复制到设备外逐文件核对，
再在设备重查同一清单后删除对应副本；保留活动journal、本次故障日志、业务
数据及生产回退包。日志与二进制现场、修复源码/模块和完整测试原件已在
构建机校验归档，无永久公开附件。环境故障不改变代码回归通过范围，也不
构成候选正式部署或历史崩溃修复的证据。

## 11-26 编码 DMA-BUF 同步事务

驱动`48a41e3`、验证客户端`7673d62`补齐编码导入输入的同步。图片处理前
对每个DMA-BUF等待生产者exclusive fence，每平面最多1 s，可中断；超时或
中断直接报错，不提交当前图片。直接MFDIN读取在每次使用时重新同步映射的
SG表，避免同一导入缓冲再次排队时只依赖首次map。CPU回退先begin再取得
vmap，映射失败配对end；正常复制后的end错误原样返回，不再继续H.264/JPEG
图片命令或计为成功拷贝。MMAP输入不增加fence等待和DMA-BUF CPU访问。

客户端对导入平面的初始化和逐帧CPU写入使用标准DMA_BUF_IOCTL_SYNC的
WRITE START/END，EINTR/EAGAIN重试，其他错误终止。该客户端此前缺少这些
同步，旧MMAP/引用素材生成不受影响；不能把旧导入测试扩大为完整缓存验证。

实际input辅助函数与H.264/JPEG入口Host回归覆盖每平面fence超时/中断、
重复直接读取、非连续回退、begin/vmap/end失败及配对、单平面NV12与多平面
NV12/YUV420；同步失败时当前图片不启动、无成功计数。旧代码忽略end失败
而回归失败，修正后通过；三个故障注入分别撤去单平面/多平面错误传播和
fence等待错误传播，均被抓住。客户端回归也覆盖sync重试和永久错误退出。
136项Host/干净源码包检查、C ASan/UBSan、严格checkpatch、原5.15.79 ARM
外部/additive构建与补丁源码一致性通过。候选encoder SHA256：
`2a55b4105162fb2a61f785bc07877fc573c6353c08402eeb8ef9127c2459f625`。

本次只在工作机验证，未连接/加载实机、未安装候选，继续保留此前恢复的
`7991ba4`生产包。真实导出器cache/fence、完整图片/取消/重开和吞吐未新增
实机验收；Host桩和编译不证明DMA硬件行为，不关闭历史containerd/SFTP/
FFmpeg异常或尾帧问题。源码/模块及完整报告仅在构建机校验归档，无永久
公开附件。硬件缩放、音频/HLS和实时目标仍未完成。

## 11-26 decoder GE2D 缩放候选

驱动源码`06504f3`、验证客户端`834b7e3`。H.264/HEVC在显式GE2D且码流尺寸
已知时，CAPTURE TRY_FMT/S_FMT可独立选择每轴1–1/4的偶数输出；越界/奇数
请求归一化并返回实际值，0表示源尺寸。输出配置绑定对应源几何，新源尺寸
查询回到源大小，应用按标准source-change流程在释放采集缓冲后重新协商。
OUTPUT S_FMT清除旧配置/元数据状态；分配后的S_FMT保持EBUSY。默认NEON、
MJPEG、码流尺寸未知和不适用的源尺寸保留原尺寸输出。

解码参考面及图片队列继续保存源尺寸/crop，采集缓冲、stride、容量检查、
中转面和bytesused按输出大小计算。G_FMT与COMPOSE报告采集大小，CROP保留
源窗口；非等比例缩放修正标准像素宽高比。转换失败的缩放图片返回ERROR及
零payload，禁止原尺寸CPU回退覆盖小缓冲。没有软件模拟像素填充来证明
真实硬件滤波；Host转换桩只验证地址、裁剪、尺寸、padding及错误分支。

真实回调和交付函数回归覆盖协商/G_FMT/selection、分配后拒绝、源尺寸不变、
source-change、新流清理、较小采集容量、payload、MMAP中转与DMA-BUF直接
输出、失败拒绝CPU回退；几何/phase/像素比例检查共307200组尺寸请求，含
极值和非等比例。两个故障注入分别恢复源尺寸输出和错误的全尺寸CPU回退，
均被回归抓住。138项Host与干净提交源码检查、ASan/UBSan、严格checkpatch、
原5.15.79 ARM外部/additive构建及补丁源码一致性通过。候选decoder SHA256：
`2328ac8de7a60896a6dc5a3a8f8c16f17c6a7e06f23721f3e22b0a9cbaa3694c`。

本轮未连接/操作实机，候选没有安装，不能从Host模型/既有GE2D样片继承
真实缩放内容、缓存/回压、取消/尺寸切换或吞吐验收。客户端CAPTURE_SIZE
入口已编译并回归参数校验，真实dump/软解及滤波质量待补。官方FFmpeg的
scale过滤器不调用该接口，仍是CPU缩放；未修改FFmpeg或生产应用，尚无
新的音频/HLS、实时/1.3×或原厂系统对照。生产仍按此前恢复的`7991ba4`
记录保留，当前健康状态未在本轮重查。源码快照测试使用固定上游缓存，
不依赖本地报告；原件/候选模块及63项运行包仅在构建机校验归档，无永久
公开附件。

## 11-26 CAPTURE 转换失败的结束边界

驱动源码`dcbc890`修正帧池交付失败路径：图片转换失败仍返回ERROR和两平面
零payload，但保留时间戳、序号、flags/timecode/field、source-change序号重置
及最终EOS标记。此前提前返回会丢失失败图片上的LAST/EOS，可能让客户端
等待结束边界。EOS继续在LAST出队的buf_finish发布，不在转换完成时提前发。

实际交付/空结束/出队函数的ASan/UBSan回归覆盖24种组合：原尺寸/缩放、
转换成功/失败、普通图片/source-change LAST/最终EOS，以及空FLUSH/LAST/
RESTART；检查旧几何交付、零payload、元数据、一次EOS及新片段重新协商。
旧实现回归失败；撤去失败图片的LAST、EOS或序号重置，三项故障注入均被
检出。139项Host与独立提交源码包测试、严格checkpatch、原5.15.79 ARM
外部/additive构建、补丁零fuzz/源码一致性通过。候选decoder SHA256：
`7c97240a0b73386d93a94835a2e1347c1385d3d080f52f01248332f1dd2b05c8`。

本轮只在工作机验证，未连接/部署实机，继续暂停新硬件实验；未重查在役
`7991ba4`健康。转换桩不证明真实像素、DMA或缩放质量，也不关闭历史H.264
299/300、MPEG-4/WMV3尾帧及containerd/SFTP/FFmpeg异常。硬件缩放、音频/HLS
与1.3×实时仍未完成。记录标识`capture-boundary-errors-dcbc890`；源码包、
模块和报告仅在构建机工作区外SHA256校验归档，无永久公开附件。

## 11-26 decoder CAPTURE 同步错误传播

驱动源码`e4d73b1`收紧显式GE2D的回退边界：DMA-BUF的begin/end CPU访问、
映射及缓冲校验错误直接返回，不再触发一次CPU重试并把同步失败计为成功。
映射在begin成功后取得，映射失败仍配对end；GE2D只在两平面的CPU访问都
结束后启动。MJPEG、暂存面分配失败和引擎故障仍允许原尺寸CPU回退；缩放
输出继续禁止该回退。GE2D成功计数只在实际硬件转换调用成功后增加。

旧实现遇一次性end错误后错误返回成功，新增断言可复现。实际函数Host回归
覆盖原尺寸/缩放、两平面各自的短暂begin/end错误与空映射共12种组合，检查
配对、原样错误、无转换/成功计数；另保留MMAP暂存复用、MJPEG/超时回退和
CPU失败后的全部访问清理。恢复通用回退、提前映射、漏掉映射失败的end三项
故障注入均被检出。139项Host与独立提交源码包测试、ASan/UBSan、严格
checkpatch、原5.15.79 ARM外部/additive构建和补丁零fuzz/源码一致性通过。
候选decoder SHA256：
`cb3fc681c359b26fa1c048d887db0a367a9e73a67e391396e54e48150de0eb59`。

仅工作机验证，未连接/安装实机；在役`7991ba4`健康未重查，新硬件实验继续
暂停。Host exporter/引擎替身不证明真实cache/fence、像素或吞吐，也不关闭
历史运行时异常及尾帧问题。记录标识`decoder-capture-sync-e4d73b1`；源码包、
模块/运行包和报告仅在构建机工作区外SHA256校验归档，无永久公开附件。

## 11-26 GE2D 导入缓冲 fence

驱动源码`4e426d6`为独立GE2D M2M补入处理前的DMA-BUF fence等待。OUTPUT
等exclusive写者，CAPTURE等全部读写者；每平面最多1 s、可中断，不持有
硬件锁。超时/中断不运行blit，两个缓冲返回ERROR、输出两平面payload为零，
任务正常结束。MMAP不访问导入状态；同一import每次重排队都重新等待。
原5.15.79的vb2映射/prepare只处理映射及缓存，不代替fence等待。完成与
所有权仍依靠QBUF/DQBUF交接，未新增completion fence，不宣称完整隐式同步。

旧worker在未完成fence时仍启动blit，实际函数断言可复现；修正后覆盖四个
平面的超时/中断、输入shared读者不阻塞、输出shared读者阻塞、三次重排队、
四种MMAP/导入组合及单平面错误原样返回。跳过输入等待、跳过输出等待、把
输出错设为只等写者三项故障注入均被检出。139项Host/独立提交源码包测试、
ASan/UBSan、严格checkpatch、原5.15.79 ARM外部/additive构建及补丁零fuzz/
源码一致性通过。外部与additive GE2D模块SHA256相同：
`f5c42f654ad737b2bf4c3e5bf9525b225e29a5439c684b089c15f992648802c3`。

本轮仅工作机验证，未连接/加载/部署实机；在役`7991ba4`健康未重查，生产
GE2D继续关闭，新硬件实验仍暂停。fence替身不证明真实导出器、cache/DMA
内容、取消时延或吞吐，也不关闭历史运行时异常。记录标识
`ge2d-buffer-fences-4e426d6`；源码/模块和完整报告仅在构建机工作区外SHA256
校验归档，无永久公开附件，硬件缩放、音频/HLS及1.3×实时目标仍未完成。

## 11-26 音视频 HLS 验证入口

源码`d972b87`为阶段测量补入可选音频样本时长、原始逐幅时间线及首尾音画
偏差检查；未选择音频检查时仍只验证视频，报告明确标记范围。独立视频
解码增加xerror。官方7.0.2 ARM工具只新增标准HLS demuxer配置，4,662个
源码文件与校验过的官方归档逐字节一致，BUILD_VARIANT为upstream。

主机6.1.1及新ARM7.0.2工具各自完整读取两段本地HLS：24幅视频均与输入
NV12参考一致，AAC共97,280样本/48 kHz；初始复制音轨的首尾音画偏差约
21.3/5.3 ms，在声明50 ms容差内。ARM工具另完成AAC重编码/HLS复用，并由
两套软件工具复核。五类真实软件失败（缺音频、短音频、错位、断口、缺段）
在两套验证器中均拒绝；142项Host/独立源码包回归通过。新工具SHA256：
ffmpeg `da3cda8f9a66429f19f010e44d1564e502b06b91e1c6cba8180500b16f07db3b`；
ffprobe `04b32c7aecfb43a78dad075ccc95c7a4d19bc70fa52d19c94728fc2d43e7736b`。

本轮没有修改驱动或连接/部署实机；软件HLS与主机速度不构成硬件音视频、
音频波形质量、网络/加密/直播、完整OpenList或1.3×吞吐验收，新硬件实验
仍暂停。在役包及其当前健康未重查。记录标识`av-timeline-d972b87`；源码、
工具、素材与报告仅在构建机工作区外SHA256校验归档，无永久公开附件。

## 11-26 存储余量与被动取证复核

以工作机源码`5c81e36`复核原5.15.79-meson设备；在役包仍为`7991ba4`，
没有替换模块、固件或FFmpeg工具，也未加载GE2D或启动编解码任务。12:04–12:34 UTC
状态核对中containerd启动时间仍为08:09 UTC、累计NRestarts为43；该
计数包含此前故障，不作为新增43次崩溃。当前containerd SHA256与此前独立
校验的软件包一致，不能倒推历史瞬时差异的原因。

根分区首次普通可用空间约195 MiB。46份旧封存journal共436,207,616字节先
传到工作机，逐文件校验SHA256，独立归档副本再次校验，再在设备复核同一
清单、尺寸/mtime/inode后只删除匹配副本。删除前后普通可用空间约187→603 MiB。
活动journal与最新封存现场保留，业务数据、在役包及回退资料未清理。
当前包校验、两个video节点、五容器、四项HTTP200、pool_bytes=0、GE2D关闭
及26个启动文件哈希通过；同一boot与服务启动/重启计数保持。

既有`observe-network.py`按原源码哈希启动一次六小时限时被动采样，每10秒
记录，两个轮转日志总上限8 MiB，目录0700/文件0600。配置低优先级、空闲
I/O及10%单核CPU预算，核对unit active和连续新样本；没有设置开机任务或
自动恢复。此时仅确认采样已运行，不是六小时稳定性验收。日志不包含串口/
独立内核崩溃通道，pstore仍为空，缺失日志不能据此认定或排除内核故障。

记录标识`runtime-readiness-5c81e36`；原journal、清理清单、状态与采样原件
仅在构建机工作区外私有目录SHA256校验归档，无永久公开附件。此次增加存储
余量与被动证据，不关闭containerd/FFmpeg/SFTP异常、历史尾帧或网络故障；
新硬件实验继续暂停，缩放、完整音视频/HLS、1.3×及厂商同机对照仍待验收。

## 11-26 main 20220a8 部署验收

按用户要求将最新main完整运行包`20220a824de9eb501befabeed6f5957074d67584`
安装并保留在原5.15.79-meson/64 MiB CMA设备，默认NEON/GE2D关闭。142项
主机回归、原ARM外部/additive双构建、67项驱动输入一致性及该提交Host/
ARM-package CI通过。原`7991ba4`完整包、state与固件87项先备份到设备外
逐项校验，再走标准卸载/安装和新持久state；临时暂停OpenList后恢复，
未替换内核/boot DT、调整CMA或修改应用镜像。首次传输超时保留部分包，
安装前校验拒绝；续传完整哈希通过后才实际切换，没有把失败传输计为安装。

新decoder SHA256 `cb3fc681c359b26fa1c048d887db0a367a9e73a67e391396e54e48150de0eb59`，
encoder `2a55b4105162fb2a61f785bc07877fc573c6353c08402eeb8ef9127c2459f625`；
runtime及默认Overlay字节不变。新包独立官方7.0.2工具为upstream，哈希见
[工具记录](#11-26-音视频-hls-验证入口)，不替换宿主/容器FFmpeg。

原生16组600/600可见NV12逐帧精确，含H.2641080p300幅、HEVC1080p、crop/
裁剪切换、tiles、DPB16、CRA/开放GOP、自然EOS、最小池和慢消费。三组MJPEG
9幅全平面质量及两种提前关闭顺序通过。编码22组185幅完整软解/顺序和
Y/U/V各平面PSNR均大于30 dB，覆盖11种支持布局、1080p、GOP/强制IDR、
slice、同fd重开、VBR、SAR、JPEG及连续NV12/NV21/YUV420和lima导入；BGR24
按既有接口拒绝。该有限矩阵不证明所有exporter/fence或任意并发。

官方7.0.2默认NEON，完整1080p60幅直接硬解→硬编约2.77 s（0.72×），CPU缩
720p再编码约7.31 s，两条均60/60且全YUV质量通过，未达1.3×。独立320×240
50幅/2秒输入另加AAC重编码与本地MPEG-TS HLS：视频50/50、各平面质量通过，
音频97,280样本/48 kHz，首尾偏差约21.3/5.3 ms；音频波形质量、完整OpenList
原命令/镜像8.1.3及1080p音视频链未新增验收，不能把小尺寸速度扩大为实时结论。

13:19 UTC安装后完整包/固件、unit enabled/active、两节点、五容器/四项
HTTP200、pool_bytes=0、GE2D关闭与26个启动文件哈希通过；containerd保持
原启动时间/累计43次重启，二进制匹配既有验证包。新state已设备外逐文件
校验，保护性回退timer在内容验收后取消，保留回退入口但未演练整包降级。
没有重启或生产持续负载，历史运行时异常、尾帧与网络根因未关闭；GE2D
缩放候选仍未实机，后续实验须逐项满足取证条件。记录标识
`deploy-main-20220a8`；新旧源码/运行包、state、输出与报告仅在构建机工作区外
SHA256校验归档，无永久公开附件。

## 11-26 单平面 NV12 DMA-BUF 直接读取

候选源码`ca0d320`为标准单内存平面NV12补入编码器直接读取；已有NV12M/
NV21M保持。先等待生产者fence并同步映射，连续/8字节对齐、stride匹配且
高度为16行倍数时使用`base + stride * height`定位UV，完整范围不得超过
32-bit DMA。其他布局、MMAP或分散映射继续CPU复制，短payload与同步错误
不启动图片；staging仍分配供回退。验证客户端新增`NV12-1`选择真实单平面。

实际H264/JPEG输入函数回归修复前无法走直接分支，修复后覆盖720p、854×480、
1088高、1080/396等非整宏块行、stride不符、空/未对齐/跨32-bit地址、短输入
和fence错误。直接时staging哨兵逐字节未改变；撤掉高度/地址尾界检查或改错
UV偏移三项故障注入均失败。143项Host/独立提交源码包检查、ASan/UBSan、
strict checkpatch、原5.15.79 ARM外部/additive构建及补丁零fuzz/67项源一致性
通过，两个入口encoder SHA256均为
`3cbf0b9bb6d90b02b3c0d020cd04efd9c4a471371b622e38b543cf149b70c27b`。

只临时更换encoder模块，decoder/runtime/Overlay保留在役版本，GE2D不加载。
15组102幅覆盖单平面MMAP/dumb、720p、crop宽度、非整宏块高与1080p回退、
lima分散导入、同fd重开、JPEG及原NV12M；另8组64幅重复MMAP/原直接路径
作对照。全部图片完整软解、帧序及Y/U/V每平面PSNR>30 dB。新单平面直接
累计51幅copied=0，原NV12M直接24幅；91幅按预期复制，直接分支copy_ns=0。

首次主机逐字节位流对照在720p/crop失败，不能当作通过。后续相同MMAP路径
的两次输出与原NV12M直接路径也有差异；重复MMAP软解差异最大8/9级，
新/旧直接对照仍通过全平面质量，但原因未定位。不承诺严格位流确定性，
完整输出与原失败断言均保留。没有以修改驱动或降低质量门槛掩盖该差异。

候选测完恢复在役`20220a8`编码器、OpenList与五容器/四项HTTP200；包校验、
26个启动文件、boot ID、containerd启动/累计重启计数及二进制不变，帧池归零、
GE2D关闭，临时恢复timer已取消。未重启、修改CMA、正式安装候选或测完整GE2D/
官方工具少拷贝转码链，不继承原版FFmpeg的速度结论，1.3×仍未完成。
记录标识`nv12-dmabuf-direct-ca0d320`；源码/模块、输入客户端、输出/失败和
恢复报告仅在构建机工作区外SHA256校验归档，无永久公开附件。

## 11-26 GE2D 到编码器共享 DMA-BUF 交接

按用户“每步完成部署最新”要求，先将已验证单平面NV12直接读取正式安装
保留为`6925c3f`。新旧完整包、固件与state87项设备外逐文件SHA256核对；
两个102图安装矩阵的内容/顺序/全平面质量与直接/复制计数通过，H.26450/
HEVC10冒烟通过。保护窗曾在等待处理时触发恢复旧包，首次增量包重建又因
SOURCE_COMMIT遗漏被清单校验拒绝；这些失败均保留。最终同一已验二进制
重新安装并确认业务后及时取消timer，未把中间安装状态写成最终保留。
重复编码仍可能有少量位流差异，完整内容/质量门槛保留，不声称严格确定性。

新增标准V4L2验证入口源码`f053ed2`，客户端`encode-test -G GE2D_DEVICE`
仅接受dumb导入的NV12-1。CPU生成GE2D的MMAP源，GE2D CAPTURE导入编码器
OUTPUT使用的同一DMA-BUF；等待GE2D两队列出队后才编码，编码器归还输入
后再生成下一幅，共享目的面不经CPU重新填充或memcpy。显式核对双方stride/
容量、完成payload/时间戳与ERROR，拒绝不匹配格式/导入，等待有界，结束
停两队列再释放映射。该序列不发布completion fence，不允许其他设备抢用。

145项Host及独立提交源码包检查通过，真实客户端的ASan/UBSan替身覆盖同fd
交接、GE2D完成先于encoder QBUF、共享面不被CPU写入、格式不符、ERROR、
短payload、错时戳、POLLERR及持续EAGAIN终止。跳过完成、错共享fd、接受
短payload三项故障注入均被检出。ARM静态客户端构建通过，本增量没有修改
任何驱动/微码，模块与此前原ARM双构建/实机验收字节一致。

实机临时加载独立GE2D，保持decoder默认NEON，encoder使用新在役二进制。
7组47幅涵盖320×240、1280×720、854×480、704×396、1080p、同fd重开和JPEG；
独立工作机完整软解/顺序及每平面PSNR>30 dB。35幅encoder直接读取、copied=0/
copy_ns=0，12幅396/1080高非整宏块布局按预期复制，完整原件保留。只做
GE2D同尺寸复制→编码交接，未加入decoder、缩放、完整音视频或应用原命令。

临时GE2D、Overlay与guard均清除，恢复最新默认NEON，包/固件、26个启动
文件、帧池归零、unit enabled/active、原五容器及另一个无关容器保留，四项
HTTP200与containerd启动/累计重启数核对通过。OpenList恢复曾超出首次短
HTTP等待窗口，后续读取为200，保留检查失败，未新增containerd重启。

随后将`f053ed2`完整运行包及对应ARM客户端部署保留；核对所有模块/固件
逐字节不变后分阶段替换包，避免为客户端/文档更新再次卸载内核模块或重启
业务，前包另存，安装state未改变。encoder仍为SHA256
`3cbf0b9bb6d90b02b3c0d020cd04efd9c4a471371b622e38b543cf149b70c27b`；
decoder/runtime/默认Overlay和原版FFmpeg字节不变。不启用生产GE2D、未改
boot DT/CMA或重启设备，不证明1.3×、零拷贝全链或历史运行时异常已关闭。
记录标识`shared-ge2d-encoder-f053ed2`；原件、前后包/state、源码、工具和
失败/恢复记录仅在工作区外私有目录SHA256校验归档，无永久公开附件。

## 11-26 GE2D 缩放后共享编码输入

客户端源码`926d0c05721ccdbb20b5c8a7070d40ac823899ec`新增`-S WxH`：CPU
生成该尺寸的GE2D源，GE2D缩小后写入encoder导入的同一NV12 DMA-BUF，位置
参数仍为编码输出尺寸。每轴只允许1～4倍缩小，拒绝奇数、越界和非法参数；
REF保存源尺寸，供工作机独立缩放参考。共享目的面仍不经CPU重写，交接遵循
此前两引擎的QBUF/DQBUF所有权顺序，不发布completion fence。

146项Host及独立提交源码包检查、C ASan/UBSan和ARM静态客户端构建通过。
真实客户端替身同时验证原尺寸与128×64→64×32源布局/像素、恢复输出几何、
目的面不被CPU写入和既有六种错误终止；错源几何、错误恢复输出几何、放宽
源尺寸上限三项行为故障注入均检出。此步没有修改驱动或微码。

原版5.15.79-meson/S805实机临时加载既有独立GE2D与匹配Overlay，decoder保持
NEON。9组47幅覆盖640×384→320×192/480×288、1080p→720p、854×480→640×288/
640×360/426×240、1280×768→320×192四倍边界、同fd编码队列重开及JPEG。
工作机用官方FFmpeg独立bilinear缩放源，再完整软解全部输出，47/47幅与顺序
通过，各组Y/U/V平面PSNR均>30 dB，最低30.36 dB；一组设备源REF与工作机源
逐字节一致。41幅encoder直接读取且copied/copy_ns均为0，360行高6幅按预期
复制。该质量比较包含缩放与有损编码，不要求位流或滤波器逐字节相同。

临时GE2D/Overlay与恢复timer清除后，原五业务容器、四项HTTP200、unit
enabled/active、26个启动文件、原state及containerd字节/启动/累计重启数
核对通过，帧池归零。随后完整运行包与匹配客户端正式更新保留为`926d0c0`；
更新前验证模块/固件逐字节不变，包刷新没有再次卸载模块或重启业务，前包
留作回退。encoder/runtime/decoder与上节哈希相同；独立GE2D SHA256
`f5c42f654ad737b2bf4c3e5bf9525b225e29a5439c684b089c15f992648802c3`，客户端
SHA256 `3f50da2303ac6fe476e66998a96ca773ceca2eea74ae48abba53fa3fa9cada6a`。

正式环境仍默认NEON、GE2D关闭，官方FFmpeg字节不变；未接入decoder、完整
音视频/应用链、跨设备并发复用或长时压力，不宣称端到端零拷贝、1.3×或历史
运行时异常已修复。记录标识`shared-ge2d-scale-926d0c0`；源包、客户端、
原始媒体/报告及前后包/state仅在工作区外SHA256校验归档，无永久公开附件。

## 11-26 真实解码缩放与共享编码交接

客户端源码`a6f3fc8cb96cbe75d704b72c440d69629b48bbe3`扩展现有stateful解码
验证入口：`ENCODE_DEVICE`/`ENCODE_STREAM`把CAPTURE导入的两个NV12M DMA-BUF
直接交给H264编码器，等编码器CAPTURE和OUTPUT都返回后才复用decoder面。
比较双方尺寸、stride、容量和payload，检查错误、时间戳与有界出队；尺寸
切换排空并关闭consumer后才释放旧面，提前退出先停止consumer。只有显式
dump/hash才在用户态同步并读取采集像素，没有CPU重新填充共享目的面。
未新增completion fence，应用仍须串行保持上述所有权。

148项Host及独立提交源码包检查通过，ARM静态客户端和ASan/UBSan替身覆盖
同fd交接、目的面不被改写、两引擎完成顺序、drain/释放及十类错误。错共享
fd、跳过输入出队和接受短payload三项故障注入均检出。原5.15.79 ARM外部/
additive双入口与补丁一致性复核通过；此最终增量没有改变任何内核/微码字节。

原版5.15.79-meson/S805/64 MiB CMA上临时启用decoder GE2D，主机生成六份
有运动纹理的H264/HEVC样片，各12/12/8幅（640×384、854×480、1080p），
独立软件解码后再bilinear缩放作参考。先两组24幅缩放冒烟与两组24幅完整
交接通过，再完成24组272幅独立decoder矩阵：MMAP及meson-drm连续DMA-BUF、
原尺寸、640→480/160、854→640×288/426×240、1080p→720p。完整帧序与各组
Y/U/V PSNR>30 dB（最低30.07），MMAP/导入原始字节相同，结束帧池归零。
首次导入脚本误选lima card，dumb创建在分配阶段失败；改按实际驱动名选择
meson-drm后完成矩阵，失败及恢复记录保留。

完整共享转码矩阵20组220幅均出队，encoder direct=220、copied/copy_ns=0。
其中18组172幅静态尺寸及两种提前STREAMOFF顺序完整软解、帧序、全平面
质量通过，最低PSNR30.10 dB。另两组尺寸切换共48幅未通过内容/顺序门槛：
640×384→854×480切换后均缩到320×192，第二段出现采样对齐差异。补充两份
独立854→320静态流与切换流的decoder原始输出逐字节相同；因此不能把最近
软件帧索引的偏移直接判为固件重排，也不能把完整帧数当作正确内容。

为检验采样中心假设曾临时测试相位候选（GE2D SHA256
`9bfa7326b17de6b420804cd2eef339065e2194cb8b6c2e2c8aa867355bc76b32`），
Host及双构建通过但真实854→320质量/顺序仍失败，Y PSNR约28.11 dB。
该候选已撤回，源码/additive补丁恢复；最终GE2D恢复原SHA256
`f5c42f654ad737b2bf4c3e5bf9525b225e29a5439c684b089c15f992648802c3`。
失败输出和参考保留，不降低门槛或声称任意比例/切换质量通过。

启动前发现四核load约9、CPU采样约97%、温度约71°C，保存进程现场后按
测试流程暂时停OpenList，CPU降至约20%。CrashCore确认是既有代理程序。
每组前5 s采样CPU、可用内存及温度，过载停止提交新任务并轮询；必要硬件
任务串行且有超时/恢复timer。各批结束均恢复原NEON、OpenList及五业务，
四项HTTP200、26启动文件/原state、containerd字节/启动/累计重启数不变，
GE2D卸载、帧池归零；未新增系统重启或持续压力测试。

完整运行包和匹配客户端已更新保留为`a6f3fc8`；刷新前验证模块/固件逐字节
不变，包更新无须再次卸载模块或重启业务，原包另存。客户端SHA256
`e65140feb5bc7eb1e93c8a265b3b89bc9f9c0ccdc3a68c9e87a6200a755feb1c`。
正式GE2D仍关闭，官方FFmpeg字节不变；标准诊断链通过子集不证明官方工具/
OpenList、音视频、1.3×、并发或长期稳定性，#14/#32保持开放。记录标识
`decoder-shared-a6f3fc8`；原件、失败候选、软件参考与包/state在工作区外
SHA256校验归档，无永久公开附件。

## 11-26 GE2D 滤波首样本重复修复

继`a6f3fc8`的854×480→320×192失败，固定S805厂商
[`stretchblt.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d/stretchblt.c)
设置`hsc_rpt_p0_num`/`vsc_rpt_l0_num`各1，
[`ge2d_hw.c`](https://github.com/hardkernel/linux/blob/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d/ge2d_hw.c)
将其写入INI_CTRL高位；当前移植遗漏，两个寄存器均写0。四抽头滤波读取
起点因此偏移，运动纹理的空间偏移还会干扰最近软件帧的顺序判据。
源码`2fea36e2474d709fb205b48a5f23edb6d08df1ae`仅在缩小的对应轴写首样本
重复1，保持相位步长、原尺寸/裁剪、颜色和字节序路径。此前只加中心相位
的失败候选不再采用，原始失败及对照继续保留。

148项Host和独立提交源码包、ASan/UBSan、strict checkpatch、原5.15.79 ARM
外部/additive双入口与补丁一致性通过。真实MMIO回归覆盖横纵缩小、只缩一轴、
unity/crop和既有错误清理；分别删去横/纵首样本重复的两项故障注入均检出。
GE2D SHA256 `b838485a571d1f302077f7cb89e20dd996967b5a4558ec66269b9eeb3f480b1b`。

实机先对原失败静态/切换四组72幅完整软解、顺序及独立bilinear参考验收，
854→320静态H264/HEVC输出Y PSNR约36.34/36.38 dB。随后24组272幅独立decoder
重验全部通过，最低组/平面PSNR34.70 dB，MMAP与meson-drm DMA-BUF原始字节
相同。22组244幅真实解码→GE2D→共享编码输入重验也全部通过，最低平面
PSNR34.88 dB，含原尺寸、四倍/非等比/crop、1080p→720p、两种输入的源尺寸
切换及两种提前STREAMOFF顺序。244幅encoder direct、copied/copy_ns=0；
提前取消可能让decoder额外完成未交给encoder的图片，不将其算成已编码输出。

各组前继续采样CPU/可用内存/温度，任务串行、有界并带恢复timer。一次
OpenList启动超过首次HTTP等待窗，停止新硬件工作后轮询恢复四项HTTP200，
原五业务、containerd字节/启动/累计重启数正常，非新崩溃。各批结束卸载
GE2D/临时Overlay、恢复NEON，原state、26启动文件及固件完整校验通过。

完整运行包已更新保留为`2fea36e`，同时在部署目录单独保存上述可选GE2D
二进制与来源/哈希；默认包仍只自动加载原三个模块，不改其字节/固件或
FFmpeg，不启用生产GE2D。旧包/state和修复前模块另存，更新不再次重启业务。
此修复关闭的是本轮采样偏移，#14/#32仍需完整回压、同步/超时和客户端/
应用收益门槛；未证明任意格式/比例、并发、1.3×或长期稳定性。记录标识
`ge2d-repeat-2fea36e`；源码、输入/软件参考、输出、失败、完整包/state与
服务恢复原件仅在工作区外SHA256校验归档，无永久公开附件。

## 11-26 六十秒输入诊断与存储恢复

运行包`2fea36e`、原版5.15.79-meson/64 MiB CMA，官方未修改7.0.2工具，默认
NEON/GE2D关闭。工作机固定运动纹理的1080p30 H264/HEVC输入各60 s、1800幅，
分别独立软解得到完整参考。H264直接转码CAPTURE4/encoder OUTPUT4退出0，
但decoder converted=1441、encoder copied=1437，工作机完整软解只得1437幅。
CAPTURE12/OUTPUT2重复仍仅1448/1436，完整软解1436幅。两份已输出部分逐幅
全YUV与输入连续前缀对齐，最低帧/平面PSNR38.84 dB；未看到中间丢图/乱序，
仍不能据此前缀结果证明缺失尾部原因。日志出现官方包装器
“All capture buffers returned to userspace”告警；不能从约55.29/54.32 s计算
有效吞吐，也未因此把历史299/300归为同一问题。HEVC性能组尚未执行。

直接V4L2客户端在相同H264输入交付1800幅、帧池归零，约45.18 s；此组只
记录交付计数，没有导出像素，不作为正确内容或吞吐验收。随后已验证的
GE2D→共享NV12M→编码客户端在完整1080p输入上出队1800幅，GE2D/encoder
direct各1800、copied/copy_ns=0，约53.25 s、子进程CPU约2.16 s，转换累计
约42.92 s。原始编码输出仍待工作机完整软解/质量/顺序验证，不能声明有效
全链吞吐；仅从当前耗时也未达到约46 s门槛，不继承到官方工具/OpenList。

首次恢复OpenList失败明确为Docker创建目录ENOSPC：SSD普通可用空间0，
containerd active、累计重启43和启动时间未变，没有新SIGSEGV/OOM证据。
实际containerd/dockerd/FFmpeg字节已另存，连续读取哈希一致；原失败媒体、
内核/journal与容器状态设备外保存。停止期间日志先完整归档校验再清空后
OpenList恢复；随后只回收逐文件匹配外部旧归档的镜像/源包和实验副本。
另将64份未使用的旧实验文件约285.64 MB逐文件复制到系统分区暂存，源与
副本SHA256及落盘核对后迁出SSD，保留原件等待设备外归档。SSD普通余量
恢复到约337 MB，系统分区清理已归档副本后约431 MB；未调整保留空间比例，
不清理业务下载/音乐、容器卷、当前安装或回退资料。各次操作后原五业务、
四项HTTP200、原state/26启动文件及模块/固件核对通过。

最后一次原生转码记录恢复命令成功，随后Tailscale SSH认证与exec请求仍
成功，但`true`/`uname`/Python均以1退出且无输出，PTY/SFTP也失败；只读TCP
转发诊断返回`ssh-monitor: RPC channel closed`。Tailnet ping及外部四项
HTTP仍正常。已停止新的硬件工作并保存协议现场，当前不能独立重读设备
状态或取回上述1800幅媒体；不据此断言整机卡死、DMA损坏或已定位#34根因。
记录标识`long-input-storage-2fea36e`，原件与失败仅在工作区外私有目录按
SHA256归档，无永久公开附件。尚未设备外转存的暂存原件继续保留在设备上。

## 11-26 慢消费与官方 H263 工具候选

`3817a3a`在现有解码客户端增加严格0～1000 ms的`CAPTURE_DELAY_MS`，仅延迟
非空图片的归还；ASan/UBSan实际函数检查解析、非法输入不污染状态、零延迟
和非空图片停留，两项故障注入检出缺失停留与放宽上限。ARM静态构建通过。

`b97a0b3`仅为独立官方7.0.2工具启用已有H263 demux/mux/parser、软件及V4L2
decoder，保持源码原版。实际组件列表/构建配置通过，工作机ARM仿真完整软解
352×288 baseline、704×396 PLUS与1280×720 PLUS各12幅，全部NV12像素与主机
原生软件参考逐字节相同。FFmpeg SHA256
`124efdcbccdd472f4ec7552ba479c5c012fbddda0dde043822977315f836e151`；ffprobe
`270c5682c713c9037404405c670ca9e02c63040351fdac6b0eade70d4ccc3a2c`。

`3c55bda`补齐运行包中遗漏的SAR bug补丁输入，与已有drain/input-error补丁
一同保留，清单覆盖64项；正常打包仍是未修改的upstream工具。既有可选
`--bugfixes`版本另存供诊断，没有进入生产或正常打包入口。

149项Host及独立提交源码包检查通过，候选尚未安装或实机验收；两槽慢消费、固件失败
恢复及HD内容门槛仍保留，不能用ARM软件解码替代S805硬解。远程执行恢复后
再开展有界验收，生产来源仍为上节`2fea36e`，官方旧工具字节未替换。

## 11-26 慢消费、H263 与输入 PTS 实机增量

恢复远程访问后，原2fea36e驱动与已验GE2D完成两槽、每幅延迟50 ms的慢消费
矩阵，H264/HEVC小尺寸、1080p→720p及源尺寸切换6组88幅，完整软解/顺序及
全YUV质量通过，最低组/平面PSNR37.27 dB，encoder direct=88、copied/copy_ns=0。

H263固件缺失与32字节截断分别明确失败ENOENT/EINVAL，每次恢复原固件后
baseline12幅内容正确。原生baseline/PLUS/720p各12、32×32/1080p/1916×1080
各6幅，加两次恢复及官方未修改H263工具baseline/PLUS/720p各12幅，共11组
114幅全内容/顺序通过，平面最大像素差3。原固件哈希、原NEON包/state、
26启动文件、业务与HTTP恢复核对通过。此部分闭环缺失/截断固件及已测HD，
更多畸形/GOB/容量组合仍待覆盖，#16保持开放。

完整1800幅原生转码的工作机检查发现最后两幅互换。现有直接客户端给所有
H264/HEVC输入零PTS，而驱动EOS批次依靠真实PTS排序。`d6c7aa7`加入显式
`INPUT_PTS`：每AU一项非负微秒、保持码流解码顺序传入，严格检查数值、长度、
额外/缺失项及32-bit timeval范围。参考PTS来自独立软解帧的pkt_pos与呈现
序，1800项边界逐项匹配真实客户端分帧和官方parser，没有事后排序输出。
150项Host检查、ASan/UBSan和两项行为故障注入通过。

取原失败尾部60幅完整IDR GOP做相同硬件A/B，零PTS稳定复现58/59互换，
仅提供正确输入PTS后全部内容/顺序及每平面PSNR>30 dB通过，无驱动修改。
这定位本次验证客户端输入缺口，不能据此关闭历史299/300或认定驱动任意
码流都正确。随后的带PTS完整1800幅重复遇到下节根分区只读失败，未取得
有效完成/像素/性能结果。上述客户端和H263工具候选尚未更新生产运行包，
设备仍保留2fea36e，硬件工作已暂停；通过/失败原件均私有外部SHA256归档。

## 11-26 根分区只读与缓存字节差异现场

21:59:24 UTC长输入检查期间，系统分区出现错误，候选脚本写results/dmesg
均报EROFS，且报告写失败阻断原finally恢复步骤；不把保护timer当作已恢复。
Tailscale SSH随后认证/exec成功但空退出，现有ttyd备用终端确认根分区
`/dev/mmcblk1p2` ext4以ro挂载；superblock first error为
`ext4_validate_block_bitmap`，last为`ext4_lookup` inode205 checksum invalid，
已记16次错误。没有足够证据区分存储、内存、供电或具体DMA原因。

在只读分区上不删除临时文件，先核对三个已部署模块/默认Overlay哈希，
手动卸载临时GE2D并恢复默认NEON。Docker失效时现有OpenList进程仍在其
原cgroup freezer，核对PID/进程及cgroup后只将该组FROZEN改THAWED，外部
四项HTTP200恢复；未重启网络、设备或终止其他容器shim。

本轮首次保留实际差异字节：containerd/dockerd的普通缓存读与O_DIRECT读
分别相差65/95个4 KiB页，540006个字节，两种原始页均传工作机并逐页校验、
外部归档。O_DIRECT的整个文件哈希仍分别匹配健康记录`91d408a9…`/
`1545c4e2…`，普通读却不同；先前两次普通读也可能不一致。tailscaled普通/
直接读相同，但与先前健康快照不符，未替换或重启它。只能确认本次缓存/
直接读取差异与文件系统元数据错误，不能因此证明具体驱动根因。

差异保全后，仅对两个runtime文件执行POSIX_FADV_DONTNEED，未全机清缓存、
写二进制或强制根分区可写；普通读恢复健康哈希，containerd恢复active。
Docker标准启动仍失败，直接输出显示containerd缺少namespace服务。进一步
检查发现元数据bolt等插件因只读根分区无法正常初始化；不能把daemon进程
active当作功能恢复。全部硬件实验停止，不对挂载根分区做在线fsck、盲目
remount rw或无救援重启。
记录标识`readonly-native-pts-failure`；实际差异页、校验和、内核/服务与
恢复现场私有外部归档，无永久公开附件。#34继续开放，离线核验与完整恢复
须在可靠备份和救援条件下执行。

11-27 01:35 UTC保全并逐文件核对原containerd根目录10项、587148字节后，
将同一份数据复制到`/run`，再临时bind到原元数据路径；根ext4保持ro，
原元数据未写入。全部必需插件及moby namespace恢复，随后标准Docker启动
成功。守护进程重新接管时将旧容器判为退出，原五业务重新启动；Arcane
被旧docker-proxy端口占用阻挡，核对该代理的PID代际、父进程、完整参数和
Arcane原端口绑定后仅终止该遗留代理，Arcane恢复。02:01 UTC原五容器及
四项HTTP200、Docker/containerd active、原模块/固件哈希、GE2D关闭和帧池
归零均核对通过。codec为手动加载，unit仍inactive，未伪造active状态。

更新后的RAM元数据在containerd主进程短暂停止约0.25 s期间制作一致副本，
带20 s恢复保护，未暂停任何业务容器；新11项checkpoint已保存到SSD，并
在主机逐文件校验归档。该恢复是易失的临时措施，不修复ext4，不保证重启
后可用。用户确认没有串口、可控电源、备用S805或额外本地终端，因此
原厂系统切换、冷启动/异常重启与离线修复无法在当前条件下验收。继续
只读取证和必要服务轮询，生产硬件实验保持停止。

## 11-27 解码器 DMA-BUF 栅栏与 padding 清理

只读恢复后的 30 分钟主机侧被动轮询完成（30 轮四项 HTTP 200、6 轮 SSH
`uname -r` 正常），05:59 UTC 再次只读复核五容器、Docker/containerd
`NRestarts=0`、模块/固件哈希、GE2D 关闭与 `pool_bytes=0` 均保持。随后在
工作机推进 #14/#32 离线候选，未连接/修改或安装实机，生产继续保留恢复后
的 `2fea36e` 状态。

- **导入栅栏等待**：`vdec/meson8b-capture.c` 新增 `amvdec_capture_wait()`，
  在显式 GE2D 或 MJPEG CPU 回退写入导入 CAPTURE DMA-BUF 前等待所有读写者
  栅栏（`dma_resv_wait_timeout(..., true, true, 1 s)`）；`vdec/esparser.c`
  为导入 OUTPUT DMA-BUF 在准备与写操作前等待写者栅栏（`all=false, 1 s`）。
  超时或中断直接返回 `-ETIMEDOUT` / `-ERESTARTSYS` 并中止当前输出，不回退
  CPU 重试。
- **消除冗余 CPU 访问/清零**：导入 CAPTURE DMA-BUF 在可见宽高与对齐一致且
  无尾部的精确布局下跳过 `dma_buf_begin_cpu_access`、`vb2_plane_vaddr`
  （`dma_buf_vmap`）、`memset` 与 `dma_buf_end_cpu_access`；有行尾或尾部
  padding 时仅清理对应区域。MMAP 共享暂存面在 `amvdec_shadow` 记录最近已
  清零输出宽高，仅在首次分配或输出尺寸变化时清零，避免稳态逐帧重复清零。

真实函数 ASan/UBSan 回归覆盖 CAPTURE 双平面/缩放/MJPEG 栅栏超时与中断、
精确导入缓冲零 CPU 访问/无 `vmap` 通过、同容量尺寸缩小重清暂存面，以及
OUTPUT 导入栅栏超时/中断中止且不触发 ESPARSER；3 项故障注入（撤除 CAPTURE
栅栏等待、撤除暂存尺寸变化重清、撤除 OUTPUT 栅栏等待）均被检出。`make test`
全部 150 项主机测试与文档检查、严格 `checkpatch.pl` 零告警、5.15.79 ARM 完整
内核、外部与 additive 模块构建及补丁零 fuzz/源码一致性通过；`venc`/
`runtime`/`ge2d` 模块字节未变，外部与 additive `meson-vdec.ko` SHA256 均为
`b51d67379869d6fdfca135f05a7cabc45ea18d44c39b78d5fb05cfeb7c43a9ff`。

本轮仅工作机验证，未实机测试真实导出器栅栏、DMA 像素或吞吐；#14/#32 实机
验收与 #34/#4/#5/#6 等问题仍保持开放，待后续具备安全条件再验。

## 11-27 编码器 DMA-BUF 边界与 JPEG CMA 缩减

继续在工作机离线推进 #14（未连接、修改或安装实机，生产继续保留恢复后
的 `2fea36e` 状态与上表在役模块）：

- **JPEG 免除 H.264 专用 CMA**：`venc/venc.c` 的 `venc_alloc_buffers()`、`venc_buffspec_init()`
  与 `venc_hw_init()` 在 `V4L2_PIX_FMT_JPEG` 帧内编码会话跳过未使用的
  `VENC_REF0`、`VENC_REF1`、`VENC_ANALYSIS` 分配及参考 canvas 初始化，并在
  `venc_free_buffers()` 失败/释放路径一并清零各槽位 `paddr`/`size`。1080p
  JPEG 编码会话单路节省约 `7.06 MiB` CMA（720p 约 `3.70 MiB`）。
- **多平面 DMA-BUF 32 位与容量校验**：`venc/input.c` 的 `venc_in_map()` 增加
  空 `sg_table`、零基址及 `(u64)dma_addr + size <= U32_MAX + 1` 的 32 位 DMA
  范围校验；`venc/venc.c` 的 `venc_stage_input()` 对双平面 `NV12M`/`NV21M`
  直接通道补齐平面容量 `vb2_plane_size >= width * (rows / vsub)` 与 32 位
  上限检查，越界或容积不足回退安全 staging 路径。

工作机 ASan/UBSan 回归新增 `venc_in_map()` 空表/非连续/4 GiB 跨界检查、`NV12M`
双平面 4 GiB 边界及短容积回退用例，以及 JPEG 仅 3 项分配与部分失败全清理校验；
`make test` 全部 151 项主机测试与文档检查、`git diff` 严格 `checkpatch.pl`（0
errors/warnings/checks）、5.15.79 ARM 完整内核/外部/additive 模块构建与
[0001-meson8b-runtime-codecs.patch](../patches/0001-meson8b-runtime-codecs.patch)
一致性通过；外部与 additive `meson-venc.ko` SHA256 均为
`01c8adb5ccdfcb4d62a78b0abde91701a5bdb63f08273b76208880490de7efa4`，`vdec`/
`ge2d`/`runtime` 字节未变。本轮仍仅限工作机检查，#14/#32 实机与全链吞吐验收
保持开放。

## 11-27 同 fd 重开清零与 GE2D CREATE_BUFS

继续在工作机离线推进 #32/#14（未连接、修改或安装实机，生产继续保留恢复后
的 `2fea36e` 状态）：

- **解码器同 `fd` 重开清零缩放几何**：`vdec/vdec.c` 的 `vdec_start_streaming_locked()`
  从 `STATUS_STOPPED` 重建新会话时一并清零 `sess->capture`，防止同 `fd`
  经 `STREAMOFF`/`REQBUFS(0)` 后未重新 `S_FMT(OUTPUT)` 就启动同尺寸新流时
  沿用上一会话的缩放配置。
- **GE2D 补齐 `PREPARE_BUF`/`CREATE_BUFS`**：`ge2d/meson8b-ge2d.c` 的
  `ge2d_ioctl_ops` 接入 `v4l2_m2m_ioctl_prepare_buf` 与 `v4l2_m2m_ioctl_create_bufs`，
  与 `ge2d_queue_setup()` 的既有容量校验配合以支持按需扩展连续缓冲。

工作机 ASan/UBSan 回归新增 `vdec_start_streaming_locked()` 同 `fd` 重开清零
`sess->capture` 与 `ge2d_queue_setup()` 扩容接受用例；`git diff` 严格
`checkpatch.pl` 零告警，5.15.79 ARM 完整内核/外部/additive 模块构建与
[0001-meson8b-runtime-codecs.patch](../patches/0001-meson8b-runtime-codecs.patch)
一致性通过，外部与 additive 模块 SHA256 一致为 `meson-vdec.ko`
`1885d1b85e25f40fb15cc28f6205745d8b44ca3cae91822b704b7a562f85b7a5`、`meson8b-ge2d.ko`
`60973f85714e6e1a66033516072f2829905997d2c7ad4ae1d5e18a660fb5f1cf`，`venc`/
`runtime` 未变；最后运行 `make test` 覆盖全部 152 项主机与文档测试通过。
实机硬件重开、队列扩容与全链吞吐仍在 #14/#32 保持开放。

## 11-27 码控上限测量与 HEVC/H.263 切片边界

继续在工作机离线推进 #24、#15 与 #16（未连接、修改或安装实机，生产继续保留恢复后
的 `2fea36e` 状态）：

- **码控测量上限一致性**：`venc/rate-control.h` 提取 `venc_rc_p_ceiling()`
  供 `venc_rc_clamp_p()` 与 `venc_rc_measure()` 共享，并向 `venc_rc_measure()`
  （及 `venc/venc.c` 调用点）传入 `bool idr`：长 GOP 首个 IDR 使用 `qp_max`
  （而非 P 帧 `32`）作为饱和判据，且当 `qp_min > VENC_RC_QP_CEILING` 时与实际
  P-QP 限幅保持同一上限，消除误计饱和帧与误报风险。
- **HEVC 首 slice 依赖标志校验**：`vdec/codec_hevc.c` 的 `codec_hevc_process_rpm()`
  拒绝 `slice_segment_address == 0` 且 `dependent_slice_segment_flag != 0` 的
  非法输入，防止为无前驱独立 slice 的首包配置 `MPRED_CTRL0_NEW_PIC | MPRED_CTRL0_NEW_SLI_SEG`。
- **H.263 首行 `GN=0` GOB 头拒绝**：`vdec/h263-vlc.h` 的 `h263_translate()`
  要求检测到的 GOB 头号 `gob != 0`，拒绝首宏块行（`y == 0`）出现非法 `GN=0`
  GOB 头。

工作机 ASan/UBSan 回归扩展 [test_rate_control.c](../tests/test_rate_control.c)、
[test_rate_control_state.py](../tests/test_rate_control_state.py)、
[test_hevc_rpm.py](../tests/test_hevc_rpm.py)、
[test_hevc_tile_registers.py](../tests/test_hevc_tile_registers.py) 与
[test_h263_vlc.c](../tests/test_h263_vlc.c)（涵盖首个 `QP 36` IDR 不计饱和、
`qp_min = 45` 测量上限、首 slice `dependent_slice_segment_flag=1` 拒绝、跨 tile
混合 slice 标志及 `GN=0` GOB 拒绝）；`git diff -- venc/ vdec/` 严格
`checkpatch.pl` 零告警（0 errors/warnings/checks），5.15.79 ARM 完整内核/外部/
additive 模块构建与 [0001-meson8b-runtime-codecs.patch](../patches/0001-meson8b-runtime-codecs.patch)
一致性通过，外部与 additive 模块 SHA256 一致为 `meson-vdec.ko`
`8f7f07a0ad874947df99c6d569e7f6c71b7c05db79de7f8510e292bc2ffb05b7`、`meson-venc.ko`
`e6b1aa08d4c071f1b0507f9303dbca7f8d25bd3e6660a36133a02bba530e9595`，`runtime`/
`ge2d` 字节未变；最后运行 `make test` 覆盖全部 152 项主机与文档测试通过。实机
多 slice/HD/码控验收仍在 #15/#16/#24 保持开放。

## 11-27 停流缩放清零修复与阶段一/二实机验收

实机复现 `v4l2-accept` 同 `fd` 重开场景时发现：`vdec` 的 `OUTPUT` 队列设置
`min_buffers_needed = 1`，导致仅调用 `VIDIOC_STREAMON(OUTPUT)` 而尚未
`VIDIOC_QBUF(OUTPUT)` 时会延迟执行 `vdec_start_streaming_locked()`，从而使停流后
立即查询 `G_FMT(CAPTURE)` 或 `REQBUFS(CAPTURE)` 仍读到旧会话的缩放尺寸。遂在
`vdec/vdec.c` 的 `vdec_stop_streaming_locked()` 进入 `STATUS_STOPPED` 路径同步
清零 `sess->capture` 与 `sess->dpb_size`（同时保持 `STATUS_NEEDS_RESUME`
动态分辨率切换期间 `STREAMOFF(CAPTURE)` 的缩放几何不变），并在
[test_capture_scaling.py](../tests/test_capture_scaling.py) 增补停流清零与保留
测试。

- **工作机与双构建验证**：`make test` 全部 153 项主机测试（含 ASan/UBSan）与
  文档检查通过；`vdec/vdec.c` 变更通过严格 `checkpatch.pl`（0 告警）；5.15.79 ARM
  外部与 additive 模块构建及 [0001-meson8b-runtime-codecs.patch](../patches/0001-meson8b-runtime-codecs.patch)
  同步通过（`meson-vdec.ko` SHA256 `3b6308f0ce7ecb2fa57a2ebf1f8567dd2d9cfb638d55eb47f47431d79f8fba38`、
  `meson-venc.ko` `e6b1aa08d4c071f1b0507f9303dbca7f8d25bd3e6660a36133a02bba530e9595`、
  `meson8b-ge2d.ko` `60973f85714e6e1a66033516072f2829905997d2c7ad4ae1d5e18a660fb5f1cf`）。
- **Stage 2 实机验收（临时加载 `meson8b-ge2d.ko` + `capture_ge2d=1`）**：
  - `ge2d-copy` 640×360 同尺寸及 320×180 缩放各 5 帧通过；`GE2D`
    `VIDIOC_CREATE_BUFS`、过小缓冲 `EINVAL` 拒绝、`VIDIOC_PREPARE_BUF` 及对新创建
    `index=1` 缓冲实际执行转换全部通过。
  - `vdec` 同 `fd` 重开（H.264 640×360 先协商 `320×180` 解码，双队列 `STREAMOFF`
    + `REQBUFS(0)` 后不重设 `S_FMT(OUTPUT)` 仅 `STREAMON(OUTPUT)`）在 `QBUF` 前
    即刻 `G_FMT(CAPTURE)` 与二次 `SOURCE_CHANGE` 后均恢复为 `640×360`。
  - `vdec`（`capture_ge2d=1`）+ DRM dumb `CAPTURE_DMABUF` + 共享 `venc` 端到端
    转码：640×360 30/30（`GE2D=30, DIRECT=30, COPIED=0`）及缩放 320×180 30/30
    （`GE2D=30, DIRECT=30, COPIED=0`）通过；`HEVC` 2-slice 640×384 经
    `capture_ge2d=1` + DRM dumb `CAPTURE_DMABUF` 输出 20/20 与软件参考逐帧可见像素
    MD5 精确一致，结束后 `pool_bytes=0`。
  - `GE2D -> venc` 宏块对齐 320×192 `NV12-1` 直读 10/10（`DIRECT=10, COPIED=0`），
    非对齐 320×180 按预期回退安全复制（`COPIED=10`）；`NV21M` dumb DMA-BUF
    直读 10/10（`DIRECT=10, COPIED=0`），非连续 `lima` GEM DMA-BUF 回退安全复制
    5/5（`DIRECT=0, COPIED=5`）。
  - `JPEG` 1080p 5 帧（quality=85，无 `REF0`/`REF1`/`ANALYSIS` CMA）解码输出全平面
    平均 PSNR `30.73 dB`（`y:29.47 u:35.63 v:35.56`）；`RC` `qp-min=45`
    （`i-qp=48, p-qp=45`, 15 帧）报告 `qp=45`、`saturated_frames=14`（首帧 IDR
    不误计饱和）。
  - `H.263` 4CIF（704×576，12 帧，平均 PSNR `66.50 dB`）与 PLUS（164×100，3 帧，
    平均 PSNR `65.16 dB`）全平面接近软件参考；首行 `GN=0` 非法 GOB 头在实机安全
    拒绝并中止异常会话，后续会话可正常恢复解码。
- **未关闭边界**：默认 `ffmpeg h263p` 输出默认开启 Annex K Slice Structured
  （`SS=1`），仍按既有能力边界以 `-EOPNOTSUPP` 拒绝；HEVC 首 slice
  `dependent_slice_segment_flag=1` 仍仅工作机回归，尚无实机异常码流用例；1080p 1.3×
  实时目标（#7）及历史故障调查（#4/#5/#6/#34）继续开放。

## 11-27 生产部署升级与 Stage 1 默认 NEON 复验

完成上述 Stage 2 与宿主回归后，将正式默认 NEON 运行包升级部署至目标机（保留
`2fea36e` 设备外校验备份，生产继续保持 GE2D 关闭），并在正式包下完成 Stage 1
有界复验与五业务健康核对：

- **默认 NEON 复验**：`HEVC` 2-slice 640×384 20/20 与 `H.264` 360p 30/30
  可见像素 MD5 逐帧精确一致；`JPEG` 1080p 5 帧、编码 `NV12M` dumb DMA-BUF
  15 帧直读（`DIRECT=15, COPIED=0`）、`RC`（`i-qp=36` 首个 IDR 不误计饱和，
  `qp=32`, `saturated_frames=26`）与 `H.263` 4CIF/PLUS 解码通过，结束后
  `pool_bytes=0`。

## 11-27 GE2D 格式色彩矩阵翻转旋转与缓存暂存验收

完成 #31 GE2D 多格式（`NV12`/`NV21`/`NV21M`/`YUV420`/`YUV420M`/`YVU420`/
`YVU420M`/`UYVY`/`RGB24`/`BGR24`/`XBGR32`/`RGBX32`/`XRGB32`）、BT.601/BT.709
有限与全量程矩阵、`HFLIP`/`VFLIP`/`ROTATE(90/180/270)` 及 `vdec` 显式 GE2D
MMAP 缓存别名 `capture_stage`（配合 `dma_sync_single_for_cpu`）的宿主回归与
S805 实机临时加载验收：

- **工作机与双构建验证**：`make test`（153 项主机测试含 ASan/UBSan 与文档检查）
  通过；`ge2d/ge2d-layout.h`、`ge2d/meson8b-ge2d.c` 与 `vdec/meson8b-capture.c`
  通过严格 `checkpatch.pl`（0 告警）；5.15.79 ARM 外部/additive 模块构建与
  [0001-meson8b-runtime-codecs.patch](../patches/0001-meson8b-runtime-codecs.patch)
  同步通过（`meson8b-ge2d.ko` SHA256
  `cd5e2e5b29f3a09ecb2d86bdb13f6601285013cb8c8dbd2b41b21e5fe9201157`、`meson-vdec.ko`
  `5d4dfb9005ed71a06a41a9fc71e5c8613a418a452fe99bd90e43917477f6d3af`、
  `meson8b-runtime.ko` `e88dd31947056dcc416357ac25cfdbd1eb99a43d53d5c4e65a6be3e1b28c1b02`、
  `meson-venc.ko` `e6b1aa08d4c071f1b0507f9303dbca7f8d25bd3e6660a36133a02bba530e9595`）。
- **S805 实机临时验证（`meson8b-ge2d.ko` + `capture_ge2d=1`）**：
  - `RGB24/BGR24/XBGR32/RGBX32/XRGB32` 互转全部全像素精确匹配（按 V4L2 字节序）；
    `NV12 -> NV12M` 走 `GREY` 平面直拷逐字节零误差，`UYVY -> NV12M` 通过，
    `NV21/NV21M/YUV420/YUV420M/YVU420/YVU420M -> NV12M` 经由 `ge2d_blit_one()`
    硬件 4:2:0 色度相位插值后亮度逐像素一致、平滑色度最大差 `<= 4`（需注意
    `SRC_FMT` `0x4c` 垂直色度相位在阶跃边界处约有 `-0.75` 行插值）。
  - `BT.601` 有限/全量程与 `BT.709` 有限/全量程 `RGB24 -> NV12M -> RGB24`
    回环色块及绿条 luma（`140`/`145`/`164`/`173`）实机通过；`HFLIP`、`VFLIP`、
    `ROT90`、`ROT180`、`ROT270` 对 64×32 `RGB24` 全像素精确一致。
  - `vdec` 缓存 `capture_stage` MMAP 输出与软件参考逐帧精确一致：`H.264` 360p
    30/30 及 `HEVC` 2-slice 640×384 20/20 可见像素 MD5 精确一致，`320×180`
    MMAP 缩放 30/30 通过；`meson-drm` 哑帧 DMA-BUF `320×192` 经 `vdec -> venc`
    共享转码 30/30（`DIRECT=30, COPIED=0`），`v4l2-accept`（`CREATE_BUFS`/
    `PREPARE_BUF`/同 `fd` 重开清零缩放/`H.263 GN=0` 拒绝）通过，结束后
    `pool_bytes=0`。
- **还原与未关闭边界**：验收结束后已卸载 `meson8b-ge2d.ko` 并恢复在役默认
  NEON（`capture_ge2d=N`、`meson8b-vcodec`/`containerd`/`docker` `active` 且
  `NRestarts=0`、五容器运行、四项本地 HTTP `200`、`pool_bytes=0`）。双源/alpha
  混合（#31 剩余范围）与生产默认 GE2D 启用继续保留开放。
