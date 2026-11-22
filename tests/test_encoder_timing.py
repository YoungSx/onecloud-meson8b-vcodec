"""SPS timing cannot change under an already streaming encoder."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class EncoderTimingTests(unittest.TestCase):
    def test_period_change_requires_streamoff(self):
        s = (ROOT / "venc/venc.c").read_text()
        start = s.index("static int venc_s_parm(")
        end = s.index("\nstatic int venc_subscribe_event(", start)
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
typedef uint64_t u64;
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 10
#define VENC_RC_WINDOW_FRAMES 240
struct file {int unused;};struct v4l2_fract {unsigned int numerator,denominator;};
struct v4l2_streamparm {int type;struct {struct {struct v4l2_fract timeperframe;} output;} parm;};
struct config {unsigned int numerator,denominator;};
struct venc_ctx {struct {int *lock;} ctrl_handler;struct v4l2_fract timeperframe;struct config rc_config;void *m2m_ctx;};
static struct venc_ctx context;static bool streaming;
static struct venc_ctx *venc_fh_to_ctx(struct file *f){(void)f;return &context;}
static void rational_best_approximation(unsigned int n,unsigned int d,unsigned int maxn,unsigned int maxd,unsigned long *num,unsigned long *den){assert(n<=maxn && d<=maxd);*num=n;*den=d;}
static void mutex_lock(int *p){(void)p;}static void mutex_unlock(int *p){(void)p;}
static void *v4l2_m2m_get_src_vq(void *p){return p;}static void *v4l2_m2m_get_dst_vq(void *p){return p;}
static bool vb2_is_streaming(void *p){(void)p;return streaming;}
static int venc_g_parm(struct file *f,void *fh,struct v4l2_streamparm *a){(void)f;(void)fh;a->parm.output.timeperframe=context.timeperframe;return 0;}
"""
            + s[start:end]
            + r"""
int main(void){struct file f={0};struct v4l2_streamparm a={.type=10,.parm.output.timeperframe={1,30}};assert(!venc_s_parm(&f,0,&a));assert(context.rc_config.denominator==30);streaming=true;a.parm.output.timeperframe=(struct v4l2_fract){1,24};assert(venc_s_parm(&f,0,&a)==-EBUSY && context.timeperframe.denominator==30);a.parm.output.timeperframe=(struct v4l2_fract){2,60};assert(!venc_s_parm(&f,0,&a));streaming=false;a.parm.output.timeperframe=(struct v4l2_fract){1001,30000};assert(!venc_s_parm(&f,0,&a) && context.rc_config.numerator==1001 && context.rc_config.denominator==30000);return 0;}
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
                    "-Wno-unused-parameter",
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
