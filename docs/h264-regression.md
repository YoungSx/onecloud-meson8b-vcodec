# H.264 少帧时序回归

本轮从合并后的 `bd47ef8` 建立工作机回归；`vdec/`、`venc/`、`runtime/`
与部署源码 `3c7d785` 字节一致。没有修改或部署驱动。
[历史 299/300](validation-records.md#历史少帧与持续负载)的根因仍未确定，
[#5](https://github.com/YoungSx/onecloud-meson8b-vcodec/issues/5)继续开放。

## 工作机重放

```sh
python3 -m unittest discover -s tests -p test_h264_timing.py -v
make test
```

[测试入口](../tests/test_h264_timing.py)在编译时提取当前实际驱动函数，
[C 测试边界](../tests/h264-timing.c)替代 MMIO、内核服务、固件图片及转换。
提取的路径包括硬 IRQ/线程完成、H.264 ready 批次、真实 offset/PTS 链表、
图片结束标记、帧池队列/交付、CAPTURE 完成和 LAST 出队。
测试使用 ASan/UBSan，不复制一份驱动状态机作为预期答案。

每次重放输出 `build/host-tests/h264-timing.log`，包含场景参数、时刻、
IRQ 完成数、帧池长度、槽号、PTS、几何、copy/DQBUF 数及 LAST/EOS。
失败保留断言和完整场景输出；相同参数对应相同调度步骤。`make test`
通过 Python discovery 自动运行它，不需要新依赖或 CI job。

| 场景 | 断言 |
| --- | --- |
| 72 组 300 图片：CAPTURE 1/4/10，IRQ 批次 1/3/6，8 个固定调度种子 | IRQ→copy→CAPTURE→DQBUF 各 300；身份与 PTS 连续；尾部解码序按 PTS 交付；恰好一个 EOS，在 LAST 出队后出现 |
| 转换期间另一个 3 图片 IRQ 批次到达 | 6 图片全交付，转换中的槽仍 waiting，不提前回收 |
| IRQ 线程延迟 250 ms | STOP 等线程完成再等 50 ms 安静窗口；完成等待时图片可以尚未 copy，不能因此丢弃帧池 |
| 旧尺寸尾图已排队，再收到新尺寸 source change | 旧图保留自己的几何；source-change LAST 重置序列但不产生 EOS |
| 已排图片、尚未转换时取消，再建立新会话 | 取消丢弃 waiting 图片、错误归还 CAPTURE；重开无旧图/旧 EOS，随后完整 300 图片 |
| 去掉活跃 IRQ 判断、完成时刻更新、PTS 排序或提前发送 EOS | 四种单独故障注入都必须触发断言，避免测试只验证计数自增 |

这里的图片身份是合成值，**不是 H.264 解码像素**。PTS 倒序尾批测试对应
B 帧交付排序，不模拟固件 DPB、运动补偿或参考帧内容。单线程按步骤调度
覆盖确定的交错，不证明真实 SMP 锁/内存序、vb2 poll 行为或硅片 DMA。

## 与已有修复的对应关系

| 机制 | 已有修复 | 回归及仍缺证据 |
| --- | --- | --- |
| 硬 IRQ 已到，但线程处理未结束，STOP 误判安静 | `a82bc99` 跟踪 `irq_active`，在线程完成时更新时间，并约束等待上限 | 新整链延迟场景检出忽略活跃标记/完成时刻两种退化；原 299/300 没有 IRQ 时序，不能建立因果关系 |
| 发布 EOS 时 LAST 仍未出队，事件优先的客户端停止取帧 | `3b5adc2` 将 EOS 移到专用 LAST 的 `buf_finish` | 新整链覆盖图片 LAST 与回压；现有 `test_decoder_event_order.py` 覆盖空 flush LAST 和重复 finish。提前发事件注入必失败 |
| CAPTURE 数量小于固件帧池需求；copy 阻塞 IRQ 或槽过早回收 | `8d0b461` 独立帧池，`b29b466` 异步交付，`e90469f` 分离记账/交付锁 | 新整链覆盖队列回压及 copy 中到达 IRQ；`test_pool.c` 与 `test_pool_copy_lock.py` 覆盖槽持有。尚无当前包真实固件长测证据 |
| EOS 尾批按解码序返回，B 图 PTS 错排 | `45e8d1b` 在 STOP 时按已有 PTS 排序 | 新尾批覆盖 1/3/6 图片；**该修复早于 11-16 少帧现场**。仓库历史样片为 Constrained Baseline、0 B 帧、1 reference、300 帧，不能把 B 帧排序作为其根因 |
| 分辨率切换后旧 recycle 槽号释放新表 | `fd4f2bb` 暂停回收、清 pending 及邮箱，再启用新表 | 现有 `test_h264_generation.py` 执行实际 resume；新整链仅覆盖 source-change LAST/排队几何。新旧表切换的真实固件内容仍需实机 |
| #23 的 HEVC stop、HCODEC DMA 与 SPS/PPS 长度错误 | 合并后 `16c0d9b`；部署源码为原分支 `3c7d785` | 三项定向测试及有界硬件验收已完成；它们不修改 H.264 解码路径，不作为 #5 修复证明 |

## 仍需实机完成的 #5 条件

历史记录只有软件 300/硬件 299 的输出长度，未在失败现场对齐硬件完成、
交付、出队与 EOS，也没有确定少的是哪一幅。当前主机结果只能锁定已知
软件机制的覆盖，不能关闭历史调查。

恢复取证与救援条件后，复用[直接 V4L2 客户端](../tests/res-switch-test.c)，
先完成以下准备再执行有界检查：

1. 为客户端补单调时刻与 QBUF/DQBUF/DQEVENT/STOP/STREAMOFF 记录、CAPTURE
   延迟归还；B 帧输入需从工作机给出每 AU 的真实 PTS。现有 raw ES 客户端
   没有这些 PTS，不能拿它直接验收 B 帧呈现顺序。
2. 先使用已有驱动 debugfs/picture 日志，核实能否逐幅关联 IRQ 槽、offset/PTS、
   pool、CAPTURE。若缺少关联点，再考虑可关闭的内核 trace event；主机日志
   不冒充设备采样。日志存到独立工作机并保留失败现场。
3. 工作机准备 Baseline 300 帧、B 帧/多参考流及尺寸切换输入和逐帧软件参考；
   记录输入、模块、固件、Overlay 哈希及内核。执行 CAPTURE 1/4/10、有界回压、
   尺寸切换、两种 STREAMOFF 顺序/重开，比较实际完整帧数和内容。
4. 按 IRQ 少帧、帧池丢弃、CAPTURE 完成后未取走、LAST/EOS 顺序分类；
   若仍不可复现，报告未建立因果关系，不补假尾帧或重复图片。

生产持续负载仍暂停。性能拆分随后推进，格式扩展继续排后。
