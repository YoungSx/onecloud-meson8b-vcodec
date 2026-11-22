# 安装、升级与回滚

本项目使用原版 Linux `5.15.79-meson` 加载外部模块。包中 Overlay 的
provider phandle 必须与目标机运行树一致。默认 DTS 是原验收设备快照，
同型号设备也应校验，不能仅凭板型复用。内核升级后按新 ABI 重编三个模块。

## 获取与校验

在外部机器构建或下载匹配版本的运行包及 `SHA256SUMS`。校验归档后解压到
独立目录，以下以 `runtime-package/` 为例。运行包内还有覆盖全部文件的
清单，安装器会再验证它、目标内核、OneCloud 身份和 provider phandle。

```sh
sha256sum -c SHA256SUMS
mkdir runtime-package
tar -xzf onecloud-meson8b-runtime-VERSION.tar.gz -C runtime-package
sudo python3 runtime-package/scripts/runtime-device.py check runtime-package
```

`check` 是只读检查。发现版本或 phandle 不匹配时，先按[构建指南](build.md)
生成匹配包。不要强制加载、改 vermagic 或覆盖启动文件。

## 安装自动加载

安装前结束 codec 客户端。`STATE` 必须是新的现有父目录下的子目录；其中
保存每次固件覆盖前的备份和恢复记录。安装器拒绝覆盖已有服务安装。

```sh
sudo bash runtime-package/scripts/install-autoload.sh runtime-package /root/new-codec-state
systemctl is-enabled meson8b-vcodec.service
systemctl is-active meson8b-vcodec.service
ls /dev/video*
```

安装目录为 `/opt/meson8b-vcodec/current`，备份路径写入
`/opt/meson8b-vcodec/state-path`。Unit 加载前重新校验包和固件，并检查两个
codec probe 成功；失败时卸载本次加载的模块。它仅绑定原内核版本。
启停与失败清理已有实机记录；开机自动加载已于 2026-11-16 冷启动验收通过
（见[重启验收](../evidence/p6-2026-11-16/reboot-autoload.md)）。

```sh
# 先退出客户端，再停止或重启
sudo systemctl stop meson8b-vcodec.service
sudo systemctl start meson8b-vcodec.service
```

## 手动一次性加载

不需要自动加载时使用以下流程。不要与 systemd 安装同时执行。

```sh
sudo python3 runtime-package/scripts/runtime-device.py apply runtime-package /root/manual-codec-state
sudo python3 runtime-package/scripts/runtime-device.py rollback /root/manual-codec-state
```

## 开发 overlay 的临时加载

在役机当前已暂停持续负载实验。以下用于有救援入口的开发设备，不是升级
流程。不要为一次测试覆盖 `/lib/firmware/meson/vdec/meson8b-runtime.dtbo`：
异常重启会使原安装包的校验失败，原编解码服务无法自动恢复。

最新开发脚本可从已校验的包生成独立临时 overlay 文件，经 runtime 模块的
只读 `overlay_file` 参数加载。成功或失败后移除临时文件；突然掉电时即使
独立文件残留，原 overlay 与原 systemd 安装也不受影响。其他微码必须仍与
已部署固件一致。此路径要求配套的新 runtime 模块，主机失败清理测试和
5.15.79 交叉构建通过，尚未做实机重启验收。

```sh
sudo sh scripts/dev-swap.sh dev --overlay candidate.dtbo \
  runtime/meson8b-runtime.ko vdec/meson-vdec.ko venc/meson-venc.ko
sudo sh scripts/dev-swap.sh release
```

直接使用新版本加载器时，先在空闲设备卸载原模块，然后运行
`runtime-device.py load CHECKED_PACKAGE --temporary-overlay`；测试结束卸载
候选模块并启动原服务。此命令不替换原固件或安装自动加载。

## 完整回滚

结束客户端后执行当前安装中的移除脚本：

```sh
sudo bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh
```

它禁用并移除 unit，按 encoder → decoder → runtime 顺序卸载模块，再恢复
安装前固件/Overlay，保留备份目录供核查。回滚拒绝覆盖安装后被另行更新的
固件；遇到拒绝时保留 state，按[故障排查](troubleshooting.md)检查差异。
此前实验阶段已存在的固件属于安装前快照，回滚后可仍留在磁盘而不被加载。

## 升级

校验新包并保留旧包及 state。退出客户端，完整移除旧安装，再以新的 state
安装新包。失败时从旧包重新安装，并使用另一个新的 state。不要直接改写
`current/` 中的文件，否则下次校验加载会拒绝。升级不需要更换 WanOS 或 `/boot`。
