"""V4L2 compressed OUTPUT bytesused includes its data_offset prefix."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DecoderInputOffsetTests(unittest.TestCase):
    def test_vp9_header_rewrite_preserves_the_output_prefix(self):
        source = (ROOT / "vdec/esparser.c").read_text()
        body = source[source.index("static int vp9_update_header("):
                      source.index("/* Pad the packet")]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
#define VP9_HEADER_SIZE 16
#define dev_warn(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
struct vb2_buffer {u8 *data;u32 payload;unsigned long capacity;struct {u32 data_offset;}planes[1];};
static void *vb2_plane_vaddr(struct vb2_buffer *b,int p){assert(!p);return b->data;}
static unsigned long vb2_plane_size(struct vb2_buffer *b,int p){assert(!p);return b->capacity;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){assert(!p);return b->payload;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int p,u32 n){assert(!p);b->payload=n;}
#include "vdec-input.h"
struct amvdec_core {void *dev;};
''' + body + r'''
int main(void){u8 data[80];struct vb2_buffer b={.data=data,.payload=13+4,.capacity=sizeof(data),.planes={{13}}};struct amvdec_core c={0};
 memset(data,0xa5,sizeof(data));memcpy(data+13,"abcd",4);
 assert(vp9_update_header(&c,&b)==20&&!memcmp(data+13+16,"abcd",4));
 assert(!memcmp(data+13+12,"AMLV",4));for(u32 i=0;i<13;i++)assert(data[i]==0xa5);
 b.capacity=13+20;b.payload=13+4;memcpy(data+13,"abcd",4);assert(!vp9_update_header(&c,&b));
 b.capacity=sizeof(data);b.payload=13;assert(!vp9_update_header(&c,&b));
 b.payload=14;data[13]=0xc7;assert(!vp9_update_header(&c,&b));
 return 0;}
'''
        self.run_c(code)

    def test_output_prepare_rejects_invalid_spans_before_parser_work(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        body = source[source.index("static int vdec_vb2_buf_prepare("):
                      source.index("static void vdec_vb2_buf_finish(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
typedef uint32_t u32;
#define V4L2_TYPE_IS_OUTPUT(t) ((t)==1)
#define V4L2_TYPE_IS_CAPTURE(t) ((t)==2)
#define V4L2_FIELD_NONE 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct vb2_buffer {void *vb2_queue;u32 type,num_planes;struct {u32 data_offset,length,bytesused;}planes[2];};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 field;};
struct amvdec_vb2_buffer {struct vb2_v4l2_buffer vb;bool eos;};
struct core {void *ge2d_dev;};struct amvdec_session {struct core *core;};
static struct core core;static struct amvdec_session sess={&core};
static struct amvdec_session *vb2_get_drv_priv(void *q){(void)q;return &sess;}
static struct vb2_v4l2_buffer *to_vb2_v4l2_buffer(struct vb2_buffer *b){return (void *)b;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){return b->planes[p].bytesused;}
static u32 vb2_plane_size(struct vb2_buffer *b,int p){return b->planes[p].length;}
''' + body + r'''
int main(void){struct amvdec_vb2_buffer b={.vb.vb2_buf={.type=1,.num_planes=1,.planes={{.data_offset=32,.length=8192,.bytesused=35}}},.eos=true};
 assert(!vdec_vb2_buf_prepare(&b.vb.vb2_buf)&&!b.eos);
 b.eos=true;b.vb.vb2_buf.planes[0].bytesused=31;assert(vdec_vb2_buf_prepare(&b.vb.vb2_buf)==-EINVAL&&b.eos);
 b.vb.vb2_buf.planes[0].bytesused=32;assert(vdec_vb2_buf_prepare(&b.vb.vb2_buf)==-EINVAL);
 b.vb.vb2_buf.planes[0].bytesused=8193;assert(vdec_vb2_buf_prepare(&b.vb.vb2_buf)==-EINVAL);
 b.vb.vb2_buf.planes[0].bytesused=35;b.vb.vb2_buf.num_planes=2;assert(vdec_vb2_buf_prepare(&b.vb.vb2_buf)==-EINVAL);
 b.vb.vb2_buf.type=2;core.ge2d_dev=&core;assert(vdec_vb2_buf_prepare(&b.vb.vb2_buf)==-EINVAL);
 b.vb.vb2_buf.planes[0].data_offset=0;assert(!vdec_vb2_buf_prepare(&b.vb.vb2_buf));
 return 0;}
'''
        self.run_c(code)

    def test_parser_padding_starts_after_the_actual_payload(self):
        source = (ROOT / "vdec/esparser.c").read_text()
        body = source[source.index("static int esparser_pad_start_code("):
                      source.index("static int\nesparser_write_data(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
#define dev_warn(...) ((void)0)
#include "esparser-padding.h"
struct vb2_buffer {u8 *data;unsigned long capacity;u32 payload;struct {u32 data_offset;}planes[1];};
static void *vb2_plane_vaddr(struct vb2_buffer *b,int p){assert(!p);return b->data;}
static unsigned long vb2_plane_size(struct vb2_buffer *b,int p){assert(!p);return b->capacity;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){assert(!p);return b->payload;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int p,u32 n){assert(!p);b->payload=n;}
#include "vdec-input.h"
struct amvdec_core {void *dev;};
''' + body + r'''
int main(void){u8 data[8192],copy[8192];struct vb2_buffer b={.data=data,.capacity=sizeof(data),.payload=35,.planes={{32}}};struct amvdec_core c={0};
 memset(data,0xa5,sizeof(data));
 assert(esparser_pad_start_code(&c,&b,3)==4093);
 for(u32 i=0;i<35;i++)assert(data[i]==0xa5);
 assert(data[32+4098]==1&&data[32+4099]==0xff&&data[32+4608]==0xa5);
 memset(data,0xa5,sizeof(data));memcpy(copy,data,sizeof(data));b.capacity=32+4607;
 assert(esparser_pad_start_code(&c,&b,3)==-ENOSPC&&!memcmp(data,copy,sizeof(data)));
 return 0;}
'''
        self.run_c(code)

    @staticmethod
    def run_c(code):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-sign-compare",
                            "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "vdec"),
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
