"""Scaled CAPTURE geometry remains separate from reference pictures and SAR."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureScalingTests(unittest.TestCase):
    def test_even_quarter_limits_source_change_and_pixel_aspect(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
typedef uint32_t u32;typedef uint64_t u64;
#include "capture-geometry.h"
#include "../ge2d/ge2d-scale.h"
int main(void){
 struct meson_capture_geometry c={0};
 struct meson_capture_size size=meson_capture_resolve(&c,1920,1080);
 assert(size.width==1920&&size.height==1080);
 meson_capture_configure(&c,1920,1080,1280,720,true);
 size=meson_capture_resolve(&c,1920,1080);assert(size.width==1280&&size.height==720);
 size=meson_capture_resolve(&c,854,480);assert(size.width==854&&size.height==480);
 struct meson_capture_ratio ratio=meson_capture_pixelaspect(&c,1920,1080,1,1);
 assert(ratio.numerator==1&&ratio.denominator==1);
 meson_capture_configure(&c,1920,1080,960,1080,true);
 ratio=meson_capture_pixelaspect(&c,1920,1080,1,1);assert(ratio.numerator==1&&ratio.denominator==2);
 meson_capture_configure(&c,720,576,360,576,true);
 ratio=meson_capture_pixelaspect(&c,720,576,16,15);assert(ratio.numerator==15&&ratio.denominator==32);
 meson_capture_configure(&c,1920,1080,1280,720,false);
 assert(c.width==1920&&c.height==1080);
 meson_capture_configure(&c,1919,1079,640,360,true);
 assert(c.width==1919&&c.height==1079);
 /* All accepted sizes satisfy the actual scaler's phase constraints. */
 for(u32 w=2;w<=1920;w+=2)for(u32 h=2;h<=1088;h+=34){
  u32 requests[]={0,1,2,3,w/4,w/2,w-1,w,w+1,UINT32_MAX};
  for(unsigned i=0;i<sizeof(requests)/sizeof(requests[0]);i++){
   meson_capture_configure(&c,w,h,requests[i],requests[i],true);
   assert(!(c.width&1)&&!(c.height&1)&&c.width<=w&&c.height<=h);
   assert(c.width*4>=w&&c.height*4>=h&&c.width&&c.height);
   u32 step;bool pre;
   assert(!meson8b_ge2d_phase(w,c.width,&step,&pre));
   assert(!meson8b_ge2d_phase(h,c.height,&step,&pre));
   ratio=meson_capture_pixelaspect(&c,w,h,65521,65519);
   long double expected=(long double)65519*h*c.width/(65521.L*w*c.height);
   assert(fabsl((long double)ratio.numerator/ratio.denominator-expected)<1e-7L);
  }
 }
 meson_capture_configure(&c,1918,1086,480,1086,true);
 ratio=meson_capture_pixelaspect(&c,1918,1086,UINT32_MAX,UINT32_MAX-2);
 assert(ratio.numerator&&ratio.denominator);
 assert(fabsl((long double)ratio.numerator/ratio.denominator-
  ((long double)(UINT32_MAX-2)*1086*480/((long double)UINT32_MAX*1918*1086)))<1e-8L);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text('#include <errno.h>\n' + code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test'), '-lm'], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_stop_streaming_clears_capture_scaling_only_on_full_stop(self):
        source = (ROOT / 'vdec/vdec.c').read_text()
        start = source.index('static void vdec_stop_streaming_locked(')
        end = source.index('\nstatic void vdec_stop_streaming(', start)
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 2
#define VB2_BUF_STATE_ERROR 1
#define WRITE_ONCE(x,v) ((x)=(v))
#include "capture-geometry.h"
enum {STATUS_STOPPED,STATUS_INIT,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct vb2_queue {u32 type;};
struct vb2_v4l2_buffer {int unused;};
struct amvdec_codec_ops {void (*drain)(void *);};
struct amvdec_format {struct amvdec_codec_ops *codec_ops;};
struct amvdec_core {void *dev;int vdec_irq;void *cur_sess;};
struct amvdec_pool {int work;};
struct amvdec_session {struct amvdec_core *core;const struct amvdec_format *fmt_out;void *m2m_ctx,*priv,*vififo_vaddr,*recycle_thread;
 dma_addr_t vififo_paddr;u32 vififo_size,status,streamon_out,streamon_cap,dpb_size;
 struct meson_capture_geometry capture;struct amvdec_pool pool;};
static struct amvdec_session *vb2_get_drv_priv(struct vb2_queue *q){(void)q;extern struct amvdec_session session;return &session;}
static void disable_irq(int irq){(void)irq;}
static void enable_irq(int irq){(void)irq;}
static bool vdec_codec_needs_recycle(struct amvdec_session *s){(void)s;return false;}
static void kthread_stop(void *t){(void)t;}
static bool amvdec_has_pool(struct amvdec_session *s){(void)s;return false;}
static void cancel_work_sync(int *w){(void)w;}
static void vdec_poweroff(struct amvdec_session *s){(void)s;}
static void vdec_free_canvas(struct amvdec_session *s){(void)s;}
static void amvdec_pool_free(struct amvdec_session *s){(void)s;}
static void amvdec_pool_capture_stop(struct amvdec_session *s){(void)s;}
static void dma_free_coherent(void *d,u32 sz,void *v,dma_addr_t p){(void)d;(void)sz;(void)v;(void)p;}
static void vdec_reset_timestamps(struct amvdec_session *s){(void)s;}
static void vdec_reset_bufs_recycle(struct amvdec_session *s){(void)s;}
static void kfree(const void *p){(void)p;}
static struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(void *m){(void)m;return NULL;}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *m){(void)m;return NULL;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int st){(void)b;(void)st;}
static struct amvdec_codec_ops ops={0};
static struct amvdec_format fmt={.codec_ops=&ops};
static struct amvdec_core core={0};
struct amvdec_session session;
''' + source[start:end] + r'''
int main(void){
 struct vb2_queue out={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
 struct vb2_queue cap={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 /* Resolution change STREAMOFF(CAPTURE) must preserve scaled geometry. */
 session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.status=STATUS_NEEDS_RESUME,
  .streamon_out=1,.streamon_cap=1,.dpb_size=5,
  .capture={.source_width=1920,.source_height=1080,.width=1280,.height=720}};
 vdec_stop_streaming_locked(&cap);
 assert(session.status==STATUS_NEEDS_RESUME&&!session.streamon_cap);
 assert(session.dpb_size==5&&session.capture.width==1280&&session.capture.height==720);
 /* Stopping the other queue from STATUS_NEEDS_RESUME is a full stop. */
 vdec_stop_streaming_locked(&out);
 assert(session.status==STATUS_STOPPED&&!session.dpb_size&&!session.streamon_out);
 assert(!session.capture.source_width&&!session.capture.width&&!session.capture.height);
 /* Full stop (STATUS_RUNNING -> STATUS_STOPPED) must clear stale scaling. */
 session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.status=STATUS_RUNNING,
  .streamon_out=1,.streamon_cap=1,.dpb_size=5,
  .capture={.source_width=1920,.source_height=1080,.width=1280,.height=720}};
 vdec_stop_streaming_locked(&cap);
 assert(session.status==STATUS_STOPPED&&!session.dpb_size);
 assert(!session.capture.source_width&&!session.capture.width&&!session.capture.height);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_same_fd_reopen_clears_stale_capture_scaling(self):
        source = (ROOT / 'vdec/vdec.c').read_text()
        start = source.index('static int vdec_start_streaming_locked(')
        end = source.index('\nstatic int vdec_start_streaming(', start)
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define SZ_4M (4*1024*1024)
#define SIZE_VIFIFO SZ_4M
#define GFP_KERNEL 0
#define VDEC_REVISION_MESON8B 1
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 2
#define V4L2_FMT_FLAG_DYN_RESOLUTION 1
#define VB2_BUF_STATE_QUEUED 0
#define WRITE_ONCE(x,v) ((x)=(v))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define dev_err(...) ((void)0)
#include "capture-geometry.h"
enum {STATUS_STOPPED,STATUS_INIT,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct vb2_queue {u32 type;};
struct vb2_v4l2_buffer {int unused;};
struct amvdec_codec_ops {void (*resume)(void *);};
struct amvdec_format {u32 flags;struct amvdec_codec_ops *codec_ops;};
struct platform {u32 revision;};
struct amvdec_core {struct platform *platform;void *dev;int vdec_irq;void *cur_sess;};
struct amvdec_session {struct amvdec_core *core;const struct amvdec_format *fmt_out;void *m2m_ctx,*priv,*vififo_vaddr,*recycle_thread;
 dma_addr_t vififo_paddr;u32 vififo_size,status,streamon_out,streamon_cap,should_stop,keyframe_found,last_offset,wrap_count;
 u32 reference_width,reference_height,crop_left,crop_top,dpb_size,sequence_cap,sequence_out;
 struct {u32 numerator,denominator;} pixelaspect;struct meson_capture_geometry capture;
 int esparser_queued_bufs,ctrl_min_buf_capture,esparser_queue_work;bool stop_requested;};
static struct amvdec_session *vb2_get_drv_priv(struct vb2_queue *q){(void)q;extern struct amvdec_session session;return &session;}
static void *dma_alloc_coherent(void *d,u32 sz,dma_addr_t *p,int g){(void)d;(void)sz;(void)g;static char v; *p=0x100000;return &v;}
static void dma_free_coherent(void *d,u32 sz,void *v,dma_addr_t p){(void)d;(void)sz;(void)v;(void)p;}
static void kfree(const void *p){(void)p;}
static void atomic_set(int *p,int v){*p=v;}
static void v4l2_ctrl_s_ctrl(int c,int v){(void)c;extern struct amvdec_session session;session.ctrl_min_buf_capture=v;}
static void disable_irq(int irq){(void)irq;}
static void enable_irq(int irq){(void)irq;}
static int vdec_poweron(struct amvdec_session *s){(void)s;return 0;}
static void vdec_poweroff(struct amvdec_session *s){(void)s;}
static bool vdec_codec_needs_recycle(struct amvdec_session *s){(void)s;return false;}
static void *kthread_run(void *fn,void *d,const char *n){(void)fn;(void)d;(void)n;return NULL;}
static int vdec_recycle_thread(void *d){(void)d;return 0;}
static void schedule_work(int *w){(void)w;}
static bool vdec_capture_queue_fits(struct amvdec_session *s,struct vb2_queue *q){(void)s;(void)q;return false;}
static struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(void *m){(void)m;return NULL;}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *m){(void)m;return NULL;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int st){(void)b;(void)st;}
static struct platform plat={.revision=VDEC_REVISION_MESON8B};
static struct amvdec_codec_ops ops={0};
static struct amvdec_format fmt={.flags=V4L2_FMT_FLAG_DYN_RESOLUTION,.codec_ops=&ops};
static struct amvdec_core core={.platform=&plat};
struct amvdec_session session;
''' + source[start:end] + r'''
int main(void){
 session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.status=STATUS_STOPPED,.dpb_size=5,
  .capture={.source_width=1920,.source_height=1080,.width=1280,.height=720}};
 struct vb2_queue out={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
 assert(!vdec_start_streaming_locked(&out,1));
 assert(session.status==STATUS_INIT&&!session.dpb_size);
 assert(!session.capture.source_width&&!session.capture.source_height&&!session.capture.width&&!session.capture.height);
 struct meson_capture_size resolved=meson_capture_resolve(&session.capture,1920,1080);
 assert(resolved.width==1920&&resolved.height==1080);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_stop_streaming_clears_capture_scaling_only_on_full_stop(self):
        source = (ROOT / 'vdec/vdec.c').read_text()
        start = source.index('static void vdec_stop_streaming_locked(')
        end = source.index('\nstatic void vdec_stop_streaming(', start)
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 2
#define VB2_BUF_STATE_ERROR 1
#define WRITE_ONCE(x,v) ((x)=(v))
#include "capture-geometry.h"
enum {STATUS_STOPPED,STATUS_INIT,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct vb2_queue {u32 type;};
struct vb2_v4l2_buffer {int unused;};
struct amvdec_codec_ops {void (*drain)(void *);};
struct amvdec_format {struct amvdec_codec_ops *codec_ops;};
struct amvdec_core {void *dev;int vdec_irq;void *cur_sess;};
struct amvdec_pool {int work;};
struct amvdec_session {struct amvdec_core *core;const struct amvdec_format *fmt_out;void *m2m_ctx,*priv,*vififo_vaddr,*recycle_thread;
 dma_addr_t vififo_paddr;u32 vififo_size,status,streamon_out,streamon_cap,dpb_size;
 struct meson_capture_geometry capture;struct amvdec_pool pool;};
static struct amvdec_session *vb2_get_drv_priv(struct vb2_queue *q){(void)q;extern struct amvdec_session session;return &session;}
static void disable_irq(int irq){(void)irq;}
static void enable_irq(int irq){(void)irq;}
static bool vdec_codec_needs_recycle(struct amvdec_session *s){(void)s;return false;}
static void kthread_stop(void *t){(void)t;}
static bool amvdec_has_pool(struct amvdec_session *s){(void)s;return false;}
static void cancel_work_sync(int *w){(void)w;}
static void vdec_poweroff(struct amvdec_session *s){(void)s;}
static void vdec_free_canvas(struct amvdec_session *s){(void)s;}
static void amvdec_pool_free(struct amvdec_session *s){(void)s;}
static void amvdec_pool_capture_stop(struct amvdec_session *s){(void)s;}
static void dma_free_coherent(void *d,u32 sz,void *v,dma_addr_t p){(void)d;(void)sz;(void)v;(void)p;}
static void vdec_reset_timestamps(struct amvdec_session *s){(void)s;}
static void vdec_reset_bufs_recycle(struct amvdec_session *s){(void)s;}
static void kfree(const void *p){(void)p;}
static struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(void *m){(void)m;return NULL;}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *m){(void)m;return NULL;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int st){(void)b;(void)st;}
static struct amvdec_codec_ops ops={0};
static struct amvdec_format fmt={.codec_ops=&ops};
static struct amvdec_core core={0};
struct amvdec_session session;
''' + source[start:end] + r'''
int main(void){
 struct vb2_queue out={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
 struct vb2_queue cap={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 /* Resolution change STREAMOFF(CAPTURE) must preserve scaled geometry. */
 session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.status=STATUS_NEEDS_RESUME,
  .streamon_out=1,.streamon_cap=1,.dpb_size=5,
  .capture={.source_width=1920,.source_height=1080,.width=1280,.height=720}};
 vdec_stop_streaming_locked(&cap);
 assert(session.status==STATUS_NEEDS_RESUME&&!session.streamon_cap);
 assert(session.dpb_size==5&&session.capture.width==1280&&session.capture.height==720);
 /* Stopping the other queue from STATUS_NEEDS_RESUME is a full stop. */
 vdec_stop_streaming_locked(&out);
 assert(session.status==STATUS_STOPPED&&!session.dpb_size&&!session.streamon_out);
 assert(!session.capture.source_width&&!session.capture.width&&!session.capture.height);
 /* Full stop (STATUS_RUNNING -> STATUS_STOPPED) must clear stale scaling. */
 session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.status=STATUS_RUNNING,
  .streamon_out=1,.streamon_cap=1,.dpb_size=5,
  .capture={.source_width=1920,.source_height=1080,.width=1280,.height=720}};
 vdec_stop_streaming_locked(&cap);
 assert(session.status==STATUS_STOPPED&&!session.dpb_size);
 assert(!session.capture.source_width&&!session.capture.width&&!session.capture.height);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
