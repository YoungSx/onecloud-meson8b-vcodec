"""The real VC-1 input hook must transform Annex L without changing WVC1."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Wmv3InputTests(unittest.TestCase):
    def test_sequence_state_payload_and_wrong_geometry(self):
        source = (ROOT / "vdec/codec_vc1.c").read_text()
        body = source[source.index("static int codec_vc1_inspect_input("):
                      source.index("/* End of sequence")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;typedef uint8_t u8;
#define V4L2_PIX_FMT_VC1_ANNEX_L 1
#include "wmv3-header.h"
struct codec_vc1 {bool sequence_seen;};
struct format {u32 pixfmt;};
struct amvdec_session {void *priv;struct format *fmt_out;u32 width,height;};
struct vb2_buffer {u8 *data;u32 payload,capacity;struct {u32 data_offset;}planes[1];};
static u8 *vb2_plane_vaddr(struct vb2_buffer *b,int p){assert(!p);return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){assert(!p);return b->payload;}
static u32 vb2_plane_size(struct vb2_buffer *b,int p){assert(!p);return b->capacity;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int p,u32 n){assert(!p);b->payload=n;}
#include "vdec-input.h"
''' + body + r'''
int main(void){
 u8 data[128]={0};data[3]=0xc5;data[4]=4;data[8]=0x40;
 data[12]=240;data[16]=64;data[17]=1;data[20]=12;data[36]=3;
 memcpy(data+44,"abc",3);
 struct codec_vc1 v={0};struct format f={1};struct amvdec_session s={&v,&f,320,240};
 memmove(data+13,data,47);memset(data,0xa5,13);
 struct vb2_buffer b={.data=data,.payload=13+47,.capacity=sizeof(data),.planes={{13}}};
 s.width=640;assert(codec_vc1_inspect_input(&s,&b,0)==-EINVAL&&!v.sequence_seen&&b.payload==13+47);
 s.width=320;assert(!codec_vc1_inspect_input(&s,&b,0)&&v.sequence_seen&&b.payload==13+55);
 assert(!memcmp(data+13+52,"abc",3));for(u32 i=0;i<13;i++)assert(data[i]==0xa5);
 b.planes[0].data_offset=0;
 memset(data,0,sizeof(data));data[0]=3;memcpy(data+8,"def",3);b.payload=11;
 assert(!codec_vc1_inspect_input(&s,&b,0)&&b.payload==25&&!memcmp(data+22,"def",3));
 f.pixfmt=2;b.data=NULL;b.payload=0;assert(!codec_vc1_inspect_input(&s,&b,0));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)
