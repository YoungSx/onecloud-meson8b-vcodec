# MPEG-2 B 帧流交付帧数：149 或 150

2026-11-17 在役机 v0.3.0 包（`0a0542b`）实测。本记录修正
[drain-eos.md](../p6-2026-11-15/drain-eos.md) 中「150 帧 B 帧流返回全部
150 帧」的单次样本结论：同一输入重复运行并不固定返回 150 帧。

## 结论

- 150 帧 B 帧流 `m2v-bf.m2v` 在两批完整落盘的重复运行中共 20 次，14 次
  交付 150 帧，6 次交付 149 帧。不是确定性结果。
- 短输出缺的是显示序 148，即解码序最后一幅（temporal_ref 6 的 B 帧）；
  它之前解码的 P 帧（显示序 149）仍然交付。149 帧输出的前 148 幅与软件
  0–147 在位对应，第 149 幅是软件 149；luma 平均绝对差 0.016–0.019，与
  90 帧流同级的舍入差，没有中间缺帧或错帧。
- 新增的 `esparser: EOS write … drain terminator not delivered` 在整个 B 帧
  测量窗口内没有出现，EOS 写入本身送达了解析器。
- 在流尾追加数据能让这幅不再丢，但会换来不定数的多余帧：追加一个帧头
  （`h1`）或最后一个编码图像（`last1`）的 16 次运行没有一次少于 150，
  其中 8 次为 151；逐幅核对的一次 151 帧输出里，前 150 幅在位，第 151 幅
  是软件 149 的重复（luma MAD 0.175，正常在位帧为 0.02 左右）；只追加
  序列结束码（`vos`）与基线相同（5 次 150、3 次 149）。
- 驱动的 `eos_sequence` 已在序列结束码后附一个帧头，用来让固件完成解码序
  最后一幅。流内多一个帧头即可稳定出这幅，说明 EOS 路径上的那个帧头有时
  没让固件完成它，或完成晚于 `codec_mpeg12_drain_work()` 的判据
  （IRQ 静默 ≥100 ms 且队列 ≤1 后 flush）。两种情况本轮没有区分；与
  [m4v-tail.md](../p6-2026-11-16/m4v-tail.md) 的 MPEG-4 帧尾同属 drain
  释放问题，驱动不改。

## 方法

- 输入 `m2v-bf.m2v`（320×240，150 幅，md5 `aeecec1bc5079c77efe7e585e283dde9`；
  解码序尾部为 temporal_ref 7 的 P、temporal_ref 6 的 B）。
- 软件参考 `sw-m2v-bf.nv12`，tight 布局 115200 字节/幅。
- 采集用 `FRAME_DUMP` 导出 NV12M，canvas 布局 196608 字节/幅
  （luma 131072 + chroma 65536），幅数 = 字节数 / 196608。逐次计数用
  [m2v-bf-delivery.sh](../../scripts/m2v-bf-delivery.sh)。
- 定位用 [m2v-frame-order.py](../../scripts/m2v-frame-order.py)：去掉
  canvas 行填充后按可见 luma（隔 4 字节取样）在 ±2 位置内找最近的软件
  图像，打印不在原位或差异超过舍入的帧。
- [m4v-frame-compare.py](../../scripts/m4v-frame-compare.py) 的 8×8 网格
  整数均值在这条流上分不开相邻的 148 与 149（两者 delta 都是 0），会把
  缺 148 误报为 `missing_ref=[149]`；[m2v-bf-classify.log](m2v-bf-classify.log)
  保留了该误报，定位以 luma 比较为准。
- 导出文件写在 `/root` 下，不用 `/tmp`：`/tmp` 是 496 MiB tmpfs，满盘时
  dump 被截断，计数与丢帧无法区分。当天 17:09 前后读到的「128 帧」就是
  tmpfs 满盘的截断；写在 `/tmp` 的早期复测因此不计入下表。

## 结果

### 重复运行

| 批次 | 运行数 | 150 帧 | 149 帧 |
| --- | --- | --- | --- |
| [m2v-bf-delivery.log](m2v-bf-delivery.log) | 12 | 9 | 3 |
| [m2v-bf-tail-ab.log](m2v-bf-tail-ab.log) 的 base | 8 | 5 | 3 |
| 合计 | 20 | 14 | 6 |

### 缺哪一幅

三次 149 帧输出（`short-1`…`short-3`）与 tail A/B 的一次 149 帧输出都是同一
结果，见 [m2v-bf-order.log](m2v-bf-order.log)：

```
short-1 hw_frames=149 ref_frames=150 not_in_place=[(148, 149, 0.017)]
hw146->sw146(0.017) hw147->sw147(0.016) hw148->sw149(0.019) next sw148(2.678)
```

150 帧输出全部在位；151 帧输出前 150 幅在位，第 151 幅最接近软件 149：

```
run hw_frames=150 ref_frames=150 not_in_place=[]
extra hw_frames=151 ref_frames=150 not_in_place=[(150, 149, 0.198)]
```

### 追加流尾的 A/B

每变体 8 次，原始记录见 [m2v-bf-tail-ab.log](m2v-bf-tail-ab.log)。

| 变体 | 追加内容 | 149 帧 | 150 帧 | 151 帧 |
| --- | --- | --- | --- | --- |
| base | 无 | 3 | 5 | 0 |
| last1 | 最后一个编码图像 | 0 | 6 | 2 |
| h1 | `00 00 01 00 00 08 00 00` | 0 | 2 | 6 |
| vos | `00 00 01 B7` | 3 | 5 | 0 |

## 对既有记录的修正

[drain-eos.md](../p6-2026-11-15/drain-eos.md) 的「150 帧返回全部 150 帧」
来自单次运行；正确表述是 149 或 150，短输出缺解码序最后一幅 B 帧（显示序
148），其余帧与软件参考在位一致。