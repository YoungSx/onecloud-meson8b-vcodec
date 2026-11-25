"""Run the real MJPEG input hook on a payload with a nonzero data_offset."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MjpegInputTests(unittest.TestCase):
    def test_header_validation_precedes_parser_dma(self):
        source = (ROOT / 'vdec/codec_mjpeg.c').read_text()
        self.assertIn('.inspect_input = codec_mjpeg_inspect_input,', source)
        hook = source[source.index('static int codec_mjpeg_inspect_input('):
                      source.index('static int codec_mjpeg_can_recycle(')]
        fixture = (ROOT / 'tests/test_mjpeg_header.c').read_text()
        fixture = fixture[fixture.index('static const u8 header[]'):]
        fixture = fixture[:fixture.index('};') + 2]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;typedef uint8_t u8;
struct vb2_buffer {u8 *data;u32 payload;unsigned long capacity;struct {u32 data_offset;} planes[1];};
static void *vb2_plane_vaddr(struct vb2_buffer *b,int plane){assert(!plane);return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int plane){assert(!plane);return b->payload;}
static unsigned long vb2_plane_size(struct vb2_buffer *b,int plane){assert(!plane);return b->capacity;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int plane,u32 n){assert(!plane);b->payload=n;}
#include "vdec-input.h"
#include "mjpeg-header.h"
struct amvdec_session {u32 width,height;};
''' + fixture + hook + r'''
int main(void){
 u8 data[128],saved[128];memset(data,0xa5,sizeof(data));memcpy(data+13,header,sizeof(header));memcpy(saved,data,sizeof(data));
 struct vb2_buffer vb={.data=data,.payload=13+sizeof(header),.capacity=sizeof(data),.planes={{13}}};
 struct amvdec_session s={320,240};
 assert(!codec_mjpeg_inspect_input(&s,&vb,0));
 assert(vb.payload==13+sizeof(header)&&!memcmp(data,saved,sizeof(data)));
 s.width=640;assert(codec_mjpeg_inspect_input(&s,&vb,0)==-EINVAL);
 s.width=320;data[13+8+1]=0xc2;assert(codec_mjpeg_inspect_input(&s,&vb,0)==-EOPNOTSUPP);
 data[13+8+1]=0xc0;data[13+8+14]=0x12;
 assert(codec_mjpeg_inspect_input(&s,&vb,0)==-EOPNOTSUPP);
 vb.payload=13;assert(codec_mjpeg_inspect_input(&s,&vb,0)==-EINVAL);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-fsanitize=address,undefined',
                            '-fno-pie', '-no-pie', '-I', str(ROOT / 'vdec'),
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
