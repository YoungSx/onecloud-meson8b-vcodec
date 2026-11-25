# OneCloud 部署、卸载与版本回退

适用于玩客云 S805/Meson8b、原版 **Linux 5.15.79-meson**。运行包安装三个
外部模块、固件和可移除的运行时设备树 Overlay；无需替换系统、启动内核、
boot DT 或调整 CMA。构建在外部机器完成，目标机只负责检查和安装。

**版本先选清楚：** 最新正式版是 v0.4.0，下面以它作为新设备安装示例。
当前 WanOS 已部署较新的完整开发运行包 `f581f98`，不要在它上面直接照抄首次安装
或降级。版本与模块哈希见[部署快照](rollout-status.md)。新版只完成热更新和
有界检查；新的冷启动、异常重启与回退实机验收仍由
[#6](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/6)追踪。

## 1. 安装前：在目标 OneCloud 检查

下文“目标机”命令都在 SSH 登录后以 root 执行；普通账号先用 `sudo -i`。
“外部主机”是下载/交叉构建的电脑。为目标机配置 SSH 别名 `onecloud`，或把
示例中的 `root@onecloud` 换成自己的入口，不在公开文档填写设备地址或密码。
除首次检查明确说明的“不存在”外，命令失败都先停止后续步骤并调查。

```sh
uname -r
tr '\000' '\n' < /sys/firmware/devicetree/base/compatible
df -h / /lib/firmware /root
df -i / /lib/firmware /root
command -v python3 bash realpath systemctl modprobe insmod rmmod tar sha256sum
systemctl status meson8b-vcodec.service --no-pager
ls -ld /opt/meson8b-vcodec /etc/systemd/system/meson8b-vcodec.service
awk '$1 ~ /^(meson8b_runtime|meson_vdec|meson_venc)$/ {print}' /proc/modules
```

预期内核为 `5.15.79-meson`，compatible 同时包含 `xunlei,onecloud` 和
`amlogic,meson8b`。首次安装时 unit、`/opt/meson8b-vcodec` 和三个模块均不应
存在；上述 `status`/`ls` 因不存在而报错是正常的。若已存在，改走后面的
升级流程，不能先删目录“解除”安装器的保护。

目标机需要 Python 3、Bash、systemd、kmod、coreutils 和 tar。Ubuntu/HiNAS
若确实缺少工具，在确认磁盘空间后安装相应系统包：

```sh
apt-get update
apt-get install --no-install-recommends python3 bash systemd kmod coreutils tar
```

存放包、`/opt`、固件和 state 的文件系统都要有空间及空闲 inode。至少为运行包
解压目录、安装副本、原文件备份、日志和短片输出留出余量；可用 `du -sh`
查看解压后大小，再核对 `df`。不要在已满盘上安装，也不要把备份放在 `/tmp`。
备份应位于开机可用的持久目录，并另存一份到外部机器。

安装、卸载和版本切换前，暂停 OpenList 硬件转码任务及其他持有 codec 的客户端；
业务服务可以继续运行，但此时不能提交新的硬件任务。用设备名称找到实际节点：

```sh
for name in /sys/class/video4linux/video*/name; do
    [ -f "$name" ] && printf '%s: %s\n' "$name" "$(cat "$name")"
done
# 把枚举到的本项目节点代入；若有 fuser，可检查持有进程，不加 -k。
fuser -v /dev/video0 /dev/video1
```

首次安装没有 video 节点时不需要运行 `fuser`。`video0/video1` 只是原验收设备
的编号，不是固定约定。通过应用正常取消任务，不强制 rmmod 或杀死无关服务。

## 2. 外部主机：下载并核对运行包

以下在驱动仓库的外部主机工作树执行，`build/` 为忽略目录。使用新的目录，
不要复用曾放过另一版本模块的目录。

```sh
version=v0.4.0
archive="onecloud-meson8b-runtime-$version.tar.gz"
mkdir -p build
mkdir "build/deploy-$version"
cd "build/deploy-$version"
release="https://github.com/YoungSx/onecloud-meson8b-vcodec/releases/download/$version"
curl -fL --retry 3 "$release/$archive" -o "$archive"
curl -fL --retry 3 "$release/SHA256SUMS" -o SHA256SUMS
awk -v archive="$archive" '$2 == archive || $2 == "./" archive' \
  SHA256SUMS > runtime.sha256
test "$(wc -l < runtime.sha256)" -eq 1
sha256sum -c runtime.sha256
```

Release 的外层 `SHA256SUMS` 同时列出源码包和 provenance；这里只取本次下载的
运行归档一项，不能因其他资产未下载而忽略运行包的校验失败。
源码包用于外部重建，不需要传到约 990 MiB 内存的 OneCloud 上编译。
从源码构建则按[构建指南](build.md)生成运行包，保留 `SOURCE_COMMIT` 和清单；
自定义包不使用这里的 v0.4.0 下载地址或文件名。

运行包的 Overlay provider phandle 必须匹配目标机运行树，同型号也要检查。
若目标检查报不匹配，应按构建指南从**未加载本项目 overlay 的**目标树生成 DTS，
在外部主机重建 Overlay、匹配包及清单；不要直接改解压包或只修改 manifest。
内核版本不同须按对应 ABI 重新移植/构建，不能只改 vermagic 强制加载。

## 3. 传输、解压与只读检查

继续在外部主机刚才的下载目录执行：

```sh
ssh root@onecloud 'mkdir -m 700 /root/codec-deploy-v0.4.0'
scp "$archive" runtime.sha256 SHA256SUMS \
  root@onecloud:/root/codec-deploy-v0.4.0/
# 随包准备短冒烟样片和逐帧比较工具，它们位于仓库中。
scp ../../samples/h264-320x240-smoke.mp4 ../../scripts/verify-hashes.py \
  root@onecloud:/root/codec-deploy-v0.4.0/
```

目录已存在时先核查其用途，再改用新的目录名，不覆盖旧备份。
然后在目标机执行：

```sh
cd /root/codec-deploy-v0.4.0
sha256sum -c runtime.sha256
mkdir runtime-package
tar -xzf onecloud-meson8b-runtime-v0.4.0.tar.gz -C runtime-package
python3 runtime-package/scripts/runtime-device.py check runtime-package
cat runtime-package/KERNEL_RELEASE runtime-package/SOURCE_COMMIT
```

`check` 不加载模块、不修改固件；检查包内全部文件的清单、内核版本、板型和
phandle。它不是图像正确性测试或完整 ABI 证明。失败时停止安装，按
[故障排查](troubleshooting.md)处理，不跳过保护。

## 4. 保存基线并安装自动加载

仍在目标机的部署目录。先记录当前内核、启动文件哈希，以及业务服务的访问
结果；业务地址/端口因环境而异，使用自己的健康检查，记录在私有目录中。
例如 OpenList 的当前 HTTP 状态、Home Assistant 和其他原有服务状态。

```sh
uname -r > kernel-before.txt
find /boot -type f -exec sha256sum {} + > boot-before.sha256
for file in /uImage /uInitrd /dtb/meson8b-onecloud.dtb; do
    [ ! -f "$file" ] || sha256sum "$file" >> boot-before.sha256
done
install -d -m 700 /root/codec-backups
# state 最后一级必须不存在，父目录须已存在，且不能位于 /opt/meson8b-vcodec 内。
state=/root/codec-backups/install-v0.4.0
bash runtime-package/scripts/install-autoload.sh runtime-package "$state"
```

成功时：`/opt/meson8b-vcodec/current` 保存安装副本，`state-path` 保存本次备份路径；
state 中记录原固件/Overlay及其哈希。systemd 启用并立即加载设备，后续开机自动
加载。它不替换原内核、boot DT、生产 FFmpeg，也不修改 OpenList 配置。

```sh
systemctl is-enabled meson8b-vcodec.service
systemctl is-active meson8b-vcodec.service
systemctl status meson8b-vcodec.service --no-pager
cat /opt/meson8b-vcodec/state-path
```

预期 `enabled`、`active (exited)`、退出状态成功；该 unit 是 oneshot，
`exited` 不表示驱动已经卸载。外部主机仍在第2节下载目录时，可另存本次state和
目标机基线；原运行归档已经保留在该目录。若自定义了state路径，相应替换：

```sh
# 外部主机执行，不是在OneCloud上执行。
umask 077
ssh root@onecloud 'cat /opt/meson8b-vcodec/state-path' > state-path.txt
scp -r root@onecloud:/root/codec-backups/install-v0.4.0 ./state-backup-v0.4.0
scp root@onecloud:/root/codec-deploy-v0.4.0/kernel-before.txt \
  root@onecloud:/root/codec-deploy-v0.4.0/boot-before.sha256 .
```

后面的安装后验收、暂停和卸载步骤仍在**目标机**部署目录执行。

安装脚本在最终 unit 启动失败时尝试回滚；不承诺所有中途断电、满盘或复制失败
都会自动收尾。任何失败都先保留 state、包、日志和现场，核对模块/unit/固件，
不能直接重复首次安装或手动删除 state。

## 5. 安装后：枚举、固件和短内容检查

```sh
journalctl -u meson8b-vcodec.service -b --no-pager -n 80
dmesg | tail -n 100
for name in /sys/class/video4linux/video*/name; do
    [ -f "$name" ] && printf '%s: %s\n' "$name" "$(cat "$name")"
done
/opt/meson8b-vcodec/current/tools/v4l2-ctl --list-devices
```

应看到 `meson-video-decoder` 和 `meson-video-encoder`，按名字选择它们的实际
节点，分别枚举格式/控件：

```sh
# 按上一步结果替换编号。
/opt/meson8b-vcodec/current/tools/v4l2-ctl -d /dev/video0 \
  --list-formats-out --list-formats --list-ctrls
/opt/meson8b-vcodec/current/tools/v4l2-ctl -d /dev/video1 \
  --list-formats-out --list-formats --list-ctrls
```

probe成功不代表微码已执行；某些固件到第一次STREAMON才加载。接下来在所有
其他codec客户端空闲时，用未修改的官方工具检查已传入的 **320×240、50帧**
H.264样片。以下仍在部署目录，选用宿主已有的官方完整FFmpeg/ffprobe；
不替换系统程序，也不默认假设包内工具功能相同。**v0.4.0包内工具没有H.264
编码器，且包含历史解码bug修复**；当前默认构建则为未修改官方源码，但仍是
裁剪功能的测试工具。先核对版本及能力，需要时使用另行准备的官方完整构建。
详见[FFmpeg适配](ffmpeg-compatibility.md)。

```sh
command -v ffmpeg ffprobe
ffmpeg=$(command -v ffmpeg)
ffprobe=$(command -v ffprobe)
"$ffmpeg" -version
"$ffmpeg" -hide_banner -decoders
"$ffmpeg" -hide_banner -encoders
"$ffmpeg" -hide_banner -muxers
# 确认有h264软件解码、h264_v4l2m2m解码/编码，以及framemd5/h264/null输出后继续。
"$ffmpeg" -nostdin -v error -filter_threads 1 -c:v h264 \
  -i h264-320x240-smoke.mp4 -map 0:v:0 -an -pix_fmt nv12 \
  -threads:v 1 -f framemd5 software.md5
"$ffmpeg" -nostdin -v error -filter_threads 1 -c:v h264_v4l2m2m \
  -i h264-320x240-smoke.mp4 -map 0:v:0 -an -pix_fmt nv12 \
  -threads:v 1 -f framemd5 hardware.md5
python3 verify-hashes.py software.md5 hardware.md5
"$ffmpeg" -nostdin -v error -c:v h264_v4l2m2m \
  -i h264-320x240-smoke.mp4 -an -c:v h264_v4l2m2m \
  -pix_fmt nv12 -b:v 0 -fps_mode passthrough -f h264 encoded.h264
"$ffprobe" -v error -select_streams v:0 -count_frames \
  -show_entries stream=codec_name,width,height,nb_read_frames -of json encoded.h264
"$ffmpeg" -nostdin -v error -c:v h264 -i encoded.h264 -an -f null -
dmesg | tail -n 100
uname -r
sha256sum -c boot-before.sha256
```

预期哈希比较为 `50/50`，编码结果H.264、320×240、`nb_read_frames=50`，软件
完整解码无错误；首次会话日志可核对固件加载/引擎启动，不能只凭包内有bin文件
判定成功。原始H.264不验证封装时长；帧率/时间线另按[使用指南](usage.md)验收。
本地包若未附v4l2-ctl，按[构建指南](build.md)先生成或使用系统版本。
JPEG/HEVC及更多格式按使用指南检查。

重新检查原有业务服务，确认安装前后正常；随后才恢复硬件任务。Docker 中按
实际设备节点配置 `--device`，权限与应用设置见[平台边界](portability.md)。
此短片是部署冒烟，不代替1080p完整回归、实时性或长期稳定性验收。
当前生产持续负载实验仍暂停；更新文档不意味着已完成新重启验收。

## 6. 暂停与恢复驱动

先正常结束codec客户端，再执行：

```sh
systemctl stop meson8b-vcodec.service
systemctl start meson8b-vcodec.service
```

`stop` 卸载项目模块和运行时Overlay，但保留安装文件、已启用的开机加载和
固件。临时停止不等于卸载，重启后仍会加载。若 `module busy`，检查设备持有者，
不要强制卸载；如果先前的stop失败，恢复前先核对模块状态，避免重叠加载。

## 7. 完整卸载并恢复本次安装前固件

先暂停硬件任务、确认设备无持有者。**移除脚本会删除 `/opt/meson8b-vcodec`**，
故先在目标机保存state路径，并将安装副本留在该目录之外：

```sh
state=$(cat /opt/meson8b-vcodec/state-path)
printf '%s\n' "$state"
test -f "$state/state.json"
test ! -e ./installed-package-backup
cp -a /opt/meson8b-vcodec/current ./installed-package-backup
bash /opt/meson8b-vcodec/current/scripts/remove-autoload.sh
```

`installed-package-backup`须为新的目录，复制前先确认空间；另存外部副本。
脚本禁用并停止unit，按encoder→decoder→runtime卸载，再根据state恢复固件；
成功后移除unit和安装目录，保留state。不卸载可能被其他驱动共享的vb2依赖。

卸载后逐项检查：

```sh
systemctl show meson8b-vcodec.service -p LoadState
# 预期 LoadState=not-found；旧journal记录仍会保留。
test ! -e /etc/systemd/system/meson8b-vcodec.service
test ! -e /opt/meson8b-vcodec
awk '$1 ~ /^(meson8b_runtime|meson_vdec|meson_venc)$/ {print}' /proc/modules
for name in /sys/class/video4linux/video*/name; do
    [ -f "$name" ] && printf '%s: %s\n' "$name" "$(cat "$name")"
done
```

模块查询应无输出，设备列表不再包含本项目两个名字；其他video设备可以保留。
用保留的state逐项确认原文件哈希恢复、新增文件移除：

```sh
python3 - "$state" <<'PY'
import hashlib, json, sys
from pathlib import Path
state = Path(sys.argv[1])
data = json.loads((state / 'state.json').read_text())
assert data['status'] == 'rolled_back', data['status']
for entry in data['files']:
    target = Path(data['firmware_root']) / entry['path']
    if entry['original'] is None:
        assert not target.exists(), str(target)
    else:
        assert hashlib.sha256(target.read_bytes()).hexdigest() == entry['original'], str(target)
print('PASS: pre-install firmware restored')
PY
uname -r
sha256sum -c boot-before.sha256
```

最后再核对原有业务服务。卸载后应用不能继续使用本项目硬件编码器，须按应用
能力切回软件或关闭转码；OpenList及其他业务配置不会被卸载脚本自动改写。

若固件在安装后被另行更新，回滚会拒绝覆盖；此时unit可能已被禁用，但安装和
state仍保留。先调查差异，不删除备份、不手动强改哈希。若仅unit清理阶段失败，
保留原包和state，参考[故障排查](troubleshooting.md)处理后再核查最终状态。

## 8. 升级或降级到旧版本

**卸载恢复固件，不自动恢复旧驱动安装。** 退回v0.4.0需要v0.4.0完整运行包、
对应固件/Overlay和新的安装state；把新模块删掉并不等于切回旧包。

对使用本安装器、具有完整一致state的标准安装，按以下顺序操作：

1. 取得目标版本包，完成外层/包内清单、目标内核和phandle检查。
2. 保存当前完整包、`SOURCE_COMMIT`、固件state、unit和启动/业务基线，另存外部。
3. 暂停硬件任务，按第7节完整卸载并确认恢复成功；任何一步失败都先停止切换。
4. 用目标版本的脚本安装，使用**新的、不存在的**state目录。例如在已核对的
   `older-runtime-package/`上执行：

   ```sh
   bash older-runtime-package/scripts/install-autoload.sh \
     older-runtime-package /root/codec-backups/reinstall-v0.4.0
   ```

5. 按第5节验证设备、内容和业务。若新版本失败，先核对失败清理是否完成；恢复
   保留的上一包时同样使用另一个新state，不复用已`rolled_back`的state目录。

不要改写`current/`里的模块、Overlay或SHA256SUMS来模拟升级；下一次校验会拒绝。
当前WanOS已通过标准卸载/全新state安装迁移到`f581f98`，保留独立的
原v0.4.0快照及升级前整包备份；本轮没有实机演练整包降级。按[部署快照](rollout-status.md)和
私有[迁移清单](workspace-migration.md)核对实际备份，新的实机回退验收仍待完成。

## 手动一次性安装（不注册unit）

仅用于没有systemd安装、三个模块也未加载的设备。先完成目标/包/占用检查：

```sh
python3 runtime-package/scripts/runtime-device.py apply runtime-package /root/manual-codec-state
# 结束客户端后恢复，保留state。
python3 runtime-package/scripts/runtime-device.py rollback /root/manual-codec-state
```

state最后一级必须不存在，位于持久存储且父目录存在。`apply`复制固件并加载，
不注册开机加载；`rollback`卸载并恢复原文件。不要与已安装unit混用。

## 开发用临时Overlay

以下是有救援入口的开发设备流程，不是普通部署或生产压力测试。不要为实验
覆盖规范固件路径的Overlay：异常重启可能使原安装校验失败。

配套新版runtime支持只读`overlay_file`参数，新加载器可为已校验包生成独立
临时Overlay；其余微码必须与已部署文件一致。主机清理和交叉构建通过，
异常重启尚未实机验收。

旧的设备特定 `dev-swap.sh` 已随试验脚本归档，不再作为安装入口。
在设备空闲、原服务已停且模块已卸载时使用新版
`runtime-device.py load CHECKED_PACKAGE --temporary-overlay`；测试后用该加载器
`unload`，核查清理结果，再启动原服务。此操作不安装自动加载或替换规范固件。
