# 剩余工作与已知限制（2026-11-13 更新）

原研究轮的阻断项进展如下。历史版本（STREAMON 返回 -EOPNOTSUPP 的阶段）的
完整分析保留在外层研究目录 BLOCKERS.md，此处只记录当前状态。

## 已解决

- **Meson8b canvas 字节序**：采用研究阶段列出的方案 2+4 —— 硬件解码写入独立的
  shadow `dma_alloc_coherent` 缓冲（不动仍被运动补偿引用的参考帧布局），
  帧就绪后 CPU 8 字节字节序交换拷贝到 vb2 buffer（`vdec/meson8b-capture.c`、
  `vdec/meson8b-copy.h`）。1080p 逐帧 MD5 与软解一致已验证正确性。
  该路径**不是零拷贝**，CPU 占用实测计入对比数据（见 `evidence/cpu-bench.json`）。
- **微码加载与 H.264 协议**：厂商 M8 微码 + staging H.264 codec 在实机可正常
  probe、加载、解码 1080p 300 帧、卸载无内核告警。厂商复位序列已补
  （研究阶段发现的处理器复位顺序问题）。
- **设备树接入**：不换内核/DT，运行时 overlay 注入（`runtime/meson8b-runtime.ko`），
  可完整移除。目标机在役服务全程正常。

## 未完成

1. **H.265 / MPEG-1/2 等其他 codec**：仅实现 H.264。固件提取脚本只覆盖 H.264 微码。
2. **分辨率切换 / EOS / 错误恢复 / 长时稳定性**：单文件固定分辨率 1080p 已验证；
   多文件反复 open/close（研究轮 smoke 循环做过数次）不等于压测。
   B 帧 1080p 样片（`samples/` 外层目录 h264-1080p-bframes.mp4）未跑完整验收。
3. **编码（amvenc_avc）**：完全未实现。主线无任何 Amlogic V4L2 encoder，
   厂商 3.10 `encoder.c` 是私有设备 + 命令队列接口，需要独立移植
   HCODEC 平台供电/时钟、DMA/参考帧、头生成、码率控制、V4L2 encoder 控件、drain。
   参考资料已归档：`evidence/vendor-avc-protocol.txt`、`evidence/source-excerpts/`。
4. **零拷贝输出路径**：GE2D 转换（研究轮方案 3）未做。当前 CPU 拷贝在 1080p
   下吞吐 25fps（软解 10fps），若要零拷贝需评估 GE2D YUV 格式支持或
   VDEC 写端字节序控制的厂商依据。
5. **通用化**：overlay phandle 绑定特定目标机的活设备树；其它机器需重新生成。

## 生产化前提

- 上游化路径：把本仓库的 Meson8b 平台数据/capture 软转换整理成可提交
  drivers/staging/media/meson/vdec 的补丁（当前版本针对 5.15.79 交叉编译模块）。
- 每次内核小版本升级需重新交叉编译两个模块（vermagic 绑定 5.15.79-meson）。
