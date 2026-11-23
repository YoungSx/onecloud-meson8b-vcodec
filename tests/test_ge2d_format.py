"""Run production V4L2 negotiation, plane preparation and crop selection bodies."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dFormatTests(unittest.TestCase):
    def test_layout_colour_crop_and_plane_capacity(self):
        source = (ROOT / "ge2d/meson8b-ge2d.c").read_text()
        images = source[source.index("static bool ge2d_is_nv12("):
                        source.index("static void ge2d_work(")]
        queues = source[source.index("static int ge2d_queue_setup("):
                        source.index("static void ge2d_queue(")]
        formats = source[source.index("static int ge2d_enum_fmt("):
                         source.index("static const struct v4l2_ioctl_ops")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <linux/videodev2.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;
#define U32_MAX UINT32_MAX
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define clamp_t(t,x,l,h) ((t)(x)<(t)(l)?(t)(l):(t)(x)>(t)(h)?(t)(h):(t)(x))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
struct device {int unused;};
struct ge2d_ctx;
struct vb2_queue {u32 type;bool busy;struct ge2d_ctx *ctx;};
struct m2m {struct vb2_queue src,dst;};
struct v4l2_fh {struct m2m *m2m_ctx;};
struct ge2d_ctx {struct v4l2_fh fh;struct v4l2_pix_format_mplane input,output;struct meson8b_ge2d_crop crop;};
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct vb2_buffer {u32 type,num_planes;struct vb2_queue *vb2_queue;
 struct {u32 data_offset,size,payload;dma_addr_t address;} planes[2];};
struct file {int unused;};
static void *vb2_get_drv_priv(struct vb2_queue *q){return q->ctx;}
static u32 vb2_plane_size(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].size;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].payload;}
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].address;}
static struct vb2_queue *v4l2_m2m_get_src_vq(struct m2m *m){return &m->src;}
static struct vb2_queue *v4l2_m2m_get_dst_vq(struct m2m *m){return &m->dst;}
static bool vb2_is_busy(struct vb2_queue *q){return q->busy;}
'''
        code = code.replace("struct ge2d_ctx;", '#include "ge2d-layout.h"\n'
                            '#include "ge2d-scale.h"\nstruct ge2d_ctx;')
        code += images + queues + formats + r'''
int main(void){
 struct m2m m={0};struct ge2d_ctx c={.fh.m2m_ctx=&m};
 m.src.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;m.src.ctx=&c;m.dst.ctx=&c;
 struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
 .fmt.pix_mp={.width=854,.height=479,.pixelformat=V4L2_PIX_FMT_NV12M,
 .colorspace=V4L2_COLORSPACE_REC709,.quantization=V4L2_QUANTIZATION_FULL_RANGE,
 .ycbcr_enc=V4L2_YCBCR_ENC_709,.xfer_func=V4L2_XFER_FUNC_709}};
 assert(!ge2d_set_fmt(NULL,&c.fh,&f));
 assert(c.input.width==854&&c.input.height==480&&c.input.num_planes==2);
 assert(c.input.plane_fmt[0].bytesperline==864);
 assert(c.input.plane_fmt[0].sizeimage==864*480&&c.input.plane_fmt[1].sizeimage==864*240);
 f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;f.fmt.pix_mp.width=640;f.fmt.pix_mp.height=360;
 f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;f.fmt.pix_mp.colorspace=V4L2_COLORSPACE_SRGB;
 f.fmt.pix_mp.plane_fmt[0].bytesperline=640;
 assert(!ge2d_set_fmt(NULL,&c.fh,&f));
 assert(c.output.num_planes==1&&c.output.plane_fmt[0].sizeimage==640*360*3/2);
 assert(c.output.colorspace==V4L2_COLORSPACE_REC709&&c.output.quantization==V4L2_QUANTIZATION_FULL_RANGE);
 struct vb2_buffer b={.type=m.src.type,.num_planes=2,.vb2_queue=&m.src,
 .planes={{.size=864*480,.payload=864*480,.address=0x1000000},
 {.size=864*240,.payload=864*240,.address=0x2000000}}};
 assert(!ge2d_prepare(&b));b.planes[1].payload--;assert(ge2d_prepare(&b)==-EINVAL);
 b.planes[1].payload++;b.planes[1].size--;assert(ge2d_prepare(&b)==-EINVAL);
 b.planes[1].size++;b.planes[1].data_offset=8;assert(ge2d_prepare(&b)==-EINVAL);
 b.planes[1].data_offset=0;b.planes[1].address=0x1000000;assert(ge2d_prepare(&b)==-EINVAL);
 b.planes[1].address=0x2000000;
 unsigned planes=2,buffers=1,sizes[]={864*480,864*240-1};
 assert(ge2d_queue_setup(&m.src,&buffers,&planes,sizes,NULL)==-EINVAL);
 planes=0;assert(!ge2d_queue_setup(&m.src,&buffers,&planes,sizes,NULL)&&planes==2&&sizes[1]==864*240);
 struct v4l2_selection s={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT,.target=V4L2_SEL_TGT_CROP,
 .r={.left=2,.top=2,.width=800,.height=400},.flags=V4L2_SEL_FLAG_GE|V4L2_SEL_FLAG_LE};
 assert(!ge2d_set_selection(NULL,&c.fh,&s));
 s.r.left=1;assert(ge2d_set_selection(NULL,&c.fh,&s)==-EINVAL&&c.crop.left==2);
 s.r.left=-2;assert(ge2d_set_selection(NULL,&c.fh,&s)==-EINVAL);
 s.r.left=2;m.src.busy=true;assert(ge2d_set_selection(NULL,&c.fh,&s)==-EBUSY);
 assert(ge2d_set_fmt(NULL,&c.fh,&f)==-EBUSY);m.src.busy=false;
 assert(!ge2d_get_selection(NULL,&c.fh,&s)&&s.r.left==2&&s.r.width==800);
 s.target=V4L2_SEL_TGT_CROP_BOUNDS;assert(!ge2d_get_selection(NULL,&c.fh,&s)&&s.r.width==854);
 s.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;assert(ge2d_get_selection(NULL,&c.fh,&s)==-EINVAL);
 f.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;
 assert(!ge2d_set_fmt(NULL,&c.fh,&f));b.num_planes=1;b.planes[0].size=c.input.plane_fmt[0].sizeimage;
 b.planes[0].payload=b.planes[0].size;
 struct meson8b_ge2d_image image=ge2d_buffer_image(&b,&c.input);
 assert(image.address[1]==image.address[0]+c.input.plane_fmt[0].bytesperline*c.input.height);
 b.planes[0].address=UINT32_MAX-15;image=ge2d_buffer_image(&b,&c.input);
 assert(!image.width&&ge2d_prepare(&b)==-EINVAL);
 b.planes[0].address=0x1000000;
 f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_GREY;
 assert(ge2d_set_fmt(NULL,&c.fh,&f)==-EINVAL);
 struct v4l2_fmtdesc desc={0};assert(!ge2d_enum_fmt(NULL,NULL,&desc)&&desc.pixelformat==V4L2_PIX_FMT_GREY);
 desc.index=2;assert(!ge2d_enum_fmt(NULL,NULL,&desc)&&desc.pixelformat==V4L2_PIX_FMT_NV12M);
 desc.index=3;assert(ge2d_enum_fmt(NULL,NULL,&desc)==-EINVAL);
 assert(!ge2d_get_fmt(NULL,&c.fh,&f));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-O1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "ge2d"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
