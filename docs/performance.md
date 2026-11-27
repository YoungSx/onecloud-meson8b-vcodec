# 阶段测量与 GE2D 验证

性能目标使用固定输入、完整帧数、相同质量与应用原命令。驱动计数记录
转换/拷贝的累计耗时，不能相加后当作总耗时；硬件、CPU 与工作队列会重叠。
本机历史数据不能代替候选模块的测量。

## 可复用测量入口

在具备取证/恢复条件的测试设备上，对解码、缩放、编码及完整命令分别运行
[measure-pipeline.py](../scripts/measure-pipeline.py)。脚本直接执行参数列表，
不经过 shell，不加载模块、不修改服务。输出必须是新文件；先运行命令，再
独立软件解码并核对完整帧数，超时或短输出失败。可提供相同 NV12 格式的
软件 framemd5 参考，进一步核对每幅图像；有损编码输出应另外比较 PSNR，
不能要求与输入哈希相同。

```sh
python3 scripts/measure-pipeline.py --stage decode \
  --report build/reports/decode.json --output build/reports/decode.nut \
  --frames 300 --media-seconds 10 --reference reference.md5 -- \
  ffmpeg -nostdin -v error -filter_threads 1 -c:v h264_v4l2m2m -i input.mp4 \
  -map 0:v:0 -an -pix_fmt nv12 -threads:v 1 -c:v rawvideo \
  -fps_mode passthrough build/reports/decode.nut
```

JSON 记录真实 argv、命令墙钟时间、子进程 CPU、采样到的最小普通内存/CMA、
decoder `convert_ns` 和 encoder `copy_ns` 等计数差值。软件验图在计时区间外，
失败日志与报告保留在指定报告目录。设备计数是全局值，其他会话会污染差值；
并存业务负载需单独记录。输出容器、磁盘 I/O 也在命令耗时内，不称为纯硬件耗时。
音频阶段可用原版 FFmpeg 的 `-benchmark` 单独测量，再用完整命令检查音视频
时间线。需要验收音视频输出时，增加`--audio-seconds`指定预期音频时长：

```sh
mkdir -p build/reports/av-hls
python3 scripts/measure-pipeline.py --stage av-hls \
  --report build/reports/av-hls/result.json --output build/reports/av-hls/index.m3u8 \
  --frames 300 --media-seconds 10 --audio-seconds 10 --timeline-tolerance 0.05 -- \
  ffmpeg -nostdin -v error -i input.mp4 -map 0:v:0 -map 0:a:0 \
  -c:v copy -c:a aac -hls_time 2 -hls_list_size 0 \
  -hls_segment_filename build/reports/av-hls/segment-%03d.ts \
  build/reports/av-hls/index.m3u8
```

示例输入为300幅/10秒且有10秒音频，视频仅复制，用于封装/音频阶段；完整
硬件链应保留应用原argv。`--ffmpeg`与`--ffprobe`选择独立软件验证工具，
验图/验音仍在计时区间外。原版ARM工具现启用标准HLS demuxer，无FFmpeg
源码修改。已验证范围是有限、未加密的本地MPEG-TS HLS，不是直播/加密/网络
或全部OpenList组合。

音频检查读取首个音轨每幅软解的样本数和原时间戳，核对累计样本时长、
逐幅连续性和首尾音画偏差；不使用容器duration冒充完整解码。视频时间戳
也必须连续，无法取得有效时序则失败。默认容差50 ms用于AAC首尾padding，
可显式调整；它不放宽逐幅断口检查。JSON分别标记`audio_timeline_checked`
与`audio_content_checked`，后者当前为false：不校验波形质量或音轨选择是否
符合应用要求。视频仍核对完整帧数及可选参考哈希；解码错误、音频缺失/
缩短、错位、断口和分段缺失均不能算成功。m3u8的`output_sha256`只覆盖
列表，归档还须保留全部分段及其SHA256。

## GE2D 第一个验收边界

