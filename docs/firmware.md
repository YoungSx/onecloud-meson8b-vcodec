# 固件、源码来源与许可

微码不是 GX 固件改名，而是从 Hardkernel 的 S805 GPL 源码固定提交提取。
完整输入文件哈希、词数、哨兵规则和输出 SHA-256 位于
[firmware/sources.json](../firmware/sources.json)。

- 厂商仓库：<https://github.com/hardkernel/linux>
- 固定提交：`5aed95d35d252cafc75ce613a3a0052285662de2`
- H.264：16 KiB 主程序加 20 KiB swap 段；其他当前微码补齐为 16 KiB。
- 提取前检查输入哈希，提取后检查布局及输出哈希。MPEG-4/VC-1/HEVC 等
  没有尾部哨兵的数组保留最后真实指令。

```sh
python3 scripts/extract-firmware.py --fetch
```

缓存保存在 `sources/vendor/`。源码交付包同时包含对应原微码源，支持离线
复现。H.264 还有独立 C 编译和逐字节比较测试，避免仅用同一解析器自证。

内核基线是 Linux 5.15.79 加 Armbian OneCloud 补丁，来源版本与哈希见
[构建指南](build.md)。HEVC stateful handler 的来源及 S805 协议差异见
[HEVC 验收](validation-records.md#解码与帧池)和
[来源核查](history/hevc-feasibility.md)。

## 许可

驱动按各文件 SPDX 为 GPL-2.0/GPL-2.0+，项目 [LICENSE](../LICENSE)包含
GPL v2 正文。微码来源文件保留厂商 GPL 声明。

独立 FFmpeg 7.0.2 构建未开启 GPL codec，附 `COPYING.LGPLv2.1` 和原源码
tarball、补丁及构建脚本；它不替代生产 FFmpeg。独立 v4l2-ctl 1.22.1 附
GPL `COPYING.v4l-utils` 和原源码 tarball。Release 的源码包同时提供源码、
接口定义、配置与编译安装脚本；运行包中的 `SOURCE_COMMIT` 指向对应版本。
