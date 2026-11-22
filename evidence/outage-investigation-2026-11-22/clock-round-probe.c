// SPDX-License-Identifier: GPL-2.0
/* A one-shot read-only CCF rate-rounding query; never sets or enables clocks. */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_clk.h>

static int __init clock_round_probe_init(void)
{
	struct device_node *np;
	struct clk *clk;
	unsigned long before, after;
	long rounded182, rounded255;

	np = of_find_compatible_node(NULL, NULL, "amlogic,meson8b-vdec");
	if (!np)
		return -ENODEV;
	clk = of_clk_get_by_name(np, "vdec_1");
	of_node_put(np);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	before = clk_get_rate(clk);
	rounded182 = clk_round_rate(clk, 182142857);
	rounded255 = clk_round_rate(clk, 255000000);
	after = clk_get_rate(clk);
	clk_put(clk);
	pr_info("clock-round-probe: before=%lu round182=%ld round255=%ld after=%lu (no clk_set_rate)\n",
		before, rounded182, rounded255, after);
	return 0;
}
static void __exit clock_round_probe_exit(void) {}
module_init(clock_round_probe_init);
module_exit(clock_round_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Meson8b CCF rate rounding query");
