"""Run the decoder's real size enumeration and H.263 input validation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class H263FormatTests(unittest.TestCase):
    def test_discrete_sizes_and_negotiated_picture_geometry(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        enum = source[source.index("static int vdec_enum_framesizes("):
                      source.index("static int\nvdec_decoder_cmd(")]
        source = (ROOT / "vdec/codec_mpeg4.c").read_text()
        inspect = source[source.index("static int codec_mpeg4_inspect_input("):
                         source.index("static int codec_mpeg4_can_recycle(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define V4L2_PIX_FMT_H263 1
#define V4L2_FRMSIZE_TYPE_DISCRETE 1
#define V4L2_FRMSIZE_TYPE_CONTINUOUS 2
#define SZ_1M 1048576
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
struct codec_mpeg4 {u8 *h263_output;};
struct amvdec_format {u32 pixfmt,max_width,max_height;};
struct platform {const struct amvdec_format *formats;u32 num_formats;};
struct core {struct platform *platform;};
struct amvdec_session {int fh;struct core *core;const struct amvdec_format *fmt_out;u32 width,height;void *priv;};
struct file {void *private_data;};
struct v4l2_frmsizeenum {u32 pixel_format,index,type;struct {u32 width,height;} discrete;
 struct {u32 min_width,max_width,step_width,min_height,max_height,step_height;} stepwise;};
struct vb2_buffer {u8 *data;u32 size,capacity;};
#define container_of(p,t,m) ((struct amvdec_session *)((char *)(p)-offsetof(struct amvdec_session,m)))
static const struct amvdec_format *find_format(const struct amvdec_format *f,u32 n,u32 id){for(u32 i=0;i<n;i++)if(f[i].pixfmt==id)return f+i;return NULL;}
static u8 *vb2_plane_vaddr(struct vb2_buffer *b,int plane){assert(!plane);return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int plane){assert(!plane);return b->size;}
static u32 vb2_plane_size(struct vb2_buffer *b,int plane){assert(!plane);return b->capacity;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int plane,u32 n){assert(!plane);b->size=n;}
static int h263_translate(const u8 *input,u32 n,u8 *output,u32 cap,u32 w,u32 h){(void)input;assert(n==7&&cap==4608&&w==176&&h==144);memset(output,42,20);return 20;}
#include "h263-header.h"
''' + enum + inspect + r'''
int main(void){
 struct amvdec_format formats[]={{1,704,576},{2,1920,1080}};struct platform p={formats,2};
 struct core c={&p};struct amvdec_session s={.core=&c,.fmt_out=&formats[0],.width=176,.height=144};
 struct file f={&s.fh};struct v4l2_frmsizeenum e={.pixel_format=1};
 for(u32 i=0;i<4;i++){e.index=i;assert(!vdec_enum_framesizes(&f,NULL,&e));assert(e.type==1);}
 e.index=4;assert(vdec_enum_framesizes(&f,NULL,&e)==-EINVAL);
 e.pixel_format=2;e.index=0;assert(!vdec_enum_framesizes(&f,NULL,&e)&&e.type==2);
 e.index=1;assert(vdec_enum_framesizes(&f,NULL,&e)==-EINVAL);
 u8 header[5120]={0,0,0x80,2,8,4,0x1e};u8 translated[5120];struct codec_mpeg4 codec={translated};s.priv=&codec;
 struct vb2_buffer b={header,7,sizeof(header)};
 assert(!codec_mpeg4_inspect_input(&s,&b)&&b.size==20&&header[0]==42);
 header[0]=header[1]=0;header[2]=0x80;header[3]=2;header[4]=8;header[5]=4;b.size=7;s.width=352;
 assert(codec_mpeg4_inspect_input(&s,&b)==-EINVAL);
 s.fmt_out=&formats[1];b.data=NULL;b.size=0;assert(!codec_mpeg4_inspect_input(&s,&b));
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
            subprocess.run([str(path / "test")], check=True)
