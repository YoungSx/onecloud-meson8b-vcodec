# 2026-11-22 LAN／整机离线调查

**第一轮故障高置信度指向旧驱动的共享 MPLL2 调频；第二轮尚不能定案。**
用户确认两次都无法直接访问局域网地址，因此不能仅归因于 Tailscale／DERP。
本轮没有重跑持续编解码负载，没有设置时钟、改网络规则或重启业务。

## 第一轮：网口通信失败，本机仍在运行

按本轮同步后的日志时钟与历史工具记录对齐：

| UTC 时刻 | 可核实事实 |
| --- | --- |
| 06:29:37 | 启动临时 VDEC1 255 MHz 对照；其模块 SHA256 为 `ce2ff924490562e149f6879cb7899fdf8920cb27fd2eb850c18d32a9e2089e87` |
| 06:29:38 | 最后仍收到 Tailscale 对端数据 |
| 06:29:48 | UDP 探测失败；随后多个 DERP/控制连接 EOF、超时；用户确认 LAN 也失败 |
| 06:31:27 | 落盘的测试收据显示两次600帧转码与300帧哈希通过，恢复模块完成 |
| 07:45:43 | 本机服务继续写日志，距首次网络失败约76分钟；内核也继续运行到 `218506.306713s` |

测试收据的四项 HTTP 是 `127.0.0.1` 回环请求，Arcane 是容器运行状态。
**回环健康和模块恢复不能证明以太网恢复。** 本机持续运行的事实排除了
“第一轮一失联就全机死机”，但不排除网口或网络栈故障。

旧 `vdec_1_start()` 直接对 `vdec_1` 调用 `clk_set_rate()`，该 mux/gate/divider
链允许 `CLK_SET_RATE_PARENT` 向上传播。父源既有固定 `fclk_div*`，也有可调
`MPLL2`；以太网 RGMII TX 同时使用 MPLL2。

从匹配 Linux 5.15.79 源码提取选频函数进行[离线复现](ccf-selection.c)，
[输出](ccf-selection.log)表明：

- 请求182142857Hz时，以固定 `fclk_div7` 364285714Hz除2，避免重调父源。
- 请求255000000Hz时，divisor 1的最先精确候选是 MPLL2，取整为255000000Hz。
  后续 divisor 2 的固定510MHz方案同样精确，但不会替换已经选到的候选。
- Ethernet TX 原路径约499996410Hz÷2÷2＝124999102Hz。若 MPLL2 被上述请求
  重调为255 MHz，该路径降到63.75MHz，不能继续正常提供千兆 RGMII 的125 MHz。
- 只重新插入原解码模块，可能把解码器重选回固定父源；不会恢复已不再被它
  使用、但仍被以太网使用的 MPLL2。重启初始化时钟可以恢复，符合现场现象。

另有一个相关内核描述问题：VDEC1 selector 列了6个父源，但掩码为 `0x3`。
选择软件父索引4会在硬件字段编码为0。不能据软件时钟的取整结果声称实际
VDEC1硬件就跑在255 MHz；MPLL2被重调的风险与这个索引问题都需要避免。

[只读实机取整查询](read-only-clock-query.json)显示同版运行内核将255 MHz
视为可取整的请求，前后 VDEC1、MPLL2、以太网等频率全部未变。临时查询
模块仅调用 `clk_get_rate`／`clk_round_rate`，没有 `clk_set_rate`、enable 或
MMIO写入，完成后已卸载。[模块源](clock-round-probe.c)供复核。

**证据强度：** 触发时间、源码调用、可复现的 CCF 选频、LAN/P2P同时失效且
本机持续运行相互吻合，故该机制为高置信度结论；故障瞬间没有寄存器快照，
本次没有通过再次破坏生产网络来复现，不能标为所有硬件细节均直接取证。
当前已安装 `5fedae9` 的固定父钟／精确 divider 保护替代了旧路径，原先的
[固定父钟实机检查](../p0-p3-2026-11-22/fixed-parent-live.json)与本轮只读快照
均显示 MPLL2保持约500 MHz、Ethernet TX 保持约125 MHz。

## 第二轮：原厂频率混合负载后失联，最后日志缺失

