// SPDX-License-Identifier: GPL-2.0
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/workqueue.h>

#include "meson8b-runtime.h"

#define OVERLAY_FILE "meson/vdec/meson8b-runtime.dtbo"

static int overlay_id;
static bool ready;

bool meson8b_runtime_ready(void)
{
	return READ_ONCE(ready);
}
EXPORT_SYMBOL_GPL(meson8b_runtime_ready);

static int meson8b_remove_overlay(void)
{
	struct platform_device *pdev;
	struct device_node *np;

	/*
	 * 5.15.79 releases device links on system_long_wq. Destroy our device
	 * and finish those releases before OF frees its overlay-owned nodes.
	 * Newer kernels provide device_link_wait_removal() for this ordering.
	 */
	np = of_find_compatible_node(NULL, NULL, "amlogic,meson8b-vdec");
	if (np) {
		pdev = of_find_device_by_node(np);
		if (pdev) {
			of_platform_device_destroy(&pdev->dev, NULL);
			put_device(&pdev->dev);
		}
		of_node_put(np);
	}
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
	ret = request_firmware_direct(&fw, OVERLAY_FILE, NULL);
	if (ret)
		return ret;
	if (fw->size > SZ_64K) {
		ret = -EFBIG;
		goto release;
	}
	ret = of_overlay_fdt_apply(fw->data, fw->size, &overlay_id);
	if (ret) {
		if (overlay_id)
			meson8b_remove_overlay();
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
}

module_init(meson8b_runtime_init);
module_exit(meson8b_runtime_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Volatile OneCloud codec device-tree overlay loader");
MODULE_FIRMWARE(OVERLAY_FILE);
