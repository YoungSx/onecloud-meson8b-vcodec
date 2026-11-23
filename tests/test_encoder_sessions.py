"""Actual ownership changes retain per-session memory and release only its owner."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EncoderSessionTests(unittest.TestCase):
    def test_hardware_restore_keeps_reference_canvas_roles(self):
        source = (ROOT / "venc/venc.c").read_text()
        body = source[source.index("static int venc_hw_init("):
                      source.index("static void venc_free_buffers(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#define V4L2_PIX_FMT_JPEG 1
#define HCODEC_ASSIST_MMC_CTRL1 1
#define HENC_SCRATCH_0 2
#define HENC_SCRATCH_D 3
#define HENC_SCRATCH_L 4
#define ENCODER_IDLE 0
struct venc_core {bool need_reset;int hw_status;};
struct venc_ctx {struct venc_core *core;u32 dblk_canvas,ref_canvas,dst_fmt,slice_cfg;bool initialized;};
static int powers,loads;
static int venc_poweron(struct venc_core *c){(void)c;powers++;return 0;}
static void venc_poweroff(struct venc_core *c){(void)c;}
static void venc_buffspec_init(struct venc_ctx *c){c->dblk_canvas=11;c->ref_canvas=22;}
static void venc_canvas_init(struct venc_ctx *c){(void)c;}
static int venc_loadmc(struct venc_ctx *c){(void)c;loads++;return 0;}
static void venc_write(struct venc_core *c,int r,u32 v){(void)c;(void)r;(void)v;}
static void venc_amrisc_reset(struct venc_core *c){(void)c;}
static void venc_amrisc_start(struct venc_core *c){(void)c;}
static void venc_init_encoder(struct venc_ctx *c,bool b){(void)c;(void)b;}
static void venc_prot_init(struct venc_ctx *c,bool b){(void)c;(void)b;}
#define INIT_STUB(n) static void n(struct venc_ctx *c){(void)c;}
INIT_STUB(venc_init_input_buffer) INIT_STUB(venc_init_output_buffer)
INIT_STUB(venc_init_dblk_buffer) INIT_STUB(venc_init_reference_buffer)
INIT_STUB(venc_init_assit_buffer) INIT_STUB(venc_init_ie_me_parameter)
''' + body + r'''
int main(void){struct venc_core core={0};struct venc_ctx c={.core=&core};
 assert(!venc_hw_init(&c)&&c.dblk_canvas==11&&c.ref_canvas==22);
 c.initialized=true;c.dblk_canvas=22;c.ref_canvas=11;
 assert(!venc_hw_init(&c)&&c.dblk_canvas==22&&c.ref_canvas==11&&powers==2&&loads==2);
 return 0;}
'''
        self.compile_run(code)

    def compile_run(self, code):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_switch_restore_failure_and_inactive_release(self):
        source = (ROOT / "venc/venc.c").read_text()
        body = source[source.index("static void venc_hw_stop("):
                      source.index("/*\n * Vendor light reset:", source.index("static void venc_hw_stop("))]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
struct venc_ctx;
struct venc_core {struct venc_ctx *ctx;bool done_pending,running;int irq;};
struct venc_ctx {struct venc_core *core;bool initialized;int reference,starts,restores,releases;};
#define WRITE_ONCE(x,v) ((x)=(v))
#define HCODEC_ASSIST_MBOX2_MASK 1
static void venc_write(struct venc_core *c,int r,int v){(void)c;(void)r;(void)v;}
static int stops,poweroffs,failed;
static void venc_amrisc_stop(struct venc_core *c){(void)c;stops++;}
static void venc_poweroff(struct venc_core *c){(void)c;poweroffs++;}
static void synchronize_irq(int irq){(void)irq;}
static void venc_session_release(struct venc_ctx *c){c->initialized=false;c->releases++;}
static int venc_hw_init(struct venc_ctx *c){assert(c->core->ctx==c);c->restores++;return failed;}
static int venc_hw_start(struct venc_ctx *c){assert(c->core->ctx==c);c->starts++;if(!failed)c->initialized=true;return failed;}
''' + body + r'''
int main(void){
 struct venc_core core={0};struct venc_ctx a={.core=&core,.reference=17},b={.core=&core,.reference=29};
 assert(!venc_activate(&a)&&core.ctx==&a&&a.starts==1&&core.running);
 assert(!venc_activate(&a)&&a.starts==1&&!stops);
 assert(!venc_activate(&b)&&b.starts==1&&a.initialized&&a.reference==17&&stops==1);
 assert(!venc_activate(&a)&&a.restores==1&&b.initialized&&a.reference==17&&stops==2);
 int old_stops=stops;venc_hw_stop(&b);
 assert(!b.initialized&&core.ctx==&a&&core.running&&stops==old_stops&&b.releases==1);
 failed=-ETIMEDOUT;
 assert(venc_activate(&b)==-ETIMEDOUT&&!core.ctx&&!core.running&&a.initialized);
 failed=0;assert(!venc_activate(&a)&&a.restores==2);
 venc_hw_stop(&a);assert(!core.running&&!core.ctx&&!a.initialized);
 assert(stops==poweroffs);
 return 0;
}
'''
        self.compile_run(code)
