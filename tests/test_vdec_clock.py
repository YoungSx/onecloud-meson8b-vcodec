"""Reject VDEC1 rate changes that might propagate to a shared MPLL."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class VdecClockTests(unittest.TestCase):
    def test_only_exact_parent_division_changes(self):
        source = (ROOT / "vdec/vdec_1.c").read_text()
        start = source.index("static int vdec_1_set_rate(")
        end = source.index("static int vdec_1_start(", start)
        code = (
            r"""
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#define VDEC_REVISION_MESON8B 0
#define dev_err(...) ((void)0)
struct clk {struct clk *parent;unsigned long rate;};
struct platform {int revision;};
struct amvdec_core {struct platform *platform;struct clk *vdec_1_clk,*vdec_1_div_clk,*vdec_1_parent_clk;void *dev;};
static int calls,parent_calls,parent_result;static struct clk *changed;
static struct clk *clk_get_parent(struct clk *c){return c->parent;}
static int clk_set_parent(struct clk *c,struct clk *p){assert(c&&p);parent_calls++;if(parent_result)return parent_result;c->parent=p;c->rate=p->rate;return 0;}
static unsigned long clk_get_rate(struct clk *c){return c->rate;}
static int clk_set_rate(struct clk *c,unsigned long rate){assert(c);calls++;changed=c;c->rate=rate;return 0;}
"""
            + source[start:end]
            + r"""
int main(void){struct platform p={0};struct clk fixed={.rate=364285714},selector={.rate=637500000},divider={.parent=&selector},gate={.parent=&divider},output={.parent=&gate};struct amvdec_core core={.platform=&p,.vdec_1_clk=&output,.vdec_1_div_clk=&divider,.vdec_1_parent_clk=&fixed};assert(!vdec_1_set_rate(&core,182142857));assert(calls==1 && changed==&divider && parent_calls==1 && selector.parent==&fixed);assert(vdec_1_set_rate(&core,255000000)==-EINVAL && calls==1 && parent_calls==1);fixed.rate=0;assert(vdec_1_set_rate(&core,182142857)==-EINVAL && calls==1);fixed.rate=364285714;core.vdec_1_div_clk=NULL;assert(vdec_1_set_rate(&core,182142857)==-EINVAL && calls==1);core.vdec_1_div_clk=&divider;parent_result=-EIO;assert(vdec_1_set_rate(&core,182142857)==-EIO && calls==1);p.revision=1;assert(!vdec_1_set_rate(&core,666666666) && changed==&output && calls==2);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            (p / "test.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
