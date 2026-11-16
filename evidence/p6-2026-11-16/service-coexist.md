# 五项业务服务并存验证（2026-11-16）

补上此前一直缺的服务共存一项：在役机 USB 磁盘恢复、五个业务服务全部起来后，
把编解码持续负载压在同一台机器上跑，同时按采样间隔探测每个服务的入口。

## 环境

内核 `5.15.79-meson`，运行包 `c20a473`（v0.2.0），模块 `meson-vdec` md5
`ba46b218110109ab9d8a07050e7808a0`。负载开始前（14:07）五个服务均在运行：

| 服务 | 形态 | 探测入口 |
| --- | --- | --- |
| OpenList | Docker 容器 | `http://127.0.0.1:5245/` |
| SublinkPro | 本机进程 | `http://127.0.0.1:8000/` |
| VoCat | 本机进程 | `http://127.0.0.1:7575/` |
| Home Assistant | Docker 容器 | `http://127.0.0.1:8123/` |
| Arcane agent | Docker 容器 | 无宿主端口，按其自身日志里 `Incoming request` 行数是否推进 |

Arcane 的容器镜像里没有 shell，也没有对外端口，只能用它自己的健康检查日志推进
作为"仍在服务"的证据；这只证明它在处理请求，不证明它的功能入口正确。

## 方法

脚本为仓库内 [scripts/service-coexist.sh](../../scripts/service-coexist.sh)：把
`soak.sh`（HEVC 1080p 解码逐帧比对、每五轮 H.264 1080p 解码、每轮 H.264 与
JPEG 编码）放到后台，每 20 s 探测上表四项 HTTP 入口并记录 Arcane 日志行数；
任一服务返回非 200、Arcane 计数不推进、或负载失败，都记为失败。

## 结果

```
COEXIST_DONE samples=71 fails=0 soak_rc=0 final: openlist=200 sublink=200 vocat=200 ha=200 arcane_hits=124
SOAK_DONE iter=377 FAILS=0 elapsed=1501s
```

- 71 次采样（25 分钟，20 s 间隔），四项服务**每次**都是 200，0 次不可达。
- Arcane 日志行数 107 → 124，持续推进，说明它在这段时间里一直在处理请求。
- 同期编解码负载 377 轮、0 失败：377 次 HEVC 30/30 逐帧匹配，75 次 H.264
  300/300，377 次 H.264 10 帧编码，377 次 JPEG 3 帧编码。
- 指标区间：温度 44–66 °C，CmaFree 24–46 MB（全程未耗尽），
  MemAvailable 429–498 MB，MemFree 128–261 MB；两个设备全程 `up`。
- 并存窗口（14:07–14:32）内 dmesg 无 warn/bug/oops/refcount/panic，也没有
  `can't match offset`。全程累计的 1469 条该消息全部来自 15:00 之后、本轮
  窗口之外的 MPEG-4 裸流探测（裸流无时间戳，驱动回填的 CAPTURE 时间戳全 0，
  该消息只在找不到匹配 offset 时打印，属既有行为，见
  [MPEG-4 验收](../RC-MPEG4-ACCEPTANCE-2026-11-13.md)的同类记录）。

原始日志：[service-coexist.log](service-coexist.log)（73 行，含基线与每轮采样）。

## 覆盖范围

- 25 分钟、377 轮是**有界**窗口，不是"长期并存无问题"的声明。
- 探测只打四个 HTTP 根路径与 Arcane 的日志推进，不验证各服务自身的业务功能
  （登录、索引、播放、自动化触发）。
- 负载是固定的两个 1080p 样片与 320x240 编码，不代表任意码流或任意并发。
- 内存余量在本轮偏紧（MemAvailable 最低 429 MB，CmaFree 最低 24 MB），
  这是五个服务与编解码共存时的真实余量，比单独跑编解码时（CmaFree 37–46）
  明显更小；若再叠加一个吃 CMA 或吃内存的服务，余量需要重新评估。