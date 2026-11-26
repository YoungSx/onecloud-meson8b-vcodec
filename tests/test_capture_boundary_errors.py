"""Conversion errors retain source-change LAST and final EOS metadata."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureBoundaryErrorTests(unittest.TestCase):
    def test_scaled_old_tail_errors_and_empty_boundaries(self):
        capture = (ROOT / 'vdec/meson8b-capture.c').read_text()
        delivery = capture[capture.index('static void amvdec_pool_end_locked('):
                           capture.index('/* The picture in @slot is finished;')]
        vdec = (ROOT / 'vdec/vdec.c').read_text()
        finish = vdec[vdec.index('static void vdec_vb2_buf_finish('):
                      vdec.index('static const struct vb2_ops')]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <linux/videodev2.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define VB2_BUF_STATE_DONE 1
#define VB2_BUF_STATE_ERROR 2
#define dev_warn(...) ((void)0)
#include "meson8b-pool.h"
#include "capture-geometry.h"
enum amvdec_pool_end {AMVDEC_POOL_RUNNING,AMVDEC_POOL_FLUSH,AMVDEC_POOL_LAST,AMVDEC_POOL_RESTART};
struct vb2_buffer {unsigned type,num_planes;void *vb2_queue;u32 size[2],payload[2];u64 timestamp;};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 sequence,flags,field;struct v4l2_timecode timecode;};
struct amvdec_vb2_buffer {struct vb2_v4l2_buffer vb;bool eos;int state;};
struct amvdec_geometry {u32 width,height;};
struct amvdec_picture {struct amvdec_geometry geo;u64 timestamp;u32 flags,field;struct v4l2_timecode timecode;bool restart,eos;};
struct amvdec_shadow {int unused;};
struct amvdec_frame_pool {int lock;struct meson8b_pool state;struct amvdec_picture picture[4];struct amvdec_shadow slot[4];enum amvdec_pool_end end;};
struct amvdec_codec_ops {void *recycle;};struct format {struct amvdec_codec_ops *codec_ops;};
struct core {void *dev;};
struct amvdec_session {struct core *core;struct format *fmt_out;struct meson_capture_geometry capture;struct amvdec_frame_pool pool;u32 width,height,sequence_cap;int should_stop,fh;void *m2m_ctx;};
static struct amvdec_session session;static struct amvdec_vb2_buffer buffers[4];
static unsigned cursor,available,done,copies,events;static int fail_copy;static bool scaled_case;
static void mutex_lock(int *lock){assert(!*lock);*lock=1;}
static void mutex_unlock(int *lock){assert(*lock);*lock=0;}
static u32 amvdec_get_output_size_wh(u32 w,u32 h){return ALIGN(ALIGN(w,32)*ALIGN(h,32),65536);}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *ctx){assert(ctx==&session);return cursor<available?&buffers[cursor++].vb:NULL;}
static void vb2_set_plane_payload(struct vb2_buffer *b,unsigned p,u32 n){assert(p<2&&n<=b->size[p]);b->payload[p]=n;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){
 struct amvdec_vb2_buffer *v=container_of(b,struct amvdec_vb2_buffer,vb);assert(state==1||state==2);v->state=state;done++;
 if(state==2)b->flags|=V4L2_BUF_FLAG_ERROR;
}
static void v4l2_m2m_last_buffer_done(void *ctx,struct vb2_v4l2_buffer *b){assert(ctx==&session);b->flags|=V4L2_BUF_FLAG_LAST;v4l2_m2m_buf_done(b,VB2_BUF_STATE_DONE);}
static void amvdec_recycle(struct amvdec_session *s,u32 slot){(void)s;(void)slot;assert(0);}
static int amvdec_meson8b_copy_capture(struct amvdec_session *s,const struct amvdec_geometry *g,const struct amvdec_shadow *shadow,struct vb2_buffer *b){
 (void)shadow;assert(!s->pool.lock);assert((g->width==1920&&g->height==1080)||(g->width==640&&g->height==360));
 struct meson_capture_size c=meson_capture_resolve(&s->capture,g->width,g->height);
 assert(c.width==(g->width==1920?(scaled_case?1280u:1920u):(scaled_case?320u:640u))&&
 c.height==(g->width==1920?(scaled_case?720u:1080u):(scaled_case?180u:360u)));
 u32 size=amvdec_get_output_size_wh(c.width,c.height);assert(b->size[0]==size&&b->size[1]==size/2);
 /* The source has already changed while old scaled pictures await delivery. */
 s->width=640;s->height=360;copies++;return (int)copies==fail_copy?-ETIMEDOUT:0;
}
static struct amvdec_session *vb2_get_drv_priv(void *q){return q;}
static struct vb2_v4l2_buffer *to_vb2_v4l2_buffer(struct vb2_buffer *vb){return container_of(vb,struct vb2_v4l2_buffer,vb2_buf);}
static void v4l2_event_queue_fh(int *fh,const struct v4l2_event *e){assert(fh==&session.fh&&e->type==V4L2_EVENT_EOS);events++;}
''' + delivery + finish + r'''
static void allocate(unsigned n,u32 width,u32 height){
 memset(buffers,0,sizeof(buffers));cursor=done=0;available=n;
 u32 size=amvdec_get_output_size_wh(width,height);
 for(unsigned i=0;i<n;i++)buffers[i].vb.vb2_buf=(struct vb2_buffer){.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.num_planes=2,.vb2_queue=&session,.size={size,size/2},.payload={size,size/2}};
}
static void enqueue(u32 width,u32 height,u64 ts,bool last,bool eos,bool restart){
 int slot=meson8b_pool_take(&session.pool.state);assert(slot>=0);
 session.pool.picture[slot]=(struct amvdec_picture){.geo={width,height},.timestamp=ts,.flags=last?V4L2_BUF_FLAG_LAST:0,.field=V4L2_FIELD_NONE,.timecode={.type=17},.eos=eos,.restart=restart};
 assert(!meson8b_pool_push(&session.pool.state,slot));assert(!meson8b_pool_release(&session.pool.state,slot));
}
static void deliver(void){mutex_lock(&session.pool.lock);amvdec_pool_deliver_locked(&session);mutex_unlock(&session.pool.lock);}
static void scenario(unsigned boundary,bool failure,enum amvdec_pool_end empty_end){
 struct core c={0};struct amvdec_codec_ops ops={0};struct format f={&ops};
 memset(&session,0,sizeof(session));session.core=&c;session.fmt_out=&f;session.m2m_ctx=&session;session.width=1920;session.height=1080;
 session.sequence_cap=9;copies=events=0;fail_copy=failure?2:-1;
 assert(!meson8b_pool_reset(&session.pool.state,4));
 meson_capture_configure(&session.capture,1920,1080,1280,720,scaled_case);
 allocate(empty_end==AMVDEC_POOL_RUNNING?2:3,scaled_case?1280:1920,scaled_case?720:1080);
 enqueue(1920,1080,123,false,false,false);enqueue(1920,1080,456,boundary!=0,boundary==2,boundary==1);
 session.pool.end=empty_end;deliver();assert(done==available&&copies==2&&!events&&!session.pool.state.len);
 for(unsigned i=0;i<2;i++){
  struct amvdec_vb2_buffer *v=&buffers[i];bool error=failure&&i==1;
  assert(v->state==(error?VB2_BUF_STATE_ERROR:VB2_BUF_STATE_DONE));
  assert(v->vb.vb2_buf.timestamp==(i?456u:123u)&&v->vb.sequence==9+i);
  assert(v->vb.field==V4L2_FIELD_NONE&&v->vb.timecode.type==17);
  assert(v->vb.vb2_buf.payload[0]==(error?0:buffers[0].vb.vb2_buf.size[0]));
  assert(v->vb.vb2_buf.payload[1]==(error?0:buffers[0].vb.vb2_buf.size[1]));
  assert(!!(v->vb.flags&V4L2_BUF_FLAG_LAST)==(i==1&&boundary!=0));
  assert(v->eos==(i==1&&boundary==2));
 }
 if(empty_end!=AMVDEC_POOL_RUNNING){
  assert(!buffers[2].vb.vb2_buf.payload[0]&&!buffers[2].vb.vb2_buf.payload[1]&&(buffers[2].vb.flags&V4L2_BUF_FLAG_LAST));
  assert(buffers[2].eos==(empty_end!=AMVDEC_POOL_RESTART));
 }
 unsigned expected_events=boundary==2||(empty_end!=AMVDEC_POOL_RUNNING&&empty_end!=AMVDEC_POOL_RESTART);
 for(unsigned i=0;i<available;i++){vdec_vb2_buf_finish(&buffers[i].vb.vb2_buf);vdec_vb2_buf_finish(&buffers[i].vb.vb2_buf);}
 assert(events==expected_events);
 if(boundary==1||empty_end==AMVDEC_POOL_RESTART)assert(session.sequence_cap==0);
 /* The new segment negotiates its own size; no previous LAST/EOS survives. */
 meson_capture_configure(&session.capture,640,360,320,180,scaled_case);allocate(1,scaled_case?320:640,scaled_case?180:360);fail_copy=-1;session.should_stop=0;session.sequence_cap=0;
 enqueue(640,360,789,false,false,false);deliver();assert(done==1&&buffers[0].state==VB2_BUF_STATE_DONE);
 assert(buffers[0].vb.vb2_buf.timestamp==789&&buffers[0].vb.sequence==0&&!(buffers[0].vb.flags&V4L2_BUF_FLAG_LAST)&&!buffers[0].eos);
 vdec_vb2_buf_finish(&buffers[0].vb.vb2_buf);assert(events==expected_events);
}
int main(void){
 for(unsigned scaled=0;scaled<2;scaled++)for(unsigned failure=0;failure<2;failure++){
  scaled_case=scaled;
  for(unsigned boundary=0;boundary<3;boundary++)scenario(boundary,failure,AMVDEC_POOL_RUNNING);
  for(unsigned end=AMVDEC_POOL_FLUSH;end<=AMVDEC_POOL_RESTART;end++)scenario(0,failure,end);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
