# v0.2.0 运行包部署与 MPEG-2 帧尾复验

2026-11-15 在役机实测。部署本仓库 head `c20a473` 构建的运行包（含 drain
修复），复验 MPEG-2 帧尾，并回归 H.264/HEVC 解码与 H.264/JPEG 编码。

## 包与传输

| 项目 | 值 |
| --- | --- |
| 源码 commit | `c20a4732c4017a1ff9b30262b36f99d5a6491943` |
| 包 md5 | `07620dbec2c8e015b5728379444840d9` |
| meson-vdec.ko md5 | `ba46b218110109ab9d8a07050e7808a0` |
| meson-venc.ko md5 | `2507c7cc69fb1b00943fb21e2c615800` |
| meson8b-runtime.ko md5 | `75c8e3c2baf7290ce5c03bcdbd62dfe0` |
| 包内 `SHA256SUMS` | 52 个文件全部通过 |

包按 400 KiB 分块经 DERP 传输，11 个块逐块 md5 与本机清单一致后重组；
重组后的 `pkg-v020.tgz` md5 与本机相同，解包后包内清单全量校验通过。

## 部署

先停 `meson8b-vcodec.service`、`runtime-device.py unload`，再按升级路径
`remove-autoload.sh` 移除 cd4e6b5 安装，然后
`install-autoload.sh v020 codec-state-v020-install`。安装器校验包、内核、
板型与 overlay provider 后加载模块并启用 unit。

安装后 `meson8b-vcodec.service` active，`/dev/video0`、`/dev/video1` 均在，
`/opt/meson8b-vcodec/current/SOURCE_COMMIT` 为 `c20a473`。回滚备份在
`/root/venc-deploy/p6/codec-state-v020-install`。

## MPEG-2 帧尾

90 帧 `regress.m2v`（320×240，`ffmpeg -f lavfi testsrc2` 生成，md5
`68884da0f109b5bd8acabff23b122db3`），软件参考 `sw-m2v.nv12` md5
`750144f449877009421c3b8950ec67bf`。`FRAME_DUMP` 导出后用 `tight.py`
去掉 canvas 行填充（320 字节行宽，luma 131072 + chroma 65536 字节/帧）。

| 模块 | 数据帧 | 说明 |
| --- | --- | --- |
| 已安装 cd4e6b5 | 88 | 复现记录的帧尾丢失 |
| v0.2.0（drain 修复） | 90 | 与记录一致 |

90 帧逐像素与软件参考比对：第 0/1/45/89 帧 luma 平均绝对差
0.01–0.03，99% 以上像素完全相同，差异属解码舍入，不是缺帧或错帧。
两次独立运行（部署前后各一次）tight 输出 md5 均为
`f555b2b0373a7a052455c08d24c59391`，可复现。

`res-switch-test` 报 `EOS frames=98` 是因为它在 EOS 后把重复显示/陈旧
缓冲也计入；导出的 tight 数据为 90 帧，与软件参考帧数一致。

## 回归

| 项目 | 结果 |
| --- | --- |
| H.264 10 帧编码 | `PASS: 10 frames, ownership, busy format, timestamps, EOS` |
| JPEG 3 帧编码 | `PASS: 3 frames, ownership, busy format, timestamps, EOS` |
| HEVC 1080p 硬解 | `EOS frames=31 resolutions=1`，与既有记录一致 |
| 内核日志 | 测试后无 `Oops`/`BUG:`/`panic`/`error` |

## 未覆盖

- `/mnt/sda2` 仍未枚举，Docker、SublinkPro、VoCat 保持 inactive；这与
  codec 无关，需现场处理 USB 磁盘/供电。
- 本次未做 30 分钟/2 小时/过夜持续负载，P6 的 soak 项仍未完成。
- MPEG-4 尾两帧仍缺失，VC-1 无样本未验证，结论同
  [drain-eos.md](drain-eos.md)。
