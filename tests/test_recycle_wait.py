"""Pin recycle pending accounting and wakeups in the real driver paths."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class RecycleWaitTests(unittest.TestCase):
    def test_queue_wakes_and_reset_clears_pending(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        enqueue = source[
            source.index("void amvdec_recycle(") : source.index(
                "/* Without a frame pool"
            )
        ]
        reset = source[
            source.index("static void vdec_reset_bufs_recycle(") : source.index(
                "static void vdec_stop_streaming("
            )
        ]
        header = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint32_t u32;
struct list_head {struct list_head *next,*prev;};
struct amvdec_buffer {struct list_head list;u32 fw_idx;};
struct amvdec_session {struct list_head bufs_recycle;int bufs_recycle_lock,recycle_wq;u32 recycle_pending;};
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kfree(p) free(p)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
static int wakeups;
#define wake_up_interruptible(p) ((void)(p),wakeups++)
static void list_add_tail(struct list_head *p,struct list_head *h) {p->next=h;p->prev=h->prev;h->prev->next=p;h->prev=p;}
static void list_del(struct list_head *p) {p->prev->next=p->next;p->next->prev=p->prev;}
#define list_for_each_entry_safe(p,n,h,m) for(p=(void*)(h)->next,n=(void*)p->m.next;&p->m!=(h);p=n,n=(void*)p->m.next)
"""
        body = (
            header
            + enqueue
            + reset
            + r"""
int main(void) {struct amvdec_session s={0};s.bufs_recycle.next=s.bufs_recycle.prev=&s.bufs_recycle;
amvdec_recycle(&s,3);amvdec_recycle(&s,7);assert(s.recycle_pending==2 && wakeups==2);
vdec_reset_bufs_recycle(&s);assert(!s.recycle_pending && s.bufs_recycle.next==&s.bufs_recycle);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            (p / "test.c").write_text(body)
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
