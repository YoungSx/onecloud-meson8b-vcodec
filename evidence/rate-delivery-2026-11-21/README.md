# 峰值/缓冲兑现验收 / Peak and buffer acceptance

源码 / Source: `00749671b7f0f1887f323cfadb278ea6f29aad04`，Linux 5.15.79-meson，
S805，原 64 MiB CMA。候选编码模块 SHA-256：
`e67a83367410c859f44151c774b2290124ca1f5eb3751c1161350556a92d84e8`。
FFmpeg 7.0.2 工具 SHA-256：
`b5548a4b971aeb23db6067392b99ef1d9196d1dfefadc86c4065fffecbd3a175`。

[首轮](matrix-initial.json)验证有界 640×360 编码、低预算失败、无效峰值、
新会话恢复，以及共享 raw NV12 的固定/有界 FFmpeg 编码。
[复测](matrix-final.json)独立软件解码并核对帧数，检查所有连续 AU 区间预算，
验证 FFmpeg 超预算非零退出、动态目标/容量、同 fd 重启、无效控件事务和 JPEG。
每个脚本在 finally 恢复原编码模块；原启动内核、DT 和生产容器未改。

The [initial matrix](matrix-initial.json) covers bounded encoding, an impossible
budget, invalid peak, a new session after failure and shared raw NV12 input.
The [retest](matrix-final.json) independently decodes/counts output, checks every
contiguous access-unit interval, and verifies FFmpeg failure, retargeting,
capacity changes, same-fd restart, invalid transactions and JPEG. Scripts restore
the installed encoder in finally; boot files and production containers are preserved.

| 验证 / Check | 结果 / Result |
| --- | --- |
| 640×360，30000/1001，目标 1.6M、峰值 2M、CPB 256 KiB | 150/150 帧；11325 个连续区间符合预算 |
| raw NV12，30 fps，无预算与 2M/244 KiB 预算 | 两次均 90/90 帧；有界输出 4095 个连续区间符合预算 |
| FFmpeg：1M 峰值、1 KiB CPB | 0.358 s 内非零退出；无输出；违例 1、terminal 1 |
| 1.6→1.2M 目标、2→1.6M 峰值、128→256 KiB；第 120 帧重启 | 180/180 帧；新会话统计 60 帧、违例 0 |
| 不完整 peak/CPB 事务与 peak 低于 target | 被拒绝；后续固定 QP 正常输出 15/15 帧 |
| JPEG 带极小 H.264 CPB 控件 | 5/5 帧；H.264 预算未改变 JPEG 行为 |

主机 C/Python 测试和 ASan/UBSan 通过，见[日志](host-tests.log)。完整匹配
内核/模块和独立 FFmpeg 已在外部交叉构建。该结果是媒体时间 VBR 包络，
不是严格 CBR、HRD 或网络发送限速；未增加画质/PSNR 矩阵。

Host C/Python and ASan/UBSan checks pass; see [results](host-tests.log).
The matching full kernel/modules and independent FFmpeg cross-build externally.
These results establish a media-time VBR envelope, not strict CBR, HRD or
network pacing. No new quality/PSNR matrix is claimed.

## 持续负载 / Sustained load

[30 分钟结果](sustained.json)：1808.014 s，26 轮，
78000 帧均独立软件解码和计数，117039000 个连续区间符合预算，
每轮违例及 terminal 均为零。四项 HTTP 和 Arcane 共 55 次
采样均正常；原模块恢复，生产容器启动时间和 RestartCount 未变。
[验收脚本](sustained-test.py)保留固定输入、限制和恢复逻辑。

The [30-minute run](sustained.json) lasted 1808.014 s over
26 blocks. All 78000 pictures were independently
decoded and counted; 117039000 contiguous intervals met the envelope. Every
block had zero violations and terminal errors. All 55 service
samples passed. The original encoder was restored; production start time and
restart count remained unchanged. See the [harness](sustained-test.py).
