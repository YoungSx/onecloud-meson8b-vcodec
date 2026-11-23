# 外部构建与源码补丁

目标为 ARMv7 hard-float 和原版 `5.15.79-meson`。在外部 Ubuntu 24.04
x86_64/ARM64 主机执行，不在约 990 MiB 的生产设备上构建。

## 从新工作区开始

```sh
git clone https://github.com/YoungSx/onecloud-meson8b-vcodec.git
cd onecloud-meson8b-vcodec
```

构建仅依赖仓库和脚本固定的公开输入，不依赖旧工作区路径、SSH或原机headers。
离线重建和精确部署二进制的保留方法见[工作区迁移](workspace-migration.md)。
构建/测试日志写入忽略的 `build/`，逐次报告放到 `build/reports/`；不得提交
输出日志、视频或临时部署脚本，详见[仓库规则](repository-policy.md)。

## 依赖与源码

```sh
sudo apt-get update
sudo apt-get install -y gcc-11-arm-linux-gnueabihf g++-11-arm-linux-gnueabihf \
  gcc-11-plugin-dev-arm-linux-gnueabihf build-essential flex bison bc \
  libssl-dev libelf-dev device-tree-compiler kmod curl xz-utils bzip2 \
  pkg-config shellcheck
```

Linux 官方 5.15.79 tarball SHA-256 为
`cba39031dbc0eed0785b8afdc8c58cf23df83e47001b2354fa44486ae699c154`。
[Armbian 基线补丁](../baseline/0001-armbian-onecloud-5.15.79.patch)哈希为
`3d2b062d672124b798fd3379fd53a28ce8563e86528f9643306d6cfde415d33f`。
它重建 hzyitc/armbian-onecloud `ci-20221118-033748-UTC` 的源码线，Armbian
提交 `d01e3519a87ab6a3b8df3f71da81d7040137b49f`。

## 一次构建

```sh
make test
JOBS=4 bash scripts/build-kernel.sh
JOBS=4 bash scripts/build-ffmpeg-test.sh
JOBS=4 bash scripts/build-v4l2-ctl.sh
make dtbo
```

内核脚本下载并校验固定源码，应用基线与当前 additive 补丁，使用
[原配置](../config/target-5.15.79-meson.config)完整构建 `vmlinux modules`，
然后构建外部模块和补丁目录，检查 vermagic、模块依赖、补丁/源码同步。
默认构建目录 `build/kernel/`。仅 `modules_prepare` 不生成 `Module.symvers`，
不能代替导出符号与依赖检查。原配置的 `CONFIG_MODVERSIONS` 未开启。

已有匹配的完整内核输出目录时，也可直接：

```sh
KDIR=/absolute/path/to/kernel-output make modules -j4
```

工具版本、源码哈希和选项固定在构建脚本。FFmpeg 默认为未修改的官方
源码测试构建，包含 H.264/HEVC M2M、AAC、HLS/MPEG-TS 和
文件/HTTP；没有 HTTPS/TLS、libx264/libx265 或完整格式覆盖，保留生产
FFmpeg。v4l2-ctl 为静态 ioctl 工具。所有构建日志与产物位于忽略的
`build/`，CI 将上传交付归档。

只有复现明确 bug 时才使用 `bash scripts/build-ffmpeg-test.sh --bugfixes`：
它只应用 drain 与缓冲/错误修复，没有私有选项或峰值映射。产物记录
`BUILD_VARIANT`，运行包附 `FFMPEG_BUILD_VARIANT`；旧的私有码控实验
不再作为兼容或发布基线。详见 [官方 FFmpeg 适配](ffmpeg-compatibility.md)。

## Overlay

默认 [meson8b-runtime.dts](../meson8b-runtime.dts)是原验收设备快照。
其他设备或设备树先在目标机生成新的 DTS，再在外部主机编译并替换构建输入：

```sh
python3 scripts/make-overlay.py --tree /sys/firmware/devicetree/base --output my.dts
# 外部主机
cp my.dts meson8b-runtime.dts
make dtbo
```

安装器校验 phandle。[配置片段](../config/runtime-codecs.config)和
[patches/series](../patches/series)用于同样实现的源码树集成，入口为
`CONFIG_VIDEO_MESON8B_VCODEC=m`。`patches/research/` 是单独的Linux v7.3.4研究系列，包含独立帧池和现有解码格式，
不应用到5.15运行包；仅完成编译级移植验证
（见[移植记录](research-validation.md)）。未验证新的启动
内核；运行包只安装模块/固件，不改 `/boot`。

主线系列的可复现检查需要一份未打补丁的内核源码树：

```sh
bash scripts/check-mainline-port.sh /path/to/linux-7.3.4
```

脚本只把系列涉及的文件复制到临时目录，按序以 `patch -p1 -F0` 应用并对每个
补丁运行 `checkpatch.pl --strict`，不修改源码树本身，也不需要交叉工具链。
源码树必须带 `scripts/checkpatch.pl`，否则脚本直接失败而不是跳过风格检查。
模块与 DTB 的实际编译见移植记录，两者都不代表 S805 硬件可用。

"未打补丁"要自己保证。脚本不会检查，所以一个此前已经应用过研究系列的树会报
`Reversed (or previously applied) patch detected`，看起来像系列本身坏了。重新
从 tarball 解一份最省事：

```sh
tar xf linux-7.3.4.tar.xz -C /tmp/new     # 而不是复用旧的构建树
```

## 打包和来源

```sh
bash scripts/package-runtime.sh build/runtime-package
bash scripts/package-source.sh build/source-package
python3 scripts/verify-package.py build/runtime-package
```

运行包包含模块、全部固件、Overlay、双语 README、使用/回滚文档、配置、
当前补丁及独立工具。源码包使用提交归档，附原 Linux/FFmpeg/v4l-utils
源码 tarball、固定微码源与测试样片，记录 `SOURCE_COMMIT`。
默认打包当前提交；需为已验收旧模块补源码时，在完整 Git 历史上指定：

```sh
bash scripts/package-source.sh build/old-source-package bd1cb79ad502d0b8cdcf9667a0ceb0ca0f7876ab
```

归档源码无需 Git：解压 source-package，将 `sources/` 复制到 `source/`，
在 `source/` 内运行相同脚本。不要将新脚本、旧固件和另一提交的模块混为
一个已验收版本。历史产物哈希与复現记录见[部署快照](rollout-status.md)。
