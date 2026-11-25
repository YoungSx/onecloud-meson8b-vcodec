"""Real H.264 source-change callback closes the previous capture sequence."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class H264SourceChangeTests(unittest.TestCase):
    def test_capture_last_precedes_new_geometry_event(self):
        source = (ROOT / 'vdec/codec_h264.c').read_text()
        body = source[source.index('static void codec_h264_src_change('):
                      source.index('/*\n * The bitstream offset')]
        defines = '\n'.join(line for line in source.splitlines() if line.startswith('#define ') and
                            any(word in line for word in ['MB_TOTAL', 'MB_WIDTH', 'MAX_REF']))
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#define GENMASK(h,l) ((~0u << (l)) & (~0u >> (31-(h))))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define VDEC_REVISION_MESON8B 1
#define AMVDEC_POOL_RESTART 3
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#include "dos_regs.h"
''' + defines + r'''
struct codec_h264 {u32 mb_width,mb_height,max_refs;};
struct format {u32 max_width,max_height,max_buffers;};
struct platform {u32 revision;};
struct amvdec_core {void *dev;struct platform *platform;};
struct amvdec_session {struct amvdec_core *core;void *priv;struct format *fmt_out;
 u32 width,height;bool streamon_cap,recycle_paused;int bufs_recycle_lock,keyframe_found;};
static int h264_extra_buffers=2,markers,events,resumes,aborts;static bool pooled=true;
static u32 parsed,crop;
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return r==AV_SCRATCH_1?parsed:crop;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static void mutex_lock(int *p){(void)p;}
static void mutex_unlock(int *p){(void)p;}
static bool amvdec_has_pool(struct amvdec_session *s){(void)s;return pooled;}
static void codec_h264_set_par(struct amvdec_session *s){(void)s;}
static void amvdec_pool_end(struct amvdec_session *s,int end);
static void amvdec_src_change(struct amvdec_session *s,u32 w,u32 h,u32 dpb){
 assert(dpb==4);if(w==s->width&&h==s->height){resumes++;return;}
 assert(!s->streamon_cap||!pooled||markers==events+1);events++;
}
''' + body + r'''
static void amvdec_pool_end(struct amvdec_session *s,int end){assert(end==AMVDEC_POOL_RESTART&&s->width==320&&s->height==240);markers++;}
int main(void){struct codec_h264 h={0};struct format f={1920,1088,24};struct platform p={1};
 struct amvdec_core c={.platform=&p};struct amvdec_session s={.core=&c,.priv=&h,.fmt_out=&f,.width=320,.height=240};
 parsed=120|(8160u<<MB_TOTAL_BIT)|(1u<<MAX_REF_BIT);crop=8;
 codec_h264_src_change(&s);assert(events==1&&!markers&&!aborts);
 events=0;s.streamon_cap=true;codec_h264_src_change(&s);assert(markers==1&&events==1&&s.recycle_paused);
 s.width=1920;s.height=1080;codec_h264_src_change(&s);assert(resumes==1&&markers==1&&events==1);
 s.width=320;s.height=240;pooled=false;codec_h264_src_change(&s);assert(markers==1&&events==2);
 parsed=0;codec_h264_src_change(&s);assert(aborts==1&&markers==1&&events==2);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
