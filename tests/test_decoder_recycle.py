"""Allocation failure must not silently lose a firmware recycle request."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class RecycleFailureTests(unittest.TestCase):
    def test_allocation_failure_aborts(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        start = source.index("void amvdec_recycle(")
        end = source.index("\n/* Without a frame pool", start)
        body = (
            r"""
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint32_t u32;
#define GFP_KERNEL 0
struct list_head {int n;};
struct amvdec_buffer {u32 fw_idx;struct list_head list;};
struct amvdec_session {struct list_head bufs_recycle;int bufs_recycle_lock,recycle_wq;u32 recycle_pending;};
static int fail,aborts,wakes;static struct amvdec_buffer *allocated;
static void *kmalloc(size_t n,int flags){(void)flags;return fail?0:(allocated=malloc(n));}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static void mutex_lock(int *p){(void)p;}
static void mutex_unlock(int *p){(void)p;}
static void list_add_tail(struct list_head *p,struct list_head *h){(void)p;h->n++;}
static void wake_up_interruptible(int *p){(void)p;wakes++;}
"""
            + source[start:end]
            + r"""
int main(void){struct amvdec_session s={0};fail=1;amvdec_recycle(&s,3);assert(aborts==1 && !s.recycle_pending && !s.bufs_recycle.n && !wakes);fail=0;amvdec_recycle(&s,7);assert(aborts==1 && s.recycle_pending==1 && s.bufs_recycle.n==1 && wakes==1 && allocated->fw_idx==7);free(allocated);return 0;}
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
