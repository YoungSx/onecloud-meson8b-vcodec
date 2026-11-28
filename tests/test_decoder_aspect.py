"""Exercise the real legacy mailbox handlers and standard pixel-aspect ioctl."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DecoderAspectTests(unittest.TestCase):
    def test_legacy_mailbox_ratio_reaches_standard_ioctl(self):
        helpers = (ROOT / "vdec/vdec_helpers.c").read_text()
        marker = "void amvdec_set_par_from_ratio("
        helper = helpers[helpers.index(marker):helpers.index("\nvoid amvdec_set_par_from_dar(")]
        core = (ROOT / "vdec/vdec.c").read_text()
        ioctl = core[core.index("static int vdec_g_selection("):
                     core.index("static const struct v4l2_ioctl_ops vdec_ioctl_ops")]
        for codec, ratio_reg, mailbox in (("mpeg4", "MP4_PIC_RATIO", "MREG_BUFFEROUT"),
                                           ("vc1", "VC1_PIC_RATIO", "VC1_BUFFEROUT")):
            with self.subTest(codec=codec):
                source = (ROOT / f"vdec/codec_{codec}.c").read_text()
                macros = "\n".join(line for line in source.splitlines()
                                   if line.lstrip().startswith("#define"))
                header_state_check = ""
                if codec == "mpeg4":
                    header_state_check = "fmt.pixfmt=V4L2_PIX_FMT_H263;s.pixelaspect=(struct v4l2_fract){17,13};check(&s,2,17,13);fmt.pixfmt=2;"
                start = source.index(f"static irqreturn_t codec_{codec}_threaded_isr(")
                end = source.index(f"static irqreturn_t codec_{codec}_isr(", start)
                code = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <linux/videodev2.h>
typedef uint32_t u32;typedef uint64_t u64;typedef int irqreturn_t;
#include "capture-geometry.h"
#define BIT(n) (1u<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define EXPORT_SYMBOL_GPL(x)
#define IRQ_HANDLED 1
static inline void dev_dbg(const void *dev, const char *fmt, ...) {(void)dev;(void)fmt;}
#include "dos_regs.h"
struct amvdec_core {void *dev;};
struct format {u32 pixfmt;};
struct amvdec_session {int fh;struct meson_capture_geometry capture;struct amvdec_core *core;struct format *fmt_out;struct v4l2_fract pixelaspect;u32 width,height,rotation,reference_width,reference_height,crop_left,crop_top;};
struct file {void *private_data;};
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static u32 registers[65536];static int delivered,aborted;
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return registers[r/4];}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;registers[r/4]=v;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborted++;}
static void amvdec_dst_buf_done_idx(struct amvdec_session *s,u32 i,u32 offset,u32 field){(void)s;(void)i;(void)offset;(void)field;delivered++;}
'''
                code += '#include "codec-par.h"\n' + helper
                registrations = "\n".join(line for line in core.splitlines()
                                            if line.strip().startswith((".vidioc_g_selection =", ".vidioc_g_pixelaspect =")))
                code += macros + "\n" + source[start:end] + ioctl + r'''
struct video_device {unsigned long flags;};
struct v4l2_ioctl_ops {int (*vidioc_g_selection)(struct file *,void *,struct v4l2_selection *);int (*vidioc_g_pixelaspect)(struct file *,void *,int,struct v4l2_fract *);};
static struct video_device device;
static struct video_device *video_devdata(struct file *f){(void)f;return &device;}
#define ENOIOCTLCMD 515
#define WARN_ON(x) (x)
#define test_bit(b,p) (((*(p))>>(b))&1)
#define V4L2_FL_QUIRK_INVERTED_CROP 0
#include "../tests/fixtures/v4l2-cropcap-5.15.79.c"
''' + "static const struct v4l2_ioctl_ops ops={\n" + registrations + "\n};\n" + r'''
static void check(struct amvdec_session *s,u32 ratio,u32 num,u32 den){
 registers[RATIO/4]=ratio;registers[MAILBOX/4]=1;
 codec_CODEC_threaded_isr(s);struct file f={&s->fh};
 for(u32 type=V4L2_BUF_TYPE_VIDEO_CAPTURE;type<=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;type+=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE-V4L2_BUF_TYPE_VIDEO_CAPTURE){
  struct v4l2_cropcap cap={.type=type};
  assert(!v4l_cropcap(&ops,&f,NULL,&cap));
  assert(cap.type==type&&cap.pixelaspect.numerator==den&&cap.pixelaspect.denominator==num);
  assert(cap.bounds.width==s->width&&cap.defrect.width==s->width);
 }
 assert(!registers[MAILBOX/4]&&!aborted);
}
int main(void){struct amvdec_core core={0};struct format fmt={2};
 struct amvdec_session s={.core=&core,.fmt_out=&fmt,.pixelaspect={1,1},.width=720,.height=576};
 check(&s,2,12,11);check(&s,3,10,11);check(&s,4,16,11);check(&s,5,40,33);
 check(&s,0x0b0c,11,12);check(&s,0x0302,3,2);
 check(&s,0,1,1);check(&s,1,1,1);check(&s,14,1,1);check(&s,15,1,1);
 check(&s,0x0100,1,1);check(&s,0x0020,1,1);
 RESERVED_OR_VC1
 H263_CHECK
 struct file f={&s.fh};struct v4l2_fract par;
 assert(vdec_g_pixelaspect(&f,NULL,V4L2_BUF_TYPE_VIDEO_OUTPUT,&par)==-EINVAL);
 struct v4l2_selection sel={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.target=V4L2_SEL_TGT_COMPOSE};
 s.width=1912;s.height=1080;s.reference_width=1920;s.reference_height=1088;s.crop_left=4;s.crop_top=2;
 assert(!v4l_g_selection(&ops,&f,NULL,&sel)&&sel.type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
 assert(sel.r.left==0&&sel.r.top==0&&sel.r.width==1912&&sel.r.height==1080);
 struct v4l2_cropcap cap={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 assert(!v4l_cropcap(&ops,&f,NULL,&cap));
 assert(cap.bounds.width==1920&&cap.bounds.height==1088&&cap.defrect.left==4&&cap.defrect.top==2);
 meson_capture_configure(&s.capture,1912,1080,956,1080,true);
 sel.target=V4L2_SEL_TGT_COMPOSE;
 assert(!v4l_g_selection(&ops,&f,NULL,&sel)&&sel.r.width==956&&sel.r.height==1080);
 cap=(struct v4l2_cropcap){.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};s.pixelaspect=(struct v4l2_fract){1,1};
 assert(!v4l_cropcap(&ops,&f,NULL,&cap));
 assert(cap.bounds.width==1920&&cap.defrect.width==1912&&cap.pixelaspect.numerator==1&&cap.pixelaspect.denominator==2);
 sel.target=0xffffffff;assert(v4l_g_selection(&ops,&f,NULL,&sel)==-EINVAL);
 sel.type=V4L2_BUF_TYPE_VIDEO_OUTPUT;sel.target=V4L2_SEL_TGT_COMPOSE;assert(v4l_g_selection(&ops,&f,NULL,&sel)==-EINVAL);
 return 0;}
'''.replace("RATIO", ratio_reg).replace("MAILBOX", mailbox).replace("CODEC", codec).replace(
                    "RESERVED_OR_VC1", "check(&s,6,1,1);check(&s,13,1,1);" if codec == "mpeg4" else
                    "check(&s,6,24,11);check(&s,9,80,33);check(&s,13,160,99);").replace("H263_CHECK", header_state_check)
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory)
                    (path / "test.c").write_text(code)
                    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                    "-O1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                                    "-I", str(ROOT / "vdec"), str(path / "test.c"),
                                    "-o", str(path / "test")], check=True)
                    subprocess.run([str(path / "test")], check=True)

    def test_h264_shared_table_preserves_firmware_mapping(self):
        source = (ROOT / "vdec/codec_h264.c").read_text()
        start = source.index("static void codec_h264_set_par(")
        end = source.index("static void codec_h264_resume(", start)
        code = r"""
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((~0u<<(l))&(~0u>>(31-(h))))
#define AR_IDC_BIT 16
#define AR_IDC_MASK GENMASK(7,0)
#define AR_PRESENT_FLAG BIT(0)
#define AR_EXTEND 255
#include "dos_regs.h"
struct v4l2_fract {u32 numerator,denominator;};
#include "codec-par.h"
struct amvdec_core {u32 sequence,extended;};
struct amvdec_session {struct amvdec_core *core;struct v4l2_fract pixelaspect;};
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){return r==AV_SCRATCH_2?c->sequence:c->extended;}
""" + source[start:end] + r"""
int main(void){
 struct amvdec_core c={0};struct amvdec_session s={&c,{17,13}};
 codec_h264_set_par(&s);assert(s.pixelaspect.numerator==17&&s.pixelaspect.denominator==13);
 const u32 expected[][2]={{1,1},{1,1},{12,11},{10,11},{16,11},{40,33},{24,11},{20,11},{32,11},{80,33},{18,11},{15,11},{64,33},{160,99},{4,3},{3,2},{2,1}};
 for(u32 i=0;i<17;i++){
  c.sequence=(i<<16)|1;codec_h264_set_par(&s);
  assert(s.pixelaspect.numerator==expected[i][0]&&s.pixelaspect.denominator==expected[i][1]);
 }
 c.sequence=(17<<16)|1;codec_h264_set_par(&s);assert(s.pixelaspect.numerator==2&&s.pixelaspect.denominator==1);
 c.sequence=(255<<16)|1;c.extended=(13<<16)|17;codec_h264_set_par(&s);
 assert(s.pixelaspect.numerator==17&&s.pixelaspect.denominator==13);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
