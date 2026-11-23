# 生产部署快照 / Deployment snapshot

2026-11-23 核对的精确来源。本文是部署记录，不是通用安装操作；新设备按
[安装指南](installation.md)，历史版本按[证据索引](../evidence/README.md)复核。

| 组件 | 当前记录 |
| --- | --- |
| 内核 / boot DT / CMA | 原版5.15.79-meson；启动文件未替换，CMA64 MiB。 |
| 最新正式版 | v0.4.0，源码`89dd558cd7cd1c8dc757ab9ec7e6f63b805e201f`；原安装及回滚资料保留。 |
| 已部署开发驱动 | decoder/encoder 源码`3c7d7854bcab3b93a716b7da84002015913082f0`，`/opt/meson8b-vcodec/current`；runtime沿用`5fedae9`的已部署二进制，源码无变化。 |
| OpenList 镜像 | `youngsx/openlist-onecloud:v4.2.10-official-ffmpeg-a845b34-clean`。 |
| 镜像内应用代码 | Backend `6c77764f6e940c218f08576ae67782376467d54c`；frontend `a845b348e7967e4a76f2ef07b9e840709def96cf`。后端后续提交为文档更新。 |
| FFmpeg | 官方底座8.1.3，FFmpeg/ffprobe逐字节未改；宿主原7.0.2是另一套工具。 |

## 已安装模块 SHA256

| 文件 | SHA256 |
| --- | --- |
| meson-vdec.ko | `34953c44cdfcaa997f96157a4036b6899bd9c6136a0111bf6c52ccdcedca6baa` |
| meson-venc.ko | `7d31095d1fa82a4565f3175d20badb5e3ed3ec7d476065d16b895cafbe1817d2` |
| meson8b-runtime.ko | `7ca640be512c76d71d2b3606c2a1bf4b0d7f3ca0db12896706ee6649abd6cb32` |
| runtime overlay | `72dbf9f078273ccc21f4aa90cedfa990857ee16017ecd261ccace6225474f03e` |

[11-23 P0部署验收](../evidence/p0-deploy-2026-11-23/README.md)对应这些来源。
HEVC四种提前关闭/重开、解码图像、H.264/JPEG编码和固件失败恢复通过；
旧`5fedae9`完整包保留，临时测试恢复旧包后才更新安装。新版不是正式稳定发版，
只有热更新和有界内容/恢复检查；新的冷启动和
异常重启尚未验收。旧版冷启动、长测不自动转移到新包。

11-23普通运行核查：四项HTTP为200、codec service active；磁盘满曾使Arcane
启动失败，清理重复归档后已恢复。详情见[日期记录](../evidence/passive-observer-2026-11-23/README.md)。
这些状态是时间点快照，不能当作持续在线承诺。

原v0.4.0包/firmware/unit/state和专用回退助手在设备与私有迁移归档中保留；
本次安装前的完整包、state和回退助手也已备份到外部构建机。
不能把移除全部codec安装的`remove-autoload.sh`误当作自动降级到v0.4.0。
私有路径及精确回退材料按[工作区迁移](workspace-migration.md)清单保存。

更早的安装记录移到[历史部署](history/deployments-through-v0.4.md)，其中的
“当前”“最终”只对应当时版本。
