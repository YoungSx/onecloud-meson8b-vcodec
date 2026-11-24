# 生产部署快照 / Deployment snapshot

2026-11-24 UTC 核对的精确来源。本文是部署记录，不是通用安装操作；新设备按
[安装指南](installation.md)，历史版本按[验证记录](validation-records.md)复核。

| 组件 | 当前记录 |
| --- | --- |
| 内核 / boot DT / CMA | 原版5.15.79-meson；启动文件未替换，CMA64 MiB。 |
| 最新正式版 | v0.4.0，源码`89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f`；原安装及回滚资料保留。 |
| 已部署开发驱动 | 完整运行包源码`1cf0e719a149e93bafa25352f980cc24445847eb`，`/opt/meson8b-vcodec/current`；三个模块、配套固件及自动加载已保留部署。 |
| OpenList 镜像 | `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`。 |
| 镜像内应用代码 | Backend `6c77764f6e940c218f08576ae67782376467d54c`；frontend `a845b348e7967e4a76f2ef07b9e840709def96cf`。后端后续提交为文档更新。 |
| FFmpeg | 官方底座8.1.3，FFmpeg/ffprobe逐字节未改；宿主原7.0.2是另一套工具。 |

## 已安装模块 SHA256

| 文件 | SHA256 |
| --- | --- |
| meson-vdec.ko | `a92fb63d2a49ca71f749f14e9441fe1180b034f590e6b1aa8eeaf40c2469c2d1` |
| meson-venc.ko | `48bfa619e3a392418ed5f1f358cdbed20a85a172b9fdf16f8d484c7cac748aa4` |
| meson8b-runtime.ko | `e88dd31947056dcc416357ac25cfdbd1eb99a43d53d5c4e65a6be3e1b28c1b02` |
| runtime overlay | `72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e` |

[11-24安装验收](validation-records.md#11-24-正式安装验收)对应这些模块。
官方未修改FFmpeg 7.0.2完成H.264 50/50与1080p 300/300、HEVC小流10/10、
crop 10/10和1080p 30/30完整帧哈希；生产镜像8.1.3短片复核通过。
H.264/JPEG编码、SAR 17:13及两路320×240编码完成完整软解与Y/U/V检查。
默认仍为NEON输出，未加载GE2D。

已完成service stop/start并复核内容，unit为enabled/active；OpenList已恢复，
四项HTTP均200，26个启动文件哈希不变。保留原Linux 5.15.79-meson、boot DT
和64 MiB CMA；本轮没有重启。冷启动、异常重启、长期负载和原厂对照仍未验收。

新增路径有明确失败：两种HEVC tiles帧数完整但像素不一致；H.263四种三图短流
均为2/3，WMV3为3/5；本轮拼接尺寸切换H.264为392/400、HEVC为40/60。
这些不计入支持能力，旧版本的尺寸切换结果不转移到当前包。

安装前完整`3c7d785`部署树、unit、固件和state已在设备和外部构建机校验备份；
原v0.4.0资料也保留。安装state位于开机可用的系统分区，当前包验证后保留。
`remove-autoload.sh`撤销安装，不会自动恢复旧驱动；本轮未演练整包降级。
原始材料仅本地校验归档，无永久公开附件；恢复入口见[安装指南](installation.md)，
私有材料按[工作区迁移](workspace-migration.md)清单保存。

更早的安装记录移到[历史部署](history/deployments-through-v0.4.md)，其中的
“当前”“最终”只对应当时版本。
