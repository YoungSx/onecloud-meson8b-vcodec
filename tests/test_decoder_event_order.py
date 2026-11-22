"""EOS follows dequeue of its LAST buffer and never a source-change LAST."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).parents[1]
class EventOrderTests(unittest.TestCase):
 def test_flush_then_dequeue_publishes_one_eos(self):
  p=(ROOT/'vdec/meson8b-capture.c').read_text();a=p.index('static void amvdec_pool_end_locked(');b=p.index('\n/* Convert waiting pictures',a)
  s=(ROOT/'vdec/vdec.c').read_text();c=s.index('static void vdec_vb2_buf_finish(');d=s.index('\nstatic const struct vb2_ops',c)
  code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define V4L2_EVENT_EOS 2
#define V4L2_BUF_FLAG_LAST 1
#define V4L2_FIELD_NONE 0
#define VB2_BUF_STATE_DONE 1
#define V4L2_TYPE_IS_CAPTURE(t) ((t)==9)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct vb2_buffer {unsigned int num_planes,type;void *vb2_queue;};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;unsigned int flags,field;};
struct amvdec_vb2_buffer {struct vb2_v4l2_buffer vb;int list;bool eos;};
struct amvdec_frame_pool {int end;};
struct amvdec_session {struct amvdec_frame_pool pool;void *m2m_ctx;int fh,should_stop;};
struct v4l2_event {int type;};
enum {AMVDEC_POOL_RUNNING,AMVDEC_POOL_FLUSH,AMVDEC_POOL_LAST};
static struct amvdec_session session;static struct amvdec_vb2_buffer buffers[2];
static int cursor,events,returned,dequeue;
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *p){(void)p;return cursor<2?&buffers[cursor++].vb:0;}
static void vb2_set_plane_payload(struct vb2_buffer *b,unsigned int i,unsigned int n){(void)b;(void)i;assert(!n);}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){(void)b;assert(state==1 && !events);returned++;}
static void v4l2_m2m_last_buffer_done(void *p,struct vb2_v4l2_buffer *b){(void)p;b->flags=1;v4l2_m2m_buf_done(b,1);}
static struct amvdec_session *vb2_get_drv_priv(void *p){(void)p;return &session;}
static struct vb2_v4l2_buffer *to_vb2_v4l2_buffer(struct vb2_buffer *b){return (void*)b;}
static void v4l2_event_queue_fh(int *fh,const struct v4l2_event *e){(void)fh;assert(dequeue && e->type==2);events++;}
'''+p[a:b]+s[c:d]+r'''
int main(void){for(int i=0;i<2;i++){buffers[i].vb.vb2_buf.num_planes=2;buffers[i].vb.vb2_buf.type=9;}session.pool.end=AMVDEC_POOL_FLUSH;amvdec_pool_end_locked(&session);assert(!events && returned==2 && !buffers[0].eos && buffers[1].eos);dequeue=1;vdec_vb2_buf_finish(&buffers[0].vb.vb2_buf);assert(!events);vdec_vb2_buf_finish(&buffers[1].vb.vb2_buf);assert(events==1 && !buffers[1].eos);vdec_vb2_buf_finish(&buffers[1].vb.vb2_buf);assert(events==1);buffers[0].vb.flags=V4L2_BUF_FLAG_LAST;vdec_vb2_buf_finish(&buffers[0].vb.vb2_buf);assert(events==1);buffers[0].eos=true;buffers[0].vb.vb2_buf.type=10;vdec_vb2_buf_finish(&buffers[0].vb.vb2_buf);assert(events==1);return 0;}
'''
  with tempfile.TemporaryDirectory() as directory:
   r=pathlib.Path(directory);(r/'test.c').write_text(code);subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(r/'test.c'),'-o',str(r/'test')],check=True);subprocess.run([str(r/'test')],check=True)
