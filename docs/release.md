# CI 与发版流程

采用 SemVer 标签 `vMAJOR.MINOR.PATCH` 和 Conventional Commits。版本
绑定记录的 OneCloud 内核/设备树 ABI。版本标签生成 Release 草稿，发布者
审阅后作为正式版发布；只有确实需要标注不稳定时才手工勾选 pre-release。

## CI

`CI` 在 push 到 main、pull request 和手动触发时运行：

| Job | 检查 |
| --- | --- |
| Host checks | 固定微码复现、Python tests、C ASan/UBSan、ShellCheck、本地文档链接 |
| ARM build and packages | 原配置内核完整构建、外部/源码树 codec、补丁同步、vermagic/依赖、静态 FFmpeg/v4l2-ctl、包校验 |

底层 reusable workflow 同时被 tag Release 调用。Actions 使用固定 commit
SHA，构建读权限；只发布草稿的 job 获得 contents write。生产机不参与 CI。
源码 tarball 缓存只保存输入，解压构建每次检查哈希，避免旧对象冒充新构建。

Actions artifact `release-assets` 包含运行包、源码包、`SHA256SUMS` 和
`PROVENANCE.json`；日志单独保留。CI 成功不等于当前二进制完成新的硬件验收。

## 发布一个版本

1. 按 [Contributing](../CONTRIBUTING.md)完成改动和测试。更新
   [CHANGELOG](../CHANGELOG.md)，保持双语 README 一致。driver 变化补实际
   S805 内容/恢复/服务/回滚验收。
2. 提交推送 main，确认该 commit 的 CI 通过。不要用另一 commit 的绿色状态。
3. 建立注释版本标签并推送：

   ```sh
   git tag -a v0.1.0 -m 'v0.1.0'
   git push origin v0.1.0
   ```

4. `Release` 工作流重跑全部检查，从标签同一提交构建并创建 **draft**。
   草稿附运行包、包含上游源码的完整 source 包、外部校验和与来源 JSON。
   草稿不标 pre-release，也不自动替换目标机上的版本。
5. 下载校验资产，检查 changelog、source commit、历史硬件验收范围和
   [安装/回滚说明](installation.md)，再把草稿发布为正式版。

重试失败 workflow 时，先修复来源明确的问题。已存在的 Release 不自动
覆盖资产；不要复用已发布版本标签，修复应递增版本。维护者可清理自己
未发布的失败草稿后重试，保留已发布版本不变。

## 本地复现

在已提交、干净工作树上运行[构建流程](build.md)，然后：

```sh
bash scripts/package-release.sh v0.1.0 build/release-v0.1.0
```

脚本拒绝已有输出目录、未提交源码与缺失工具。两个 archive 来源同一提交，
内部清单覆盖全部文件，外部清单覆盖归档与来源 JSON。打包规范化 tar 的
排序、时间和 owner；跨工具链版本仍应比较 provenance，不假定字节一致。

## 已安装硬件验收版本

在役机 2026-11-15 起安装 `c20a473` 运行包（v0.2.0），2026-11-17 起安装
v0.3.0：先装本机构建的 `0a0542b` 完成回归与 30 分钟负载，当晚换成 GitHub
Release 包（CI 从 `abee6f6` 构建，驱动代码相同）并重新回归，见
[v0.3.0 deploy](../evidence/p6-2026-11-17/v030-deploy.md)；`bd1cb79` 是
2026-11-14 完整硬件验收对应的版本。后续 CI/Release 产物不自动部署，新版本
需要按安装指南明确升级，生产原内核和原 FFmpeg 保留。