[GE2D 驱动](../ge2d/meson8b-ge2d.c)独立使用标准 V4L2 M2M GREY、连续 NV12
和双平面 NV12M，MMAP/DMA-BUF 与 canvas；不接替当前 decoder 的 NEON 路径。S805 寄存器
窗口为 `0xc1106280`，使用已有 G2D 时钟和 GE2D reset，避免套用 AXG 的
直接地址寄存器。依据为 Hardkernel 固定提交
[`5aed95d35d252cafc75ce613a3a0052285662de2`](https://github.com/hardkernel/linux/tree/5aed95d35d252cafc75ce613a3a0052285662de2/drivers/amlogic/display/ge2d)。

驱动在 probe 时分配独立连续缓冲，硬件执行 512×32 字节图案的 big→little
endian 转换并逐字节核对，再检查 512×32→256×16 的常量平面过滤结果。
随后检查完整 NV12 的 Y 图案、两个不同色度分量，以及 NV12 常量平面缩小。
失败不注册视频设备；命令有 100 ms 上限，超时
后将 GE2D 保持在硬复位状态、拒绝新命令，先停止 DMA 再允许队列释放。
完成判定同时等待 busy、当前/缓冲/流水线命令有效位及目的写响应计数归零；
command-ready 位可以在空闲时置位。主机回归覆盖延迟状态与命令卡住，独立
实机18幅复制/裁剪/常量缩小检查通过，范围见[命令排空记录](validation-records.md#11-26-ge2d-命令排空)。
普通客户端使用 little→little，不借 FOURCC 暗示厂商字节序。NV12 走厂商
色度格式器与独立第二输出平面，不能用 GREY blit 插值交错的 U/V 字节。

默认运行包和 Overlay 不启用 GE2D。测试入口需要在未加载 codec overlay
的原设备树上生成带 `--ge2d` 的临时 overlay，并保留原安装/回退材料：

```sh
python3 scripts/make-overlay.py --tree /sys/firmware/devicetree/base \
  --ge2d --output ge2d-test.dts
# 在工作机编译；原5.15运行树可能没有/__symbols__，不要加-@。
dtc -I dts -O dtb -o ge2d-test.dtbo ge2d-test.dts
# 模块沿用临时overlay加载/恢复流程。
arm-linux-gnueabihf-gcc-11 -O2 -Wall -Wextra -Werror -static \
  tests/ge2d-copy.c -o build/ge2d-copy
# 用枚举得到的 GE2D 设备号；示例编号不代表固定设备。
./ge2d-copy /dev/video2 512 32 10
./ge2d-copy /dev/video2 1920 1088 10
./ge2d-copy /dev/video2 1920 1088 10 1280 720
./ge2d-copy /dev/video2 1920 1080 10 1920 1080 NV12M
./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12
GE2D_DMABUF=1 ./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12M
./ge2d-copy /dev/video2 1920 1080 10 1280 720 NV12M 2 2 1280 720
```

[客户端](../tests/ge2d-copy.c)比较每幅两个平面的全部可见像素、payload、
序列号和时间戳；行尾 padding 不当作图片。缩小模式使用逐帧不同的 Y 与
U/V 常量，不能代替有纹理视频的滤波质量验证。`GE2D_DMABUF=1` 通过另一个
不运行任务的 GE2D 句柄导出连续缓冲，导入测试队列；CPU 访问使用标准
DMA-BUF 同步，不假设其他导出器支持连续物理地址。

输出可独立协商为裁剪区域的 1–1/4，使用厂商 bilinear 表与有界 phase/
prescale 配置。`VIDIOC_S_SELECTION` 支持 OUTPUT crop，NV12 坐标/尺寸必须
为偶数；排队后拒绝格式或 crop 修改。两种 NV12 内存布局可互转，保留色彩、
量程与 transfer 信息，不进行色彩矩阵转换。输入/输出各平面验证容量、32位
DMA范围、对齐及重叠；错误输出清零两个平面的 payload。

独立GE2D M2M在每次处理导入缓冲前等待已有的DMA-BUF fence：读OUTPUT只等
exclusive写者，写CAPTURE等shared读者与exclusive写者，每平面最多1 s且可
中断。等待在硬件锁外，超时/中断归还两端ERROR、输出零payload，不启动
命令。MMAP不增加等待。完成/所有权仍由V4L2 QBUF/DQBUF交接，没有发布新的
DMA-BUF completion fence；不能让其他设备在本次任务完成前并发使用这些面。

当前通过实际命令、格式、裁剪、队列和客户端的主机 ASan/UBSan 回归与 ARM
构建。取消/重开、不同 DMA-BUF 导出器、纹理缩放及共享时钟/业务仍需实机。
11-25修正Overlay导出符号、NV12源色度排列及复制路径后，独立硬件验收通过：
GREY/NV12M原样复制与裁剪的全部可见字节、常量Y/U/V平面缩小、DMA-BUF
导入及时间戳通过。默认Overlay字节不变，GE2D仍只临时加载。两组纹理缩小在修正水平分段读取后完成独立bilinear质量比较，最低PSNR约
33.5 dB；不同插值实现不逐字节相同。跨导出器生命周期、完整标准工具链及
实际转码加速收益尚未通过。精确范围见
[重启后记录](validation-records.md#11-25-重启后格式修正与-ge2d-验收)。

## 显式 decoder 接入（生产默认关闭）

当前源码增加 decoder 的 `capture_ge2d=1` 参数，默认关闭。它要求带
`--ge2d` 的 Overlay 明确引用 `amlogic,ge2d`，GE2D 驱动已加载且 probe
自检通过；缺失/未就绪时 decoder probe 失败/延期，不悄悄改变配置。
设备链接及可选模块引用保持 GE2D 生命周期，用户态 M2M 和内核转换用同一
硬件锁串行；decoder 卸载后释放引用。runtime 仍在依赖模块卸载后才移除
挥发性 Overlay，不改 boot DT。

启用后的MMAP CAPTURE使用缓存vmalloc内存，由一张coherent中转面复用
给所有采集缓冲；GE2D完成后复制到当前MMAP。DMA-BUF导入仍通过
`vb2_dma_contig`验证连续范围并直接写入。MMAP导出的DMA-BUF可能分散，
不保证能重新作为连续导入。帧池保留固件字节序，按图片
自己的 reference/crop 几何转换；只清零行尾和对齐 padding。`ge2d_frames`
统计成功硬件转换，`convert_ns` 是墙钟耗时。MJPEG 和错误路径继续 CPU
转换；DMA-BUF CPU 访问成对同步，GE2D 超时后先复位停机再回退。
显式GE2D的内部帧池只使用coherent映射，不建立cached别名；MJPEG及错误
回退直接读取coherent地址，裁剪路径也相同。默认NEON仍保留cached读取，
替代它需要目标内核支持HighMem连续缓存分配，不能复用已失败的noncoherent候选。

连续DMA-BUF CAPTURE仍消耗CMA；MMAP只需共享中转面，1080p约3 MiB。
此前20个采集缓冲占约60 MiB CMA，当前MMAP避免该占用。
原版FFmpeg的`num_capture_buffers`最小值为2，直接客户端可用
`CAPTURE_BUFFERS=1`做完整图片检查，再测实际内存/回压。压缩输入也占CMA，
原版`num_output_buffers`默认为16、最小值为2；它与CAPTURE的数量分别计算。
低缓冲不保证官方FFmpeg内部帧队列可用，不能把上述最小值当成推荐配置。NEON默认路径不
要求这些限制。显式接入原生客户端取消FNV诊断读取后，400幅精确可见像素通过，含H.264
1080p300幅和HEVC1080p30幅。300幅交付不读取图片约7.27 s；原版FFmpeg
低缓冲仍有短输出/断言失败，高缓冲又可能耗尽CMA。39703c7按当前SPS预算加
一个交付槽：典型1080p的DPB5/重排2由8槽降至6槽，参考面CMA由24降至18 MiB。
DPB16不减为更小容量，交付未完成的槽保持占用。生产默认NEON，不将短样片通过扩大为全链部署通过。

标准OUTPUT2/CAPTURE12加实际scale后，H.264小流缩至160×120的50/50与
1080p短流缩至720p的10/10通过独立软件哈希对照；HEVC此前同类检查8/30。
39703c7候选节省CMA后，原版7.0.2在OUTPUT2/CAPTURE10加scale的HEVC1080p→720p
曾30/30与软件哈希精确，约16.82 s；直接NV12仍12/30。正式重复GE2D验收中工具
崩溃、恢复服务先遇短暂包校验失败，后续哈希又一致；默认NEON已恢复，根因未定。
不能声明完整官方工具链或实时收益。细节见[帧池记录](validation-records.md#11-25-hevc-sps-帧池预算)。

11-26移除显式GE2D帧池缓存别名后，候选原生16组600幅精确，三组MJPEG
回退9幅全平面通过。三条相同官方FFmpeg缩放命令累计90幅逐帧哈希精确；
重加载后HEVC1080p→720p再测两次共60幅精确，GE2D计数各增加30。
这是有界候选重复通过，仍不证明历史崩溃根因或全部工具链/实时吞吐，生产
继续默认NEON。精确源码与模块见[coherent帧池记录](validation-records.md#11-26-ge2d-coherent-帧池)。

最近相同1080p首60图，默认NEON下decode/null约1.827 s，加入CPU软件scale
约7.119 s，硬解→4 Mbps硬编约2.821 s；后者完整60/60及YUV质量通过，约
0.71×，未达1.3×。各阶段有不同的解码读前，不相减推导纯硬件耗时。

11-26使用完整60幅输入、以自然EOF结束的进一步检查确认：CAPTURE8等低
缓冲配置可能返回0却只编码8–11幅，不能计算速度。默认NEON/CAPTURE20及
encoder OUTPUT16可完成直接/CPU缩放转码各60/60；显式GE2D/CAPTURE12
则在编码器分配时耗尽CMA。按真实NV12画布缩小编码参考面，720p节省约
3.36 MiB，仍不足以容纳该完整链；CAPTURE10最后analysis分配失败，CAPTURE9
仅9/60。所有失败和有损质量范围见[完整转码记录](validation-records.md#11-26-编码参考面预算与完整转码边界)。


11-26共享中转面修正后，同一完整60幅1080p输入在显式GE2D/decoder
CAPTURE20、encoder OUTPUT16下，官方7.0.2直接转码和CPU缩至720p再编码
均60/60，重加载重复各60/60。OpenList镜像内未改8.1.3也两条各60/60，完整
软解/全YUV质量通过。仍有CPU复制/缩放，未达到实时或1.3×目标；容器命令
时间包含启动，不能当纯编码耗时。生产默认NEON，见[采集内存记录](validation-records.md#11-26-ge2d-采集内存与官方转码)。

后续格式协商候选复验时，8.1.3容器命令超时并遇containerd服务异常，已经
恢复在役7991ba4的默认驱动及业务；本候选未安装。原7.0.2两条视频转码内容
通过，8.1.3重复保留失败，见[格式边界记录](validation-records.md#11-26-解码格式协商边界)。
decoder缩放需要独立维护源与采集几何；上述格式保护版本将CAPTURE固定到
源尺寸，没有把改写源尺寸当成硬件缩放支持。

## decoder 独立 CAPTURE 缩放

新源码`06504f3`在显式`capture_ge2d=1`且H.264/HEVC已解析源尺寸后，允许
标准CAPTURE S_FMT协商独立输出大小，每轴为源尺寸的1–1/4、偶数NV12尺寸；
TRY_FMT返回实际可用值。码流/参考面宽高和源crop保持，G_FMT的stride/容量、
COMPOSE、交付payload及中转面使用输出大小；非等比例缩放调整像素宽高比。
分配后仍拒绝S_FMT，source-change后重新协商。MJPEG和默认NEON不开放缩放。

上述初版只完成工作机回归/构建；后续独立实机范围见下文。缩放失败返回
ERROR和零payload，不用原尺寸CPU回退写小缓冲。
官方FFmpeg的`-vf scale`仍走CPU；本接口供使用标准V4L2协商的客户端显式
选择，不自动改写FFmpeg命令。恢复硬件验收后可用验证客户端：

```sh
CAPTURE_SIZE=1280x720 CAPTURE_BUFFERS=4 FRAME_DUMP=scaled.nv12 \
  ./res-switch-test /dev/video0 input.h264 H264
```

示例要求已加载本候选、启用GE2D且输入为兼容1080p；实际设备号须枚举。
客户端核对精确协商尺寸，不支持时失败，dump为32字节stride/64 KiB容量
平面。初版离线边界见[记录](validation-records.md#11-26-decoder-ge2d-缩放候选)。

## 单平面 NV12 的编码交接增量

`ca0d320`补齐一处已有复制：独立GE2D可输出标准单内存平面NV12，编码器
此前仅NV12M/NV21M可直接读取，单平面总被复制到staging。候选在连续映射、
行宽和16行高度边界吻合时，按标准UV偏移直接设置两个MFDIN组件地址；
同步/fence与QBUF→DQBUF所有权保持，布局不符仍复制。分支不调用CPU映射或
memcpy，但staging分配仍保留，也没有发布新的DMA-BUF completion fence。

临时编码器候选的15组102幅及重复对照8组64幅，直接/复制计数符合预期，
全部完整软解、顺序与Y/U/V质量通过。新单平面直接分支累计51幅copied=0，
原有NV12M直接对照24幅；其余91幅复制。重复MMAP和原直接路径也有少量
位流/软解像素差异，因此不宣称严格逐字节等价或已定位差异原因。

上述初次候选只验证编码输入交接，没有实际运行GE2D→编码共享缓冲全链；官方FFmpeg
现有MMAP命令继续复制。在役`20220a8`已恢复、GE2D关闭，完整少拷贝/缩放链、
CPU与总吞吐收益仍由#14/#32/#7继续验收，不从短时客户端耗时推断1.3×目标。

后续已将该编码增量正式安装，`f053ed2`验证客户端又补齐实际GE2D→encoder
共享面交接。7组47幅完整图像/顺序/全YUV质量通过，35幅直接读取、12幅布局
不符时复制。CPU只生成GE2D源，GE2D写同一导入目的面，编码器等待出队后读，
没有在两引擎之间CPU重写目的面；内核仍进行必要缓存同步和DMA内存搬运。
这是可观察的交接验证，不是解码→缩放→编码全链或零拷贝吞吐结论。没有
新的completion fence，应用依靠QBUF/DQBUF顺序保持所有权，不能并发复用。
正式包默认GE2D仍关闭，边界见[本次验收](validation-records.md#11-26-ge2d-到编码器共享-dma-buf-交接)。

`926d0c0`客户端增加`-S WxH`，GE2D在同一共享目的面完成真实缩小后编码。
9组47幅覆盖1080p→720p、非等比/四倍边界、非整宏块行回退、同fd重开及
JPEG；完整软解、顺序与相对独立bilinear参考的全YUV质量通过，最低平面
PSNR30.36 dB。41幅编码直接读取、6幅复制；此步仅改客户端，内核字节未改。
运行包和客户端已部署，正式GE2D仍关闭。尚未加入decoder，不将短任务
耗时换算为完整链吞吐，见[缩放交接记录](validation-records.md#11-26-ge2d-缩放后共享编码输入)。

`a6f3fc8`进一步接入实际H264/HEVC decoder输出：独立缩放24组272幅的
MMAP/连续DMA-BUF内容、顺序与全YUV质量通过，两条路径原始字节一致。
共享NV12M直接编码18组172幅（含两种提前取消顺序）通过，copied/copy_ns=0。
另两组尺寸切换共48幅虽完整出队，854→320段未通过独立滤波参考的内容/顺序
检查；相同源的静态/切换后decoder原始像素相等，仍需定位该比例的采样对齐。
中心相位候选未改善并已撤回，正式GE2D保留原字节。没有完整官方工具/音频/
应用收益结论，详见[真实链记录](validation-records.md#11-26-真实解码缩放与共享编码交接)。

`2fea36e`定位上述偏移：固定S805厂商`stretchblt`对四抽头滤波器设置横/
纵首样本重复各1，移植路径遗漏并写0。现仅在对应轴缩小时写入INI_CTRL
重复计数，原尺寸/裁剪不变。原失败静态及切换72幅通过；完整重验decoder
24组272幅最低平面PSNR34.70 dB，MMAP/DMA-BUF原始字节一致；真实共享转码
22组244幅（含原失败比例、尺寸切换和两种取消顺序）全内容/顺序/质量通过，
encoder copied/copy_ns=0。正式NEON继续使用原模块，可选GE2D修复单独部署；
不从这些短时任务推断1.3×或官方FFmpeg收益，见[修复验收](validation-records.md#11-26-ge2d-滤波首样本重复修复)。

## 11-27 1080p 阶段拆解与官方 FFmpeg 7.0.2 基线

进一步以 300 帧/10 秒 `1080p30`（及 60 帧/2 秒缩放对照）对生产默认 NEON
与临时 GE2D（`capture_ge2d=1`）进行分阶段实测：

- **官方原版 FFmpeg 7.0.2 参数边界与基线**：
  - 原始 `.h264` Annex B 输入因缺少有效 PTS 触发大量丢帧（仅 3/300）；有效 PTS
    `.mp4` 输入配合 `-num_capture_buffers 16 -num_output_buffers 12` 仍因解码缓冲被编码器
    持有而在第 58 帧触发 `"All capture buffers returned to userspace"` 提前终止。
  - 扩大解码器输出缓冲并缩小编码器输入缓冲（decoder `-num_capture_buffers 24`、
    encoder `-num_output_buffers 4`）后，官方 `7.0.2` 完成全部 **300/300** 帧
    `1080p->1080p` 转码：`wall=11.667 s`（**0.857×**，`child_cpu=2.795 s`，
    `decoder.convert_ns=3.221 s`，`encoder.copy_ns=1.366 s`，`CmaFree` 最低 `12 KiB`）。
  - 同源 60 帧 `1080p->720p` 官方 `-vf scale=1280:720` CPU 缩放为 `5.055 s`
    （**0.396×**，`child_cpu=8.028 s`）。
- **单引擎拆解（300 帧 `1080p`）**：
  - 解码至空：NEON `ffmpeg -f null -` `6.932 s`（`43.3 fps`）；`res-switch-test`
    NEON MMAP `7.024 s`（`convert_ns=2.534 s`）。
  - 临时 `capture_ge2d=1` 解码：`capture_stage` MMAP `8.574 s`（`convert_ns=8.063 s`，
    受 3 MiB 缓存同步与复制开销影响慢于 NEON）；DRM dumb DMA-BUF(4)
    `1080p->1080p` **`6.942 s`**（`43.2 fps`，`convert_ns=6.082 s`）、
    `1080p->720p` **`6.816 s`**（`44.0 fps`，`convert_ns=4.909 s`，明显快于
    FFmpeg CPU `-vf scale`）。
  - 独立 H.264 编码（`encode-test` `NV12M` 300 帧 `1080p`）：`-p motion` MMAP
    `9.409 s`（`1.06×`，`copy_ns=1.116 s`）；后续目标机高系统负载期间（`load avg ~10.6`）
    `-p static` MMAP 为 `14.991 s`（`0.667×`，`child_cpu=4.241 s`，`copy_ns=1.555 s`）、
    `-d dumb` 零复制输入为 `15.573 s`（`0.642×`，`direct_frames=300, copy_ns=0`，
    `CmaFree` 最低 `0–401 KiB`）。
- **已定位的 1.3× 瓶颈**：
  - 官方 `7.0.2` `v4l2_m2m` 仅走 MMAP 缓冲且无标准 V4L2 缩放滤镜，每帧仍经历三次
    3.1 MiB 搬运（`vdec` NEON 字节序/复制、`ffmpeg` 用户态缓冲复制与 `venc_stage_input`）；
  - `1080p` `amvdec_get_output_size_wh(1920, 1080)=2097152 > 1920*1080` 使每帧导入
    DMA-BUF 仍触发 3 MiB `DMA_BIDIRECTIONAL` `amvdec_clear_padding()` 缓存同步，且
    未缩放 `NV12->NV12` 在 `ge2d_blit()` 中仍拆分为 Y/UV 两次复位与命令轮询；
  - 将 `readl_poll_timeout` 间隔由 `50 us` 收紧至 `10 us` 会在 `1080p` 下引发每帧数千次
    hrtimer 唤醒并在高负载目标机上触发 `1080p` 解码超时与 `eth0` `NETDEV WATCHDOG`
    复位（详见[验证记录](validation-records.md#11-27-1080p-阶段拆解ge2d-alpha-混合与网络超时现场)），
    已回退保持 `50 us` 轮询，默认生产继续保持 NEON 与 GE2D 关闭。
