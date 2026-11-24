"""Verify SPS DPB requirements and picture-boundary frame-pool growth."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run_c(code):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(code)
        subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-fsanitize=address,undefined",
                        "-fno-pie", "-no-pie", "-I", str(ROOT / "vdec"),
                        str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)


class HevcDpbTests(unittest.TestCase):
    def test_inspected_sps_budget_does_not_shrink_on_read_ahead(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        body = source[source.index("static int codec_hevc_inspect_input("):
                      source.index("static void codec_hevc_capture_queued(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define max(a,b) ((a)>(b)?(a):(b))
#define system_wq 0
#define HEVC_WATCHDOG_MS 5000
#define msecs_to_jiffies(x) (x)
#include "hevc-sps.h"
struct codec_hevc {int lock,watchdog;struct hevc_sps_geometry sps;u32 sps_dpb_size;};
struct amvdec_session {void *priv;};
struct vb2_buffer {u8 *data;u32 size;};
static void *vb2_plane_vaddr(struct vb2_buffer *b,int p){(void)p;return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){(void)p;return b->size;}
static void mutex_lock(int *p){assert(!*p);*p=1;}static void mutex_unlock(int *p){assert(*p);*p=0;}
static void mod_delayed_work(int q,int *w,int t){(void)q;(void)w;assert(t==5000);}
''' + body + r'''
static void put(u8 *data,u32 *bit,u32 value,u32 count){
 while(count--){data[*bit/8]|=((value>>count)&1)<<(7-*bit%8);(*bit)++;}
}
static void ue(u8 *data,u32 *bit,u32 value){u32 n=0;for(u32 v=value+1;v>1;v>>=1)n++;put(data,bit,0,n);put(data,bit,value+1,n+1);}
static u32 sps(u8 *packet,u32 dpb){
 u8 rbsp[64]={0};u32 bit=0;
 put(rbsp,&bit,0,4);put(rbsp,&bit,0,3);put(rbsp,&bit,1,1);
 put(rbsp,&bit,1,8);put(rbsp,&bit,0x40000000,32);put(rbsp,&bit,0x90000000,32);put(rbsp,&bit,0,16);put(rbsp,&bit,120,8);
 ue(rbsp,&bit,0);ue(rbsp,&bit,1);ue(rbsp,&bit,704);ue(rbsp,&bit,576);put(rbsp,&bit,0,1);
 ue(rbsp,&bit,0);ue(rbsp,&bit,0);ue(rbsp,&bit,4);put(rbsp,&bit,1,1);
 ue(rbsp,&bit,dpb-1);ue(rbsp,&bit,0);ue(rbsp,&bit,0);put(rbsp,&bit,1,1);
 memset(packet,0,128);packet[3]=1;packet[4]=0x42;packet[5]=1;u32 n=6,zeros=0;
 for(u32 i=0;i<(bit+7)/8;i++){
  if(zeros>=2&&rbsp[i]<=3){packet[n++]=3;zeros=0;}
  packet[n++]=rbsp[i];zeros=rbsp[i]?0:zeros+1;
 }
 return n;
}
int main(void){struct codec_hevc h={0};struct amvdec_session s={&h};u8 packet[128];struct vb2_buffer b={packet,0};
 b.size=sps(packet,10);assert(!codec_hevc_inspect_input(&s,&b));assert(h.sps.dpb==10&&h.sps_dpb_size==10);
 b.size=sps(packet,5);assert(!codec_hevc_inspect_input(&s,&b));assert(h.sps.dpb==5&&h.sps_dpb_size==10);
 b.size=sps(packet,11);assert(codec_hevc_inspect_input(&s,&b)==-EINVAL&&h.sps_dpb_size==10);
 return 0;}
'''
        run_c(code)

    def test_irq_grows_same_geometry_without_discard_or_source_change(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        setup = source[source.index("static int codec_hevc_setup_pool("):
                       source.index("static void codec_hevc_resume(")]
        irq = source[source.index("static irqreturn_t codec_hevc_threaded_isr("):
                     source.index("static irqreturn_t codec_hevc_isr(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
typedef uint32_t u32;typedef uint64_t dma_addr_t;typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define HEVC_NAL_SEARCH_DONE 9
#define HEVC_SLICE_SEGMENT_DONE 8
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define clamp(v,lo,hi) ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
#include "dos_regs.h"
#include "hevc_regs.h"
enum amvdec_status {STATUS_STOPPED,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct hevc_frame {int value;};
struct codec_hevc {int lock;u32 dpb_size,dst_width,dst_height;bool client_waited,eos_seen,waiting_for_buffer;};
struct amvdec_core {void *dev_dec;};struct amvdec_format {u32 min_buffers,max_buffers;};
struct amvdec_shadow {dma_addr_t paddr;u32 luma;};
struct amvdec_session {struct amvdec_core *core;void *priv;struct amvdec_format *fmt_out;u32 dpb_size;int keyframe_found,streamon_cap;enum amvdec_status status;};
static u32 pool_count=6,budget=10,tables[64];static int prepared,forgotten,rpms,segments,changes,aborts,fail;
static struct amvdec_shadow slots[12];
static void mutex_lock(int *p){assert(!*p);*p=1;}static void mutex_unlock(int *p){assert(*p);*p=0;}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return r==HEVC_DEC_STATUS_REG?8:0;}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 value){(void)c;if(r==HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR)tables[(value>>8)&255]=value&0xffff0000;}
static u32 amvdec_pool_count(struct amvdec_session *s){(void)s;return pool_count;}
static int amvdec_pool_prepare(struct amvdec_session *s,u32 n){(void)s;assert(n>=6&&n<=12);prepared++;if(fail)return -ENOMEM;pool_count=n;return 0;}
static const struct amvdec_shadow *amvdec_pool_slot(struct amvdec_session *s,u32 i){(void)s;assert(i<pool_count);return &slots[i];}
static void codec_hevc_forget_frames(struct codec_hevc *h){(void)h;forgotten++;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static void codec_hevc_fetch_rpm(struct amvdec_session *s){(void)s;}
static int codec_hevc_process_rpm(struct amvdec_session *s){((struct codec_hevc *)s->priv)->dpb_size=budget;rpms++;return 0;}
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *s,bool hold){(void)s;(void)hold;assert(0);return 0;}
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;assert(0);}
static void amvdec_src_change(struct amvdec_session *s,u32 w,u32 h,u32 n){(void)s;(void)w;(void)h;(void)n;changes++;}
static void codec_hevc_flush_output(struct amvdec_session *s){(void)s;assert(0);}
static void codec_hevc_finish_eos(struct amvdec_session *s){(void)s;assert(0);}
static int codec_hevc_process_segment(struct amvdec_session *s){(void)s;segments++;return 0;}
''' + setup + irq + r'''
int main(void){struct codec_hevc h={0};struct amvdec_core c={0};struct amvdec_format f={6,12};struct amvdec_session s={.core=&c,.priv=&h,.fmt_out=&f,.streamon_cap=1,.status=STATUS_RUNNING};
 for(u32 i=0;i<12;i++)slots[i]=(struct amvdec_shadow){0x800000+i*0x20000,0x10000};
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED&&pool_count==10&&s.dpb_size==10);
 assert(prepared==1&&!forgotten&&!changes&&!aborts&&rpms==1&&segments==1);
 for(u32 i=0;i<10;i++){assert(tables[2*i]==slots[i].paddr);assert(tables[2*i+1]==slots[i].paddr+slots[i].luma);}
 codec_hevc_threaded_isr(&s);assert(prepared==1&&rpms==2&&segments==2);
 budget=8;codec_hevc_threaded_isr(&s);assert(pool_count==10&&prepared==1);
 pool_count=6;budget=10;fail=1;int previous_rpms=rpms,previous_segments=segments;
 codec_hevc_threaded_isr(&s);assert(aborts==1&&rpms==previous_rpms+1&&segments==previous_segments&&!changes&&!forgotten);
 return 0;}
'''
        run_c(code)
