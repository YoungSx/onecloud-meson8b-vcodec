# 故障排查

先记录 `uname -r`、包的 `SOURCE_COMMIT`、unit 状态、`dmesg` 和输入 metadata。
生产设备避免反复并发重试。结束 codec 客户端后再启停 unit。

| 现象 | 检查与处理 |
| --- | --- |
| 没有 video 节点 | `systemctl status meson8b-vcodec.service` 与 `journalctl -u meson8b-vcodec.service`；检查 probe 和固件哈希 |
| kernel/phandle mismatch | 包不匹配当前运行树或 ABI；重新生成 Overlay/构建包，不能强制加载 |
| `Invalid module format` / 未解析符号 | 使用匹配原配置完整内核输出，检查 vermagic、Module.symvers 与导出依赖 |
| CMA 分配失败 | 检查 `/proc/meminfo` 的 CmaFree，关闭其他 codec 会话；帧池按码流申请，与应用 CAPTURE 数无关 |
| 成功退出但空文件/少帧 | 检查帧数及哈希、LAST/EOS；确认所用官方FFmpeg版本与输出线程设置，见[适配说明](ffmpeg-compatibility.md)；损坏/不支持的流可产生部分输出 |
| HEVC 停滞 | watchdog 约五秒结束当前队列；保留 bitstream 日志，结束客户端后试有效流验证恢复 |
| unload busy | 退出所有持有设备的客户端，再停止 unit；不强制 rmmod |
| 回滚拒绝固件变化 | state 记录后存在其他更新；保留备份并比较哈希，不能盲目覆盖 |
| `/tmp` 空间不足 | raw 1080p 图像很大，长测试参考使用 `/dev/null`；只清理自己产生的测试文件 |
| `picture at QP N exceeds the 1024 KiB bitstream buffer` | 单帧超过缓冲，失败后尝试恢复 IDR；累计第三次溢出或 MAX_QP 仍溢出时终止队列，STREAMOFF 后再开始，避免无界重试 |
| 码率高于目标且 QP 停在 32 | 核对 `rate_control` 的窗口、饱和时长与超目标次数；此微码部分内容在 QP32 以上码率反而增加，应按实测降低尺寸/帧率或用软件编码 |
| 设置 `maxrate/bufsize` 后仍超目标 | 上游 V4L2 包装未映射峰值/CPB 控件；本驱动提供 VBR 平均目标跟踪，见[码率控制](rate-control.md) |
| 设置 MIN/MAX_QP 后客户端卡在 D 状态 | 仅 `bd1cb79`/v0.1.0 模块有此死锁；`cd4e6b5` 起修复。旧模块上不要修改这两个控件 |
| RGB24 宽度被改小 | RGB24 最宽 1376；更宽时改用 XRGB32/XBGR32 等 32 位格式 |
| 解码转换耗时偏高 | `/sys/kernel/debug/meson-vdec/` 的 `converted_frames`、`cropped_frames`、`convert_ns` 给出每帧 CPU 转换耗时；1080p 约 7–8 ms 属正常，明显更高时检查是否走未对齐左偏移路径或 CPU 被其他负载占满 |
| DMA-BUF 输入未走直接路径 | 只有 NV12M/NV21M、每个平面一段连续地址、8 字节对齐且行宽等于 bytesperline 时直接读取；`/sys/kernel/debug/meson-venc/` 计数可确认 |

宿主原7.0.2缺少`hevc_v4l2m2m`；当前容器工具不同，先核对`-decoders`。[使用指南](usage.md)
区分生产编码工具和独立解码工具。JPEG 输入须全量程 YCbCr。

历史 USB URB `-121` 日志在 codec 前已存在，不能只看旧 dmesg 就归因于
本驱动；对比本次加载与解码期间新增日志。失败 probe 的 `insmod` 可能仍
返回成功，runtime loader 会额外检查两个设备名称。

2026-11-22 的两轮 LAN/Tailnet失联不能混作同一事故：旧 VDEC1通用调频可
重调以太网依赖的MPLL2，第一轮有高置信度证据；第二轮原厂频率混载后
失联的journal尾部未落盘，仍未定案。详见[逐轮调查](validation-records.md#失联与故障保存)。
`journalctl -b -1`未显示不代表归档都不存在，必要时按`--file`和`_BOOT_ID`
核对损坏/归档文件。加载ramoops模块也不等于配置了可保存崩溃日志的内存。

## 正常运行时保留失联现场

在已有持久磁盘的私有目录运行
[`observe-network.py`](../scripts/observe-network.py)，用正常业务观测，不启动
编解码压力任务。每10秒记录启动ID、uptime、网口carrier和累计收发/错误计数、
IRQ/softirq、内存/换页、阻塞任务数、磁盘I/O与剩余容量/inode，并向该网口的默认网关发送一次ping。
ping返回值与执行错误分开记录，缺失指标记录errno，不算作正常。

```sh
sudo systemd-run --unit=vcodec-network-observer --collect \
  --property=RuntimeMaxSec=6h --property=UMask=0077 \
  /usr/bin/python3 /path/to/scripts/observe-network.py \
  --interface eth0 --seconds 21600 --interval 10 \
  --log /path/to/private-persistent/network.jsonl
```

用`--storage /path/to/data-mount`另外观测数据盘容量；日志盘满时，应先选择
有空间的持久目录，不能依赖满盘继续留证。root可用的保留块和普通进程可用
空间分开记录，避免将“root还能写”当作应用还有空间。

目录须已存在；替换上述两处路径。只运行一个实例，日志目录仅root可访问。
每条JSON写入后fsync，轮转时亦同步目录；当前文件和`.previous`各最多4MiB。
默认运行30分钟；示例运行6小时后退出，不注册开机任务。提前停止使用
`sudo systemctl stop vcodec-network-observer.service`。
磁盘写满/写失败会使采集退出，应检查unit状态和文件更新时间。

此工具不修改时钟、链路或路由，不访问watchdog，不尝试自动恢复网络。
网关ping失败只表示这条探测路径失败；结合另一台LAN设备的ARP/ping/端口
记录以及本机计数增量判断。累计IRQ仍增长、uptime连续而外部LAN失败支持
“本机仍运行”，但不单独定位PHY/MAC/DMA。最后一条记录后没有新记录既可能
是主机卡住，也可能是采集或存储阻塞，不能据此直接认定CPU死锁。

原始记录仅供私下取证。此采集不读取clock寄存器/任务堆栈，也不能代替串口、
独立内核日志或已验证的crash buffer；异常断电后持久化还取决于磁盘缓存。
第二轮失联根因仍待故障现场证据，生产持续负载保持暂停。
