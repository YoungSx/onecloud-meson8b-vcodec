# 研究线构建与运行边界

研究补丁位于 [patches/research](../patches/README.md)，针对 Linux
7.3.4；不是原 OneCloud 5.15.79 部署补丁。原始构建日志已按
[仓库规则](repository-policy.md)归档，下文保留来源、演进与验证边界。

## 固定输入与复现

11-16 原始 `linux-7.3.4.tar.xz` SHA256 为
`b11b6df8295e8e991ff040b71aaa4994a8b55caa23b6b092907f195b5840fd1f`，
工具链为 arm-linux-gnueabihf GCC 13.3.0。配置基于 `multi_v7_defconfig`，
启用 `CONFIG_STAGING_MEDIA`、Meson VDEC 与 canvas。来源检查、补丁顺序
及当前完整构建方式在[构建指南](build.md)、[研究补丁说明](../patches/README.md)
和 [.github/workflows/research.yml](../.github/workflows/research.yml)。

初始只编单模块缺少 vmlinux/Module.symvers，曾以 modpost warning 链接；
这些结果不作为最终有效模块。完整内核构建后符号和依赖已解析，最终
vermagic 为 `7.3.4 SMP mod_unload ARMv7 p2v8`。初始 partial 和完整模块
哈希不同，不能据此推断源码变化。

## 验证过的演进

| 阶段 | 实现与检查 |
| --- | --- |
| 11-16 初始平台 | 补丁无 fuzz 应用，Meson8b 平台/IRQ/时钟及 ODROID-C1 DT；内核/模块、全部 ARM DTB、VDEC binding 检查通过。 |
| power domain、DOS syscon | AO sleep/isolation 由 genpd 管理；共享 DOS regmap 保证位更新锁；DT 和绑定正例通过，缺失 domain/ao-sysctrl 或不完整 reg 区域负例被检出，旧 GX/SM1 DT 形状保留。 |
| capture/NEON | 缓存别名和字节序转换；ARM FPU 独立单元，fallback 标量；ASan/UBSan 与实际 swap body 等价测试通过。 |
| 11-22 后续系列 | 独立帧池、既有 VDEC1/HEVC 格式、IRQ/EOS/DMA、固定父钟保护同步；27 个补丁完整内核/模块、ARM DT/binding 与 ODROID 检查通过。 |
| 11-23 `0027` | HEVC STREAMOFF 丢弃引用图，28 个补丁应用/checkpatch 及改变的 HEVC ARM 对象编译通过；未重复完整新 research 内核/DT 构建。 |

NEON 单元必须与 `kernel_neon_begin()` 调用单元分离，并通过内核
`asm/neon-intrinsics.h` 处理类型；`may_use_simd()` 不成立时使用标量。
初始每 CAPTURE 一份 shadow 的历史实现已被独立帧池替代，不能按旧
构建记录推断当前内存模型。生产路径测得的 7–8 ms 也不是研究内核实测。

## 未完成

研究内核、静态 DT、跨板型从未在 S805 实机启动。电源/时钟顺序、DMA
一致性、帧完整性、EOS/取消、资源/服务与回退仍须逐版实机验收。源码补丁
和 schema 通过不能证明 silicon 行为；上游提交不属于当前项目目标。
设计背景见[共享电源域](power-domain-design.md)和[上游结论](linux-media-rfc.md)。