这轮使用了固定父钟的182 MHz候选。最后成功负载样本为 uptime7259.413s。
默认 `journalctl -b -1` 没有显示上一轮，并不意味着归档都不存在；直接按文件
和启动ID能读到损坏的 `.journal~` 文件。

[只读结构核查](journal-loss.json)发现：

- 最后可读 journal entry：6923.573697s；最后保留的内核记录：6913.549001s。
- journal header最后entry时间：7271.320251s。
- `0xd01070` 到索引记录的最后object `0xd615a0` 对应的数据区全为零；
  `journalctl --verify` 报损坏。约347.75秒的尾部没有持久化，无法从文件恢复。
- 根 ext4 挂载为 `commit=600`；journal通常也批量同步。硬重启可以丢失尚未
  落盘的数据。这个事实解释取证缺口，不等于证明磁盘故障导致失联。
- ramoops虽然加载，但 `mem_address=0`、`mem_size=0`，设备树也未配置对应
  reserved-memory。实际上没有 crash buffer。软锁死检测、hung-task检测和
  PSI亦未启用，故“没有 panic/lockup 日志”不能用来排除这些故障。

第二轮目前只能归类为 **LAN与Tailnet同时失去访问**。是 Ethernet MAC/DMA/
PHY、内核/DMA总线卡住还是供电/存储造成阻塞，现有尾部缺口无法区分。
后来补齐的DMA和锁保护是真实缺陷修复，但没有崩溃栈把它们与该次故障连接。

## 独立发现，不混作两轮共同根因

- **ShellCrash路由标记冲突：** 本机OUTPUT会把符合源地址条件的外网包完整
  改成 `0x1ed4`。Tailscale实际socket使用 `0x80000`，对应链没有豁免；改写后
  先命中priority5209/table100并被送到本机TPROXY，丢掉Tailscale绕过语义。
  这是VPN外层连接的确定配置冲突，但LAN子网已有RETURN，不能单独解释
  用户直接LAN访问也失败。
- **Ethernet watchdog：** 每分钟无条件设置EEE和autoneg。启动后约120秒的
  Link Down/Up与timer一致，首次禁用EEE也可能触发重协商；同设置的后续
  写入不一定再次硬件重协商。保留为需精简的网络恢复实现，未认定为离线根因。
- **USB：** 11-22两次启动的bus1 SSD均以480Mbps、usb-storage枚举；`usb2-1`
  的 `-121` 是12Mbps智能卡读卡器，不是SSD。同一保留窗口未见SSD I/O error、
  断连或EXT4 error。旧11-15磁盘未枚举事故不能直接推到这两轮。
- **当前资源：** 31.91秒只读采样中网关、回环HTTP和carrier全部正常，可用内存
  约470–542MiB，无OOM和直接reclaim stall。uxplay由hidagent重复拉起、
  OpenList/CrashCore有高CPU占用，但目前没有持续换页风暴；这不能还原故障时资源。

## 下一次失联如何闭环

优先用另一台LAN设备同时保留 ping、ARP响应与服务端口结果，并用串口或
独立的同步内核输出留下最后堆栈。若故障时本机仍能操作，要先取clock tree/
MPLL寄存器、Ethernet DMA/IRQ计数和任务状态，而不是先重启。若MAC或CPU
都停止，必须有串口／已验证保留内存的crash记录才能区分。

本轮未设置ramoops地址、未修改启动内核/DT、未强制降速/重协商或停止透明
代理。要恢复持续生产负载，先准备能在失联时保留日志的入口；不能靠再跑
一遍同样长测来补这个证据缺口。

## 可复核文件

[结构化结论](findings.json)、[当前健康](current-health.json)、
[被动采样](passive-health.json)、[CCF输出](ccf-selection.log)。
原始journal、生产syslog和代理配置留在忽略的 `build/` 私有目录，公开记录
仅含脱敏事实和归档哈希，没有设备地址、登录信息、worker token或业务凭据。

离线自检：

```sh
sh evidence/outage-investigation-2026-11-22/verify-clock-selection.sh
```

格式与语义核对依据：systemd官方
[Journal格式](https://systemd.io/JOURNAL_FILE_FORMAT/)、
[v245结构](https://github.com/systemd/systemd/blob/v245/src/journal/journal-def.h)、
[CCF rate propagation](https://www.kernel.org/doc/html/v5.15/driver-api/clk.html)。
