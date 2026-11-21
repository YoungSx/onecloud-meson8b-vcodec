# 码率修复实机回归 / Rate-control silicon regression

Linux 5.15.79-meson、S805、原 64 MiB CMA。源码修复为
`9843894791732b8fc07f9ee0b40398cd323d82d1`。仅在编码器空闲时临时替换
`meson-venc.ko`，每个测试脚本在 finally 恢复原模块。启动内核、设备树、
固件和生产 OpenList 配置均未改。

The S805 ran Linux 5.15.79-meson with its original 64 MiB CMA. Only the idle
encoder module was temporarily replaced; every script restored the installed
module in `finally`. The boot kernel, device tree, firmware and production
OpenList settings were preserved.

| 模块 / Module | SHA-256 |
| --- | --- |
| 原在役编码模块 / Installed encoder | `fa17e35d50f986d2eaf922590e99a44d385496956acfe64481a211b4a61c19e2` |
| 首轮矩阵 / Initial candidate | `794a8a9a81723d21fe4aaf92c803d96899d25b742beb2a122b987ffd77b06ee3` |
| 加预排队终止保护的最终模块 / Final candidate with terminal guard | `366d0653cb3f34a4d7f3c5d876463a28fab857e012ba9e8a991e5ac96bd916fe` |

首轮[18 项矩阵](matrix-initial.json)含原模块对照，另加三幅 JPEG；最终模块
[复测](matrix-final.json)覆盖全 I、同 fd 重启、分数帧率、编码中强制 IDR、
VBR 控件、有理数近似、溢出和后续恢复。成功 H.264 输出均经软件独立解码
并核对完整帧数。稳态数值丢弃开头 30 帧，不代表启动峰值。

The [initial 18-case matrix](matrix-initial.json) includes the installed-module
baseline and three additional JPEG pictures. The [final-module retest](matrix-final.json)
covers all-IDR, same-fd restart, fractional rates, in-flight force-key, VBR ABI,
period approximation, overflow and subsequent recovery. Successful H.264 outputs
were independently software-decoded and counted. Steady rates exclude the first
30 pictures and do not describe startup peaks.

| 内容与目标 / Content and target | 实测 / Observed |
| --- | --- |
| 640×360 static、GOP1、2.5 Mbps | 原稳态 2.900446 Mbps；最终 2.440734 Mbps |
| 640×360 motion、30000/1001、1.6 Mbps | 最终稳态 1.587866 Mbps |
| 同 fd 第 50 帧前 STREAMOFF/ON | 输出 100/100；第 50 帧 IDR，观测历史重新累计 50 帧 |
| motion、1.6→3 Mbps（第 75 帧） | 最后观测窗口 2.972516 Mbps；混合两目标的全段平均不是新目标误差 |
| 已运行 IDR 中再 force-key | 第 1/2 帧都为 IDR，请求未被完成回调丢弃 |
| 720p static、1/2/4 Mbps | 稳态约 1.201/1.763/3.676 Mbps |
| 1080p synthetic motion、1/2/4/8 Mbps | 约 10.35 Mbps、QP32 饱和，目标不可达 |
| 720p scene、1/4 Mbps | 约 7.65 Mbps、QP32 饱和，目标不可达 |
| 1080p noise | 第三次溢出终止队列；后续新会话正常输出 10/10 |
| 请求帧周期 1001/120000 | 返回 342/40999，误差低于 0.001%；241 fps 被拒绝 |

不可达是对应合成内容的结果，不是所有影片的固定下限；本轮没有质量/PSNR
矩阵，不能承诺更低码率的画质。debugfs 的 peak 包含启动不足一秒的窗口。

Unreachable rates belong to these synthetic samples, not all films. This round
did not establish a quality/PSNR matrix. Debugfs peak measurements include startup
windows shorter than one second.

## 预排队终止与失败实验 / Queued termination and failed experiments

直接客户端预排队四幅 1080p 噪声帧，结果为三次溢出、终止队列错误，两个
STREAMOFF 都成功，内核未继续第四次硬件尝试，见[结果](direct-queue.json)。
复现时先结束其他 codec 客户端，在外部构建后将工具复制到设备：

```sh
arm-linux-gnueabihf-gcc-11 -O2 -Wall -Wextra -Werror -static \
  tests/overflow-queue.c -o overflow-queue
# Run on S805 with the candidate encoder loaded:
./overflow-queue /dev/video1
cat /sys/kernel/debug/meson-venc/rate_control
```

The direct client queues four oversized 1080p pictures before STREAMON. It
observed terminal queue error after three overflows, both STREAMOFF calls
succeeded, and no fourth hardware overflow occurred; see [results](direct-queue.json).

原 FFmpeg 7.0.2 的 raw NV12 多缓冲实验在硬件启动前发生用户态 SIGSEGV，
[单独保留](ffmpeg7-raw-input-failure.json)，不算编码成功或驱动队列验收。
记录中的 debugfs 数值属于前一个会话，不能拿来验证失败的 FFmpeg 实验。
根因本轮未定位，生产 FFmpeg 未替换。

The original FFmpeg 7.0.2 raw-input experiment exited with user-space SIGSEGV
before hardware encoding. Its [failure](ffmpeg7-raw-input-failure.json) is separate
from driver acceptance. The debugfs values came from the prior session and are
not evidence for that tool run. Its cause remains undetermined; production
FFmpeg was preserved.

首轮 1.6 Mbps 全 I 目标在该内容下不可达，初始检查将它当成反馈失败；后来
改为原模块对照与可达的 2.5 Mbps。首帧取帧前就到达的 force-key 也不能
证明“IDR 编码中”请求语义，最终测试改在已运行的第 1 帧 IDR 发请求。
原始失败记录留在构建目录和设备测试目录，没有删除或改写成成功。

An initial all-IDR test used an unreachable 1.6 Mbps target, then was corrected
to the baseline comparison at 2.5 Mbps. A force-key sent before first-picture
selection did not test an in-flight request; the final check sends it during
an already running IDR. The original experiment records remain in the build
and device test directories.

主机测试、ASan/UBSan、NEON 对照与[完整内核/模块构建](kernel-build.log)通过，
见[主机记录](host-tests.log)。最终窗口内仅新增预期固件加载/溢出/恢复日志，
没有 Oops/BUG/KASAN。OpenList、SublinkPro、VoCat、Home Assistant 的 HTTP
均为 200，Arcane 运行；生产 OpenList 启动时间未变、RestartCount=0，原
编码模块恢复。此证据是有界回归，没有新增长时 soak 或浏览器验收。

Host tests, ASan/UBSan, NEON comparison and the [full kernel/module build](kernel-build.log)
passed; see [host results](host-tests.log). The final window contains expected
firmware/overflow/recovery messages and no Oops/BUG/KASAN. All four service HTTP
probes returned 200 and Arcane remained running. Production OpenList's start
time stayed unchanged with restart count zero, and the original encoder was
restored. This is bounded regression, not a new soak or browser acceptance.
