# 生产部署快照 / Deployment snapshot

2026-11-25 UTC 最后核对的精确来源；随后设备离线，状态不代表持续在线。本文是部署记录，不是通用安装操作；新设备按
[安装指南](installation.md)，历史版本按[验证记录](validation-records.md)复核。

| 组件 | 当前记录 |
| --- | --- |
| 内核 / boot DT / CMA | 原版5.15.79-meson；启动文件未替换，CMA64 MiB。 |
| 最新正式版 | v0.4.0，源码`89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f`；原安装及回滚资料保留。 |
| 已部署开发驱动 | 完整运行包源码`30989c8a1d943e62205d5a815b9de638165f8046`，`/opt/meson8b-vcodec/current`；三个模块、配套固件及自动加载已保留部署。 |
| OpenList 镜像 | `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`。 |
| 镜像内应用代码 | Backend `6c77764f6e940c218f08576ae67782376467d54c`；frontend `a845b348e7967e4a76f2ef07b9e840709def96cf`。后端后续提交为文档更新。 |
| FFmpeg | 官方底座8.1.3，FFmpeg/ffprobe逐字节未改；宿主原7.0.2是另一套工具。 |

## 已安装模块 SHA256

| 文件 | SHA256 |
| --- | --- |
| meson-vdec.ko | `a9d228a6e47557a6ce2934ac737d2ecb60e233534106c7d706af2712d6bc5e83` |
| meson-venc.ko | `48bfa619e3a392418ed5f1f358cdbed20a85a172b9fdf16f8d484c7cac748aa4` |
| meson8b-runtime.ko | `e88dd31947056dcc416357ac25cfdbd1eb99a43d53d5c4e65a6be3e1b28c1b02` |
| runtime overlay | `72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e` |

[11-25后续验收](validation-records.md#11-25-切换修正与新增路径诊断)对应当前decoder。
当前包H.264拼接尺寸切换400/400、HEVC 60/60全部可见NV12帧MD5与软解一致；
H.264旧CAPTURE序列LAST已补齐，切换等待超时消失。上一轮392/400和40/60
是客户端摘要漏算切换drain中的图片，原始逐帧文件实际为400和60，已纠正。
静态尺寸及编码的历史验收仍按[11-24记录](validation-records.md#11-24-正式安装验收)
核对，不能将其全部视作本包重测。

仍开放：HEVC tiles边界像素不一致；WMV3为3/5。H.263新VOP边界候选已在
临时模块恢复baseline 128×96、PLUS 164×100和704×576各3/3，顺序/全平面
通过；源码`0b24b63`尚未正式安装，当前包仍有旧尾帧限制。GE2D临时Overlay
在GE2D模块加载前被拒绝，后续无导出符号修正只通过主机检查，尚未实机。
默认仍为NEON，生产不启用GE2D。

当前包安装完成时unit为enabled/active、OpenList已恢复；每轮诊断结束脚本均
恢复正式service和OpenList。最后一次GE2D加载失败后恢复命令成功，随后SSH与
Tailnet均不可达，离线根因未定位，不能据此认定发生内核崩溃或GE2D硬件异常。
没有切换启动内核/boot DT/CMA，也没有执行重启。冷启动、异常重启、长期负载
和迅雷原厂对照仍未验收。

升级前`1cf0e71`完整安装树、unit、固件和state已在设备及外部构建机校验备份；
更早的`3c7d785`和原v0.4.0资料保留。新state位于持久系统分区，未演练整包降级。
`remove-autoload.sh`撤销安装，不自动恢复旧驱动。原始材料仅本地校验归档，
无永久公开附件；恢复入口见[安装指南](installation.md)，私有材料按
[工作区迁移](workspace-migration.md)清单保存。

更早的安装记录移到[历史部署](history/deployments-through-v0.4.md)，其中的
“当前”“最终”只对应当时版本。
