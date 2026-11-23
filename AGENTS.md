# 项目约定

永远基于架构洁癖、最佳实践开发。

遵守 [仓库内容规则](docs/repository-policy.md)和[贡献指南](CONTRIBUTING.md)：

- 不提交 `evidence/`、`private/`、运行日志、逐次报告、输出媒体和实验草稿，
  也不换目录名规避规则。放到忽略的 `build/reports/`，有长期价值的原件
  校验后另存到工作区外。
- Git 只保留正式实现、可复用测试/最小素材及精简版本结论；历史过程不逐文件
  搬入文档。能力、部署和验证入口分别使用 `docs/validation.md`、
  `docs/rollout-status.md`、`docs/validation-records.md`。
- 修改后运行 `make test`。新 clone、CI 和源码包不能依赖本机报告/备份。
- 公开源码/文档使用通用示例，移除真实设备地址、登录材料、令牌和本机归档路径。
- 原件归档并校验后才能取消跟踪/清理；不以日常整理为由改写公开提交和标签。
