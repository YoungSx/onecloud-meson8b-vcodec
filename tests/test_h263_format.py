"""Run the decoder's real size enumeration and H.263 input validation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class H263FormatTests(unittest.TestCase):
    def test_custom_sizes_and_negotiated_picture_geometry(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        negotiate = source[source.index("static bool vdec_capture_scalable("):
                           source.index("static int vdec_try_fmt(")]
        enum = source[source.index("static int vdec_enum_framesizes("):
                      source.index("static int\nvdec_decoder_cmd(")]
        getter = source[source.index("static int vdec_try_fmt("):source.index("static int vdec_s_fmt(")]
        setter = source[source.index("static int vdec_s_fmt("):
                        source.index("static int vdec_enum_fmt(")]
        source = (ROOT / "vdec/codec_mpeg4.c").read_text()
        inspect = source[source.index("static int codec_mpeg4_inspect_input("):
                         source.index("static int codec_mpeg4_can_recycle(")]
        helper = (ROOT / "vdec/vdec_helpers.c").read_text()
        ratio = helper[helper.index("void amvdec_set_par_from_ratio("):
                       helper.index("\nvoid amvdec_set_par_from_dar(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef uint64_t u64;
#include "capture-geometry.h"
#define V4L2_PIX_FMT_H264 5
#define V4L2_PIX_FMT_HEVC 6
#define V4L2_PIX_FMT_JPEG 7
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define V4L2_PIX_FMT_H263 1
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define V4L2_FRMSIZE_TYPE_STEPWISE 3
#define V4L2_FRMSIZE_TYPE_CONTINUOUS 2
#define SZ_1M 1048576
#define V4L2_PIX_FMT_MPEG2 2
#define V4L2_PIX_FMT_NV12M 3
#define V4L2_PIX_FMT_YUV420M 4
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 2
#define V4L2_FIELD_ANY 0
#define V4L2_FIELD_NONE 1
#define ALIGN(v,a) (((v)+(a)-1)&~((a)-1))
#define clamp(v,lo,hi) min(max(v,lo),hi)
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#include "h263-vlc.h"
struct v4l2_fract {u32 numerator,denominator;};
#include "codec-par.h"
struct codec_mpeg4 {u8 *h263_output;struct h263_context h263;};
struct amvdec_format {u32 pixfmt,max_width,max_height,pixfmts_cap[3];};
struct platform {const struct amvdec_format *formats;u32 num_formats;};
struct core {struct platform *platform;void *ge2d_dev;};
struct vb2_queue {bool busy;};struct m2m {struct vb2_queue src,dst;};
struct amvdec_session {int fh,lock;struct meson_capture_geometry capture;u32 dpb_size;struct core *core;const struct amvdec_format *fmt_out;u32 width,height,rotation,src_buffer_size,pixfmt_cap,colorspace,ycbcr_enc,quantization,xfer_func;struct v4l2_fract pixelaspect;void *priv;struct m2m *m2m_ctx;};
static inline struct vb2_queue *v4l2_m2m_get_src_vq(struct m2m *m){return &m->src;}
static inline struct vb2_queue *v4l2_m2m_get_dst_vq(struct m2m *m){return &m->dst;}
static int locked;
static void mutex_lock(int *lock){(void)lock;assert(!locked);locked=1;}
static void mutex_unlock(int *lock){(void)lock;assert(locked);locked=0;}
static inline bool vb2_is_busy(struct vb2_queue *q){assert(locked);return q->busy;}
struct file {void *private_data;};
struct v4l2_frmsizeenum {u32 pixel_format,index,type;struct {u32 width,height;} discrete;
 struct {u32 min_width,max_width,step_width,min_height,max_height,step_height;} stepwise;};
struct vb2_buffer {u8 *data;u32 size,capacity;struct {u32 data_offset;}planes[1];};
struct v4l2_plane_pix_format {u32 reserved[1],sizeimage,bytesperline;};
struct v4l2_pix_format_mplane {struct v4l2_plane_pix_format plane_fmt[3];u32 reserved[1],width,height,pixelformat,num_planes,field,colorspace,ycbcr_enc,quantization,xfer_func;};
struct v4l2_format {u32 type;struct {struct v4l2_pix_format_mplane pix_mp;} fmt;};
static u32 amvdec_get_output_size_wh(u32 width,u32 height){return width*height;}
static int vdec_supports_pixfmt_cap(const struct amvdec_format *fmt,u32 id){return fmt->pixfmts_cap[0]==id;}
#define container_of(p,t,m) ((struct amvdec_session *)((char *)(p)-offsetof(struct amvdec_session,m)))
static const struct amvdec_format *find_format(const struct amvdec_format *f,u32 n,u32 id){for(u32 i=0;i<n;i++)if(f[i].pixfmt==id)return f+i;return NULL;}
static u8 *vb2_plane_vaddr(struct vb2_buffer *b,int plane){assert(!plane);return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int plane){assert(!plane);return b->size;}
static u32 vb2_plane_size(struct vb2_buffer *b,int plane){assert(!plane);return b->capacity;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int plane,u32 n){assert(!plane);b->size=n;}
#include "vdec-input.h"
''' + ratio + negotiate + getter + setter + enum + inspect + r'''
int main(int argc,char **argv){
 assert(argc==3);
 struct amvdec_format formats[]={{1,1920,1080,{3,0}},{2,1920,1080,{3,0}},{5,1920,1080,{3,0}},{6,1920,1088,{3,0}}};struct platform p={formats,4};
 struct m2m m={0};struct core c={.platform=&p};struct amvdec_session s={.core=&c,.fmt_out=&formats[0],.width=128,.height=96,.m2m_ctx=&m};
 s.src_buffer_size=SZ_1M;
 struct v4l2_format format={.type=1};
 const u32 requested[][2]={{1,1},{163,99},{1930,1090}};
 const u32 expected[][2]={{32,32},{164,100},{1920,1080}};
 for(u32 i=0;i<3;i++){
  format.fmt.pix_mp.pixelformat=1;format.fmt.pix_mp.width=requested[i][0];format.fmt.pix_mp.height=requested[i][1];
  assert(vdec_try_fmt_common(&s,2,&format)==&formats[0]);
  assert(format.fmt.pix_mp.width==expected[i][0]&&format.fmt.pix_mp.height==expected[i][1]);
  assert(format.fmt.pix_mp.num_planes==1&&format.fmt.pix_mp.plane_fmt[0].sizeimage==SZ_1M);
 }
 format.type=2;format.fmt.pix_mp.width=163;format.fmt.pix_mp.height=99;
 assert(vdec_try_fmt_common(&s,2,&format)==&formats[0]);
 assert(format.fmt.pix_mp.width==128&&format.fmt.pix_mp.height==96&&format.fmt.pix_mp.num_planes==2);
 assert(format.fmt.pix_mp.plane_fmt[0].bytesperline==128);
 format.type=1;format.fmt.pix_mp.pixelformat=2;format.fmt.pix_mp.width=1;format.fmt.pix_mp.height=1;
 assert(vdec_try_fmt_common(&s,2,&format)==&formats[1]);
 assert(format.fmt.pix_mp.width==256&&format.fmt.pix_mp.height==144);
 struct file f={&s.fh};struct v4l2_frmsizeenum e={.pixel_format=1};
 assert(!vdec_enum_framesizes(&f,NULL,&e)&&e.type==3);
 assert(e.stepwise.min_width==32&&e.stepwise.max_width==1920&&e.stepwise.step_width==4);
 assert(e.stepwise.min_height==32&&e.stepwise.max_height==1080&&e.stepwise.step_height==4);
 e.index=1;assert(vdec_enum_framesizes(&f,NULL,&e)==-EINVAL);
 e.pixel_format=2;e.index=0;assert(!vdec_enum_framesizes(&f,NULL,&e)&&e.type==2);
 e.index=1;assert(vdec_enum_framesizes(&f,NULL,&e)==-EINVAL);
 /* First OUTPUT S_FMT must use the selected codec for CAPTURE geometry too. */
 s.fmt_out=&formats[1];s.pixfmt_cap=V4L2_PIX_FMT_NV12M;
 format=(struct v4l2_format){.type=1};
 format.fmt.pix_mp.pixelformat=1;format.fmt.pix_mp.width=128;format.fmt.pix_mp.height=96;
 assert(!vdec_s_fmt(&f,NULL,&format));
 assert(s.fmt_out==&formats[0]&&s.width==128&&s.height==96);
 format=(struct v4l2_format){.type=2};format.fmt.pix_mp.pixelformat=3;
 format.fmt.pix_mp.width=128;format.fmt.pix_mp.height=96;
 assert(!vdec_s_fmt(&f,NULL,&format)&&s.width==128&&s.height==96);
 format=(struct v4l2_format){.type=1};format.fmt.pix_mp.pixelformat=2;
 format.fmt.pix_mp.width=128;format.fmt.pix_mp.height=96;
 assert(!vdec_s_fmt(&f,NULL,&format)&&s.fmt_out==&formats[1]&&s.width==256&&s.height==144);
 format.fmt.pix_mp.pixelformat=1;format.fmt.pix_mp.width=164;format.fmt.pix_mp.height=100;
 assert(!vdec_s_fmt(&f,NULL,&format)&&s.width==164&&s.height==100);
 /* CAPTURE is unscaled; a different request cannot replace source geometry. */
 format=(struct v4l2_format){.type=2};format.fmt.pix_mp.pixelformat=3;
 format.fmt.pix_mp.width=320;format.fmt.pix_mp.height=240;
 struct amvdec_session saved=s;
 assert(!vdec_s_fmt(&f,NULL,&format)&&s.width==164&&s.height==100);
 assert(format.fmt.pix_mp.width==164&&format.fmt.pix_mp.height==100);
 /* Allocated queues reject mutation before codec, geometry or color changes. */
 for(unsigned out=0;out<2;out++)for(unsigned src=0;src<2;src++)for(unsigned dst=0;dst<2;dst++){
  format=(struct v4l2_format){.type=out?1:2};format.fmt.pix_mp.pixelformat=out?2:3;
  format.fmt.pix_mp.width=1920;format.fmt.pix_mp.height=1080;format.fmt.pix_mp.colorspace=77;
  s=saved;m.src.busy=src;m.dst.busy=dst;
  int rc=vdec_s_fmt(&f,NULL,&format);
  if(dst||(out&&src))assert(rc==-EBUSY&&!memcmp(&s,&saved,sizeof(s)));
  else{assert(!rc);if(!out)assert(s.width==saved.width&&s.height==saved.height);}
 }
 m.src.busy=m.dst.busy=false;s=saved;
 /* Scaled CAPTURE never changes decoder/reference dimensions. */
 c.ge2d_dev=&c;s.fmt_out=&formats[2];s.width=1920;s.height=1080;s.dpb_size=5;
 format=(struct v4l2_format){.type=2};format.fmt.pix_mp.pixelformat=3;
 format.fmt.pix_mp.width=1280;format.fmt.pix_mp.height=720;
 assert(!vdec_s_fmt(&f,NULL,&format)&&s.width==1920&&s.height==1080);
 assert(s.capture.width==1280&&s.capture.height==720);
 assert(format.fmt.pix_mp.width==1280&&format.fmt.pix_mp.height==720&&format.fmt.pix_mp.plane_fmt[0].bytesperline==1280);
 format=(struct v4l2_format){.type=2};assert(!vdec_g_fmt(&f,NULL,&format));
 assert(format.fmt.pix_mp.width==1280&&format.fmt.pix_mp.height==720);
 struct meson_capture_geometry previous=s.capture;
 m.dst.busy=true;format.fmt.pix_mp.width=640;format.fmt.pix_mp.height=360;
 assert(vdec_s_fmt(&f,NULL,&format)==-EBUSY&&!memcmp(&s.capture,&previous,sizeof(previous)));
 m.dst.busy=false;format.fmt.pix_mp.width=1;format.fmt.pix_mp.height=1;
 assert(!vdec_try_fmt(&f,NULL,&format)&&format.fmt.pix_mp.width==480&&format.fmt.pix_mp.height==270);
 assert(!memcmp(&s.capture,&previous,sizeof(previous)));
 s.width=854;s.height=480;format=(struct v4l2_format){.type=2};assert(!vdec_g_fmt(&f,NULL,&format));
 assert(format.fmt.pix_mp.width==854&&format.fmt.pix_mp.height==480);
 s.width=1920;s.height=1080;
 s.dpb_size=0;format.fmt.pix_mp.width=640;format.fmt.pix_mp.height=360;
 assert(!vdec_s_fmt(&f,NULL,&format)&&format.fmt.pix_mp.width==1920&&format.fmt.pix_mp.height==1080);
 s.dpb_size=5;c.ge2d_dev=NULL;format.fmt.pix_mp.width=640;format.fmt.pix_mp.height=360;
 assert(!vdec_s_fmt(&f,NULL,&format)&&format.fmt.pix_mp.width==1920);
 c.ge2d_dev=&c;s=saved;
 format=(struct v4l2_format){.type=99};
 assert(vdec_try_fmt(&f,NULL,&format)==-EINVAL&&vdec_g_fmt(&f,NULL,&format)==-EINVAL);
 assert(vdec_s_fmt(&f,NULL,&format)==-EINVAL&&!memcmp(&s,&saved,sizeof(s)));
 c.ge2d_dev=NULL;s=saved;s.width=128;s.height=96;
 u8 input[20000],original[10000],translated[20000];
 FILE *stream=fopen(argv[1],"rb");assert(stream);
 assert(fread(original,1,sizeof(original),stream)==8271&&feof(stream));fclose(stream);
 memcpy(input,original,8271);
 struct codec_mpeg4 codec={.h263_output=translated};s.priv=&codec;
 memmove(input+17,input,8271);memset(input,0xa5,17);
 struct vb2_buffer b={.data=input,.size=17+5573,.capacity=sizeof(input),.planes={{17}}};
 assert(!codec_mpeg4_inspect_input(&s,&b,0)&&b.size==17+9985&&input[17+3]==0xb6);
 for(u32 i=0;i<17;i++)assert(input[i]==0xa5);
 assert(s.pixelaspect.numerator==12&&s.pixelaspect.denominator==11);
 b.planes[0].data_offset=0;
 memcpy(input,original,8271);b.size=5573;s.width=352;
 assert(codec_mpeg4_inspect_input(&s,&b,0)==-EINVAL&&b.size==5573&&!memcmp(input,original,8271));
 s.width=128;b.capacity=8192;
 assert(codec_mpeg4_inspect_input(&s,&b,0)==-ENOSPC&&b.size==5573&&!memcmp(input,original,8271));
 assert(s.pixelaspect.numerator==12&&s.pixelaspect.denominator==11);
 stream=fopen(argv[2],"rb");assert(stream);
 assert(fread(original,1,sizeof(original),stream)==4558&&feof(stream));fclose(stream);
 s.width=164;s.height=100;b.capacity=sizeof(input);
 const u32 start[]={0,3391,3968,4558},length[]={7760,1856,1962};
 for(u32 i=0;i<3;i++){
  b.size=start[i+1]-start[i];memcpy(input,original+start[i],b.size);
  assert(!codec_mpeg4_inspect_input(&s,&b,0)&&b.size==length[i]&&input[10]==(i==1));
  assert(codec.h263.header.valid&&codec.h263.header.width==164&&codec.h263.header.height==100);
  assert(s.pixelaspect.numerator==1&&s.pixelaspect.denominator==1);
 }
 s.fmt_out=&formats[1];b.data=NULL;b.size=0;assert(!codec_mpeg4_inspect_input(&s,&b,0));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-O1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test"),
                            str(ROOT / "tests/fixtures/h263-baseline-128x96.h263"),
                            str(ROOT / "tests/fixtures/h263-plus-164x100.h263")], check=True)
