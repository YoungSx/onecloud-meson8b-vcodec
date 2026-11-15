# 故障排查

先记录 `uname -r`、包的 `SOURCE_COMMIT`、unit 状态、`dmesg` 和输入 metadata。
生产设备避免反复并发重试。结束 codec 客户端后再启停 unit。

| 现象 | 检查与处理 |
| --- | --- |
| 没有 video 节点 | `systemctl status meson8b-vcodec.service` 与 `journalctl -u meson8b-vcodec.service`；检查 probe 和固件哈希 |
| kernel/phandle mismatch | 包不匹配当前运行树或 ABI；重新生成 Overlay/构建包，不能强制加载 |
| `Invalid module format` / 未解析符号 | 使用匹配原配置完整内核输出，检查 vermagic、Module.symvers 与导出依赖 |
| CMA 分配失败 | 检查 `/proc/meminfo` 的 CmaFree，关闭其他 codec 会话；CAPTURE 数量不能低于实际 DPB 要求 |
| 成功退出但空文件/少帧 | 检查帧数及哈希、LAST/EOS；生产 FFmpeg 未包含独立 drain 修复；损坏/不支持的流可产生部分输出 |
| HEVC 停滞 | watchdog 约五秒结束当前队列；保留 bitstream 日志，结束客户端后试有效流验证恢复 |
| unload busy | 退出所有持有设备的客户端，再停止 unit；不强制 rmmod |
| 回滚拒绝固件变化 | state 记录后存在其他更新；保留备份并比较哈希，不能盲目覆盖 |
| `/tmp` 空间不足 | raw 1080p 图像很大，长测试参考使用 `/dev/null`；只清理自己产生的测试文件 |
| `picture at QP N exceeds the 1024 KiB bitstream buffer` | 单帧超过码流缓冲，该帧返回错误、下一帧为 IDR；提高 QP 或启用码率控制，避免在 1080p 使用很低的固定 QP |
| 码率高于目标且 QP 停在 32 | 目标低于该内容在 QP 32 的码率；此微码在 QP 32 以上码率不再下降，应降低分辨率或帧率 |
| 设置 MIN/MAX_QP 后客户端卡在 D 状态 | 仅 `bd1cb79`/v0.1.0 模块有此死锁；`cd4e6b5` 起修复。旧模块上不要修改这两个控件 |
| RGB24 宽度被改小 | RGB24 最宽 1376；更宽时改用 XRGB32/XBGR32 等 32 位格式 |
| 解码转换耗时偏高 | `/sys/kernel/debug/meson-vdec/` 的 `converted_frames`、`cropped_frames`、`convert_ns` 给出每帧 CPU 转换耗时；1080p 约 7–8 ms 属正常，明显更高时检查是否走未对齐左偏移路径或 CPU 被其他负载占满 |
| DMA-BUF 输入未走直接路径 | 只有 NV12M/NV21M、每个平面一段连续地址、8 字节对齐且行宽等于 bytesperline 时直接读取；`/sys/kernel/debug/meson-venc/` 计数可确认 |

目标原 FFmpeg 缺少 `hevc_v4l2m2m`，用包内独立工具。[使用指南](usage.md)
区分生产编码工具和独立解码工具。JPEG 输入须全量程 YCbCr。

历史 USB URB `-121` 日志在 codec 前已存在，不能只看旧 dmesg 就归因于
本驱动；对比本次加载与解码期间新增日志。失败 probe 的 `insmod` 可能仍
返回成功，runtime loader 会额外检查两个设备名称。
