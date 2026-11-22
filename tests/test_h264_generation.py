"""A reset buffer table must not receive stale H.264 recycle slot numbers."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class H264GenerationTests(unittest.TestCase):
    def test_resume_clears_old_recycle_mailboxes(self):
        s = (ROOT / "vdec/codec_h264.c").read_text()
        a = s.index("static void codec_h264_resume(")
        b = s.index("\n/*\n * Configure the H.264", a)
        code = (
            r"""
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;typedef uintptr_t dma_addr_t;
#define VDEC_REVISION_MESON8B 0
#define GFP_KERNEL 0
#define MB_MV_SIZE 96
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define ALIGN(x,n) (((x)+(n)-1)&~((n)-1))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
enum {AV_SCRATCH_7,AV_SCRATCH_8,AV_SCRATCH_9,AV_SCRATCH_0,AV_SCRATCH_1,AV_SCRATCH_4,ANC0_CANVAS_ADDR};
struct list_head {struct list_head *next,*prev;};
struct amvdec_buffer {struct list_head list;u32 fw_idx;};
struct codec_h264 {u32 mb_width,mb_height,max_refs,ref_size;void *ref_vaddr;dma_addr_t ref_paddr;};
struct platform {int revision;};struct amvdec_core {struct platform *platform;void *dev;};
struct amvdec_session {struct amvdec_core *core;void *priv;bool recycle_paused;u32 recycle_pending,num_dst_bufs;struct list_head bufs_recycle;int bufs_recycle_lock,recycle_wq;};
static int locked,clears,wakes,configured,alloc_fail,aborts;
#define mutex_lock(p) ((void)(p),assert(!locked),locked=1)
#define mutex_unlock(p) ((void)(p),assert(locked),locked=0)
#define list_for_each_entry_safe(p,n,h,m) for(p=container_of((h)->next,__typeof__(*p),m),n=container_of(p->m.next,__typeof__(*p),m);&p->m!=(h);p=n,n=container_of(p->m.next,__typeof__(*p),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
#define kfree(p) free(p)
static void amvdec_write_dos(struct amvdec_core *c,int reg,u32 v){(void)c;if(reg==AV_SCRATCH_7||reg==AV_SCRATCH_8||reg==AV_SCRATCH_9){assert(locked && !v);clears++;}if(reg==AV_SCRATCH_0){assert(configured && wakes==1);}}
static int amvdec_set_canvases(struct amvdec_session *s,u32 a[],u32 b[]){(void)a;(void)b;assert(s->recycle_paused && !s->recycle_pending && clears==3);configured=1;return 0;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static void dma_free_coherent(void *dev,u32 size,void *p,dma_addr_t addr){(void)dev;(void)size;(void)addr;free(p);}
static void *dma_alloc_coherent(void *dev,u32 size,dma_addr_t *addr,int flags){(void)dev;(void)flags;*addr=1;return alloc_fail?0:malloc(size);}
static void wake_up_interruptible(int *p){(void)p;assert(configured);wakes++;}
"""
            + s[a:b]
            + r"""
int main(void){struct platform plat={0};struct amvdec_core c={&plat,0};struct codec_h264 h={.mb_width=20,.mb_height=15,.max_refs=1};struct amvdec_session s={.core=&c,.priv=&h,.recycle_paused=true,.recycle_pending=2,.num_dst_bufs=3};struct amvdec_buffer *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));s.bufs_recycle.next=&a->list;s.bufs_recycle.prev=&b->list;a->list.prev=&s.bufs_recycle;a->list.next=&b->list;b->list.prev=&a->list;b->list.next=&s.bufs_recycle;codec_h264_resume(&s);assert(!s.recycle_paused && !s.recycle_pending && !aborts && s.bufs_recycle.next==&s.bufs_recycle);free(h.ref_vaddr);return 0;}
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
