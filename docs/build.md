# 外部构建与源码补丁

目标为 ARMv7 hard-float 和原版 `5.15.79-meson`。在外部 Ubuntu 24.04
x86_64/ARM64 主机执行，不在约 990 MiB 的生产设备上构建。

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

工具版本、源码哈希和选项固定在构建脚本。FFmpeg 为独立文件测试构建，
包含 HEVC M2M/drain 修复，没有 H.264 硬件编码包装。v4l2-ctl 为静态 ioctl
工具。所有构建日志与产物位于忽略的 `build/`，CI 将上传交付归档。

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
`CONFIG_VIDEO_MESON8B_VCODEC=m`。`patches/research/` 为早期存档，不应用到
当前实现；它已改为以主线 v7.3.4 为基准，仅用于编译级移植验证
（见[移植记录](../evidence/mainline-2026-11-16/PORT.md)）。未验证新的启动
内核；运行包只安装模块/固件，不改 `/boot`。

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
