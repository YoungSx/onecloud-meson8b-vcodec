"""Two file handles cannot race the singleton decoder's stream lifecycle."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class DecoderExclusiveTests(unittest.TestCase):
    def test_lifecycle_serializes_across_sessions(self):
        s = (ROOT / "vdec/vdec.c").read_text()
        a = s.index("static int vdec_start_streaming(")
        b = s.index("\nstatic void vdec_free_canvas(", a)
        c = s.index("static void vdec_stop_streaming(")
        d = s.index("\nstatic int vdec_vb2_buf_prepare(", c)
        code = (
            r"""
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
struct amvdec_session;struct core {pthread_mutex_t session_lock;struct amvdec_session *owner;int locked;};
struct amvdec_session {struct core *core;};struct vb2_queue {struct amvdec_session *session;};
static pthread_barrier_t gate;
static struct amvdec_session *vb2_get_drv_priv(struct vb2_queue *q){return q->session;}
static void mutex_lock(pthread_mutex_t *p){assert(!pthread_mutex_lock(p));}
static void mutex_unlock(pthread_mutex_t *p){assert(!pthread_mutex_unlock(p));}
static int vdec_start_streaming_locked(struct vb2_queue *q,unsigned int n){(void)n;struct core *c=q->session->core;assert(++c->locked==1);int result=c->owner && c->owner!=q->session?-EBUSY:0;usleep(1000);if(!result)c->owner=q->session;assert(--c->locked==0);return result;}
static void vdec_stop_streaming_locked(struct vb2_queue *q){struct core *c=q->session->core;assert(++c->locked==1);if(c->owner==q->session)c->owner=0;assert(--c->locked==0);}
"""
            + s[a:b]
            + s[c:d]
            + r"""
struct attempt {struct vb2_queue q;int result;};
static void *start(void *p){struct attempt *a=p;pthread_barrier_wait(&gate);a->result=vdec_start_streaming(&a->q,1);return 0;}
int main(void){struct core c={.session_lock=PTHREAD_MUTEX_INITIALIZER};struct amvdec_session a={&c},b={&c};struct attempt attempts[2]={{{&a},0},{{&b},0}};pthread_t threads[2];pthread_barrier_init(&gate,0,2);for(int i=0;i<2;i++)pthread_create(&threads[i],0,start,&attempts[i]);for(int i=0;i<2;i++)pthread_join(threads[i],0);assert((attempts[0].result==0 && attempts[1].result==-EBUSY)||(attempts[1].result==0 && attempts[0].result==-EBUSY));int win=attempts[0].result==0?0:1;vdec_stop_streaming(&attempts[1-win].q);assert(c.owner==attempts[win].q.session);vdec_stop_streaming(&attempts[win].q);assert(!c.owner);assert(!vdec_start_streaming(&attempts[1-win].q,1));return 0;}
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
                    "-pthread",
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
