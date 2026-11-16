# 文档目录 / Documentation

当前使用与开发说明在本目录。日期验收记录属于对应版本的证据；早期方案
保留在历史目录。中英文入口为 [README](../README.md) 和
[中文 README](../README.zh-CN.md)。

| 需要做什么 / Task | 文档 / Guide |
| --- | --- |
| 安装、升级、卸载和回滚 | [Installation](installation.md) |
| FFmpeg、JPEG、设备枚举、缓冲数量 | [Usage](usage.md) |
| 外部交叉构建、配置、源码补丁 | [Build](build.md) |
| 模块职责、共享资源、输出数据路径 | [Architecture](architecture.md) |
| 复现微码、上游源码与许可证 | [Firmware](firmware.md) |
| 实机能力、帧数、性能、已知限制 | [Validation](validation.md) |
| HEVC 硬编可行性与 H.264/JPEG 编码扩展 | [Encoding feasibility](encoding-feasibility.md) |
| probe、CMA、队列和错误恢复问题 | [Troubleshooting](troubleshooting.md) |
| CI、版本标签、Release 草稿与发布 | [Release process](release.md) |
| 提交和测试约定 | [Contributing](../CONTRIBUTING.md) |
| 当前后续工作 | [Roadmap](../ROADMAP.md) |
| 下一阶段顺序、验收门槛与范围 | [Development plan](development-plan.md) |
| 发布历史 | [Changelog](../CHANGELOG.md) |
| 上游结论、共享 DOS 所有权设计 | [linux-media 结论](linux-media-rfc.md) / [Power domain](power-domain-design.md) |

[部署快照](rollout-status.md)记录在役机当前安装的 `c20a473` 包及
2026-11-14 `bd1cb79` 验收版本，并非新版本的安装指令。[验收索引](../evidence/README.md)指向原始证据。
[历史索引](history/README.md)保留研究阻断、开发日志和 HEVC 来源核查；
[linux-media 结论](linux-media-rfc.md)记录在 Discussions，本项目不向上游提交；[共享 DOS 所有权设计](power-domain-design.md)
只是方案，未实现也未上机验证。
