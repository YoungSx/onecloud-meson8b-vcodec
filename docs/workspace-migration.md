# 工作区迁移 / Workspace migration

源码仓库不是生产机备份。重新clone能恢复代码/固件/测试样片/精简验证结论，不能恢复
忽略目录中的设备原始文件、私有日志、登录配置或曾部署的精确二进制。
不要把这些私有材料补交到公开Git。
运行报告和全部历史 `evidence/` 也不再进入 Git，保存约定见
[仓库规则](repository-policy.md)。

## 新工作区需要的三个仓库

```sh
git clone https://github.com/YoungSx/onecloud-meson8b-vcodec.git
git clone --branch feat/transcode-v4l2m2m https://github.com/YoungSx/OpenList.git
git clone --branch feat/transcode-v4l2m2m https://github.com/YoungSx/OpenList-Frontend.git
```

驱动用`main`；另外两份必须选择fork分支，不能clone上游main后以为已含本次功能。
具体已部署源码与哈希见[部署快照](rollout-status.md)，不以新clone的HEAD推断
线上二进制。需要离线保全Git时使用各仓库的`git bundle create NAME.bundle --all`。

[构建指南](build.md)覆盖驱动；后端的`internal/transcode/v4l2m2m/README.md`
说明配套前端嵌入和镜像底座。前端按`package.json`的Node/pnpm版本安装依赖，
后端按`go.mod`使用Go工具链。依赖缓存和旧绝对路径都不是构建输入。

## 删除旧工作区前必须另存

| 材料 | 为什么不能只靠clone |
| --- | --- |
| 设备最初boot文件/DT/config/原软件包及哈希 | 救援与原厂对照；不能用新构建代替原始快照。 |
| 原v0.4.0安装、firmware、unit、state及回退助手 | 升级后的精确回退；`remove-autoload.sh`只撤销安装，不自动切回旧包。 |
| 当前`1cf0e71`完整运行包及安装state、升级前`3c7d785`整包备份、原v0.4.0/`5fedae9`资料和干净OpenList镜像归档 | 新编译不保证逐字节相同；不能依赖短期CI artifact留存。 |
| 原始故障journal、syslog、私有测试结果与采集脚本 | 公开仓库只有脱敏摘要，原件可能含凭据，仅私下取证。 |
| 需要复核的历史运行报告、输出媒体和试验脚本归档 | 新源码树不含 `evidence/`；长期报告必须独立保存，CI artifact 可能过期。 |
| SSH配置、认证辅助材料、设备入口记录 | 不属于公共源码；账户级SSH/GitHub/Tailscale登录需独立保存或重新登录。 |
| 三份Git仓库的未推送提交和未提交文件 | 迁移前逐仓库检查；不能只检查工作区最外层。 |

归档放到旧工作区之外，目录0700、文件0600，检查归档完整性、内部清单与
SHA256；再复制到独立机器或备份盘。旧工作区外的同盘归档能抵御删工作区，
不能抵御整台构建机或整块磁盘丢失。

## 可删除、可重建

- 解压的Linux/FFmpeg/v4l-utils源码树、内核对象、host测试输出、旧候选构建目录。
- 前端`node_modules`、生成的`dist`和后端`public/dist`生成资产；保留锁文件，
  重建前先清除旧生成资产，防止混合前端版本。
- 已归档校验的重复交付目录、早期实验工作区脚本和旧PR草稿；Git只保留
  精简失败结论、实际测试素材和研究补丁，需要复核的原始报告从外部归档恢复。

这些删除只作用于构建工作区；不应顺带清理生产下载、数据库、业务配置或回滚材料。

## 恢复时

1. 先校验外部归档的SHA256和Git bundle，再clone三份正确分支。
2. 仅按需解出私有SSH配置、故障证据或精确部署包到忽略的`build/`或单独私有目录。
   不把整个旧归档覆盖到新clone上，否则会重新引入旧文档、资产和源码。
3. 驱动运行`make test`；按构建指南重新生成模块/overlay。需要离线构建时恢复
   校验过的原始源码tarball与vendor输入，保留原目录结构。
4. 用包内`SHA256SUMS`和当前部署快照比对；登录设备后重新读取状态。
   迁移本身不要求重启WanOS或替换已安装驱动。

私有归档的实际位置、清理清单、恢复命令和原始设备路径由工作区外的
`MIGRATION.zh-CN.md`记录；该清单不公开上传。

本次历史报告归档的位置同时记录在忽略的
`build/reports/history-archive-location.txt`。删除工作区前将该定位文件及
归档的 `SHA256SUMS`、`MANIFEST.json` 和 Git bundle 一并转存到独立备份。
