"""A HEVC reference may end during copying, but its slot stays unavailable."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class PoolCopyLockTests(unittest.TestCase):
    def test_irq_can_queue_while_copy_keeps_its_slot(self):
        s = (ROOT / "vdec/meson8b-capture.c").read_text()
        a = s.index("static void amvdec_pool_deliver_locked(")
        b = s.index("\n/* The picture in @slot", a)
        c = s.index("void amvdec_pool_push(", b)
        d = s.index("\nstatic void amvdec_pool_work(", c)
        code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
#include "CAPTURE_HEADER"
#include "POOL_HEADER"
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define max(a,b) ((a)>(b)?(a):(b))
#define VB2_BUF_STATE_DONE 1
#define VB2_BUF_STATE_ERROR 2
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
struct vb2_buffer {u64 timestamp;};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 sequence,flags,field,timecode;};
struct amvdec_vb2_buffer {struct vb2_v4l2_buffer vb;int list;bool eos;};
struct amvdec_geometry {u32 width,height;};
struct amvdec_picture {struct amvdec_geometry geo;u64 timestamp;u32 flags,field,timecode;bool restart,eos;};
struct amvdec_shadow {int unused;};
struct amvdec_frame_pool {int lock,delivery_lock,work;struct meson8b_pool state;struct amvdec_picture picture[24];struct amvdec_shadow slot[24];};
struct amvdec_codec_ops {void *recycle;};struct format {struct amvdec_codec_ops *codec_ops;};
struct core {void *dev;u32 pool_waiting_peak;};
struct amvdec_session {struct meson_capture_geometry capture;struct format *fmt_out;struct core *core;struct amvdec_frame_pool pool;u32 sequence_cap;void *m2m_ctx;};
static struct amvdec_session session;static struct amvdec_vb2_buffer buffers[2];
static int pool_locked,delivery_locked,copies,cursor,done,scheduled;
static void mutex_lock(int *p){if(p==&session.pool.lock){assert(!pool_locked);pool_locked=1;}else{assert(!delivery_locked);delivery_locked=1;}}
static void mutex_unlock(int *p){if(p==&session.pool.lock){assert(pool_locked);pool_locked=0;}else{assert(delivery_locked);delivery_locked=0;}}
static u32 amvdec_get_output_size_wh(u32 w,u32 h){assert(w==8&&h==8);return 128;}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *p){(void)p;return cursor<2?&buffers[cursor++].vb:0;}
static void amvdec_recycle(struct amvdec_session *s,u32 slot){(void)s;(void)slot;assert(0);}
static void amvdec_abort(struct amvdec_session *s){(void)s;assert(0);}
static void vb2_set_plane_payload(struct vb2_buffer *b,int p,u32 n){(void)b;assert(n==(p?64u:128u));}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){(void)b;assert(state==1);done++;}
static void amvdec_pool_end_locked(struct amvdec_session *s){assert(!s->pool.state.len);}
static void schedule_work(int *p){(void)p;assert(!pool_locked);scheduled++;}
static void amvdec_pool_push(struct amvdec_session *,u32,const struct amvdec_picture *);
static int amvdec_meson8b_copy_capture(struct amvdec_session *s,const struct amvdec_geometry *g,const struct amvdec_shadow *shadow,struct vb2_buffer *b){(void)g;(void)shadow;(void)b;assert(!pool_locked && delivery_locked);assert(s->pool.state.waiting&1);if(!copies){assert(!meson8b_pool_release(&s->pool.state,0));assert(meson8b_pool_take(&s->pool.state)==1);struct amvdec_picture next={.geo={16,16},.timestamp=84,.eos=true};amvdec_pool_push(s,1,&next);}copies++;return 0;}
""".replace("POOL_HEADER", str(ROOT / "vdec/meson8b-pool.h")).replace("CAPTURE_HEADER", str(ROOT / "vdec/capture-geometry.h"))
        # The driver function is global; keep that linkage in the declaration.
        code = code.replace(
            "static void amvdec_pool_push(struct amvdec_session *,",
            "void amvdec_pool_push(struct amvdec_session *,",
        )
        # Check the current queued slot on both copies.
        code = code.replace(
            "assert(s->pool.state.waiting&1);",
            "assert(s->pool.state.waiting&(1u<<copies));",
        )
        code += (
            s[a:b]
            + s[c:d]
            + r"""
int main(void){struct core core={0};struct amvdec_codec_ops ops={0};struct format f={&ops};session.core=&core;session.fmt_out=&f;meson_capture_configure(&session.capture,16,16,8,8,true);assert(!meson8b_pool_reset(&session.pool.state,3));assert(meson8b_pool_take(&session.pool.state)==0);assert(!meson8b_pool_push(&session.pool.state,0));session.pool.picture[0]=(struct amvdec_picture){.geo={16,16},.timestamp=42};mutex_lock(&session.pool.delivery_lock);mutex_lock(&session.pool.lock);amvdec_pool_deliver_locked(&session);mutex_unlock(&session.pool.lock);mutex_unlock(&session.pool.delivery_lock);assert(copies==2 && done==2 && scheduled==1 && !session.pool.state.len);assert(buffers[0].vb.vb2_buf.timestamp==42 && buffers[1].vb.vb2_buf.timestamp==84 && buffers[1].eos);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory)
            (p / "test.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
