// SPDX-License-Identifier: GPL-2.0
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/workqueue.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>

#include "meson8b-runtime.h"

#define OVERLAY_FILE "meson/vdec/meson8b-runtime.dtbo"
#define DOS_BASE 0xd0050000
#define DOS_SIZE SZ_64K

static int overlay_id;
static bool ready;
static void __iomem *dos_base;
static DEFINE_SPINLOCK(dos_lock);
static char *overlay_file = OVERLAY_FILE;
module_param(overlay_file, charp, 0444);
MODULE_PARM_DESC(overlay_file, "Firmware name of the runtime device-tree overlay");

void __iomem *meson8b_runtime_dos_base(void)
{
	return dos_base;
}
EXPORT_SYMBOL_GPL(meson8b_runtime_dos_base);

void meson8b_runtime_update_dos_bits(u32 reg, u32 mask, u32 value)
{
	unsigned long flags;
	u32 reg_value;

	spin_lock_irqsave(&dos_lock, flags);
	reg_value = readl_relaxed(dos_base + reg);
	writel_relaxed((reg_value & ~mask) | (value & mask), dos_base + reg);
	spin_unlock_irqrestore(&dos_lock, flags);
}
EXPORT_SYMBOL_GPL(meson8b_runtime_update_dos_bits);

static void meson8b_unmap_dos(void)
{
	iounmap(dos_base);
	dos_base = NULL;
	release_mem_region(DOS_BASE, DOS_SIZE);
}

bool meson8b_runtime_ready(void)
{
	return READ_ONCE(ready);
}
EXPORT_SYMBOL_GPL(meson8b_runtime_ready);

static void meson8b_destroy_codec(const char *compatible)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (np) {
		pdev = of_find_device_by_node(np);
		if (pdev) {
			of_platform_device_destroy(&pdev->dev, NULL);
			put_device(&pdev->dev);
		}
		of_node_put(np);
	}
}

static int meson8b_remove_overlay(void)
{
	/*
	 * 5.15.79 releases device links on system_long_wq. Destroy our devices
	 * and finish those releases before OF frees its overlay-owned nodes.
	 * Newer kernels provide device_link_wait_removal() for this ordering.
	 */
	meson8b_destroy_codec("amlogic,meson8b-vdec");
	meson8b_destroy_codec("amlogic,meson8b-venc");
	meson8b_destroy_codec("amlogic,meson8b-ge2d");
	flush_workqueue(system_long_wq);
	return of_overlay_remove(&overlay_id);
}

static int __init meson8b_runtime_init(void)
{
	struct device_node *np;
	const struct firmware *fw;
	int ret;

	if (!of_machine_is_compatible("amlogic,meson8b") ||
	    !of_machine_is_compatible("xunlei,onecloud"))
		return -ENODEV;

	np = of_find_compatible_node(NULL, NULL, "amlogic,meson8b-vdec");
	if (np) {
		of_node_put(np);
		return -EEXIST;
	}
	np = of_find_compatible_node(NULL, NULL, "amlogic,meson8b-venc");
	if (np) {
		of_node_put(np);
		return -EEXIST;
	}
	ret = request_firmware_direct(&fw, overlay_file, NULL);
	if (ret)
		return ret;
	if (fw->size > SZ_64K) {
		ret = -EFBIG;
		goto release;
	}
	if (!request_mem_region(DOS_BASE, DOS_SIZE, "meson8b-codecs")) {
		ret = -EBUSY;
		goto release;
	}
	dos_base = ioremap(DOS_BASE, DOS_SIZE);
	if (!dos_base) {
		release_mem_region(DOS_BASE, DOS_SIZE);
		ret = -ENOMEM;
		goto release;
	}
	ret = of_overlay_fdt_apply(fw->data, fw->size, &overlay_id);
	if (ret) {
		if (overlay_id)
			meson8b_remove_overlay();
		meson8b_unmap_dos();
		goto release;
	}
	WRITE_ONCE(ready, true);
	pr_info("meson8b-runtime: applied volatile codec overlay %d\n", overlay_id);
release:
	release_firmware(fw);
	return ret;
}

static void __exit meson8b_runtime_exit(void)
{
	WRITE_ONCE(ready, false);
	WARN_ON(meson8b_remove_overlay());
	meson8b_unmap_dos();
}

module_init(meson8b_runtime_init);
module_exit(meson8b_runtime_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Volatile OneCloud codec device-tree overlay loader");
MODULE_FIRMWARE(OVERLAY_FILE);
