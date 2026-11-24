"""Queued HEVC pictures must retain their own SPS window during read-ahead."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcParameterAssociationTests(unittest.TestCase):
    def test_equal_coded_sizes_do_not_alias_queued_windows(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        rpm = source[source.index("union rpm_param {"):source.index("enum nal_unit_type {")]
        enums = source[source.index("enum nal_unit_type {"):source.index("struct hevc_frame {")]
        header = source[source.index("static void codec_hevc_update_pocs("):
                        source.index("static int codec_hevc_process_segment(")]
        process = source[source.index("static int codec_hevc_process_rpm("):
                         source.index("static void codec_hevc_fetch_rpm(")]
        inspect = source[source.index("static int codec_hevc_inspect_input("):
                         source.index("static void codec_hevc_capture_queued(")]
        lookup = source[source.index("static bool codec_hevc_geometry_for_offset("):
                        source.index("static u32 codec_hevc_num_pending_bufs(")]
        clear = source[source.index("static void codec_hevc_clear_inputs("):
                       source.index("static int codec_hevc_stop(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
#define RPM_SIZE 128
#define MAX_REF_ACTIVE 16
#define HEVC_SOURCE_CHANGE 1
#define HEVC_REFERENCE_CHANGE 2
#define SAO_VB_SIZE 0x30000
#define SAO_ABV_SIZE 0x30000
#define HEVC_WATCHDOG_MS 5000
#define system_wq 0
#define msecs_to_jiffies(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define dev_dbg(...) ((void)0)
#include "hevc-sps.h"
#include "hevc-tiles.h"
#include "hevc_regs.h"
#define GFP_KERNEL 0
static int allocation_error;
static void *kmalloc(unsigned long n,int f){(void)f;return allocation_error?NULL:malloc(n);}
#define kfree free
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_safe(p,n,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m),n=entry(p->m.next,__typeof__(*p),m); \
     &p->m!=(h);p=n,n=entry(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
struct hevc_input_picture {struct list_head list;u32 offset;struct hevc_sps_geometry geometry;};
''' + rpm + enums + r'''
struct codec_hevc {
 int lock,watchdog;union rpm_param rpm_param;
 u32 width,height,dst_width,dst_height,dpb_size,lcu_x_num,lcu_y_num,lcu_total,lcu_size;
 u32 curr_poc,prev_tid0_poc,slice_segment_addr,slice_addr,random_access_poc;
 bool have_picture,skip_rasl;int is_10bit;
 struct hevc_sps_geometry sps,applied;struct hevc_tile_grid tiles;
 struct hevc_parameter_sets parameters;struct list_head input_pictures;
};
struct core {void *dev;};struct format {u32 max_width,max_height;};
struct amvdec_session {void *priv;struct core *core;struct format *fmt_out;u32 reference_width,reference_height,crop_left,crop_top;};
struct vb2_buffer {u8 *data;u32 size;};
static void *vb2_plane_vaddr(struct vb2_buffer *b,int p){(void)p;return b->data;}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){(void)p;return b->size;}
static void mutex_lock(int *p){assert(!*p);*p=1;}static void mutex_unlock(int *p){assert(*p);*p=0;}
static void mod_delayed_work(int q,int *w,int t){(void)q;(void)w;assert(t==5000);}
static u32 decode_offset;
static u32 amvdec_read_dos(struct core *c,u32 r){(void)c;assert(r==HEVC_SHIFT_BYTE_COUNT);return decode_offset;}
''' + header + clear + lookup + process + inspect + r'''
static void put(u8 *p,u32 *bit,u32 value,u32 n){while(n--){p[*bit/8]|=((value>>n)&1)<<(7-*bit%8);(*bit)++;}}
static void ue(u8 *p,u32 *bit,u32 v){u32 n=0;for(u32 x=v+1;x>1;x>>=1)n++;put(p,bit,0,n);put(p,bit,v+1,n+1);}
static u32 sps(u8 *packet,u32 left,u32 id,u32 pps_id,u32 dpb){
 u8 rbsp[64]={0};u32 bit=0,n=6,zeros=0;
 put(rbsp,&bit,0,4);put(rbsp,&bit,0,3);put(rbsp,&bit,1,1);
 put(rbsp,&bit,1,8);put(rbsp,&bit,0x40000000,32);put(rbsp,&bit,0x90000000,32);put(rbsp,&bit,0,16);put(rbsp,&bit,120,8);
 ue(rbsp,&bit,id);ue(rbsp,&bit,1);ue(rbsp,&bit,320);ue(rbsp,&bit,192);
 put(rbsp,&bit,!!left,1);if(left){ue(rbsp,&bit,left/2);ue(rbsp,&bit,0);ue(rbsp,&bit,0);ue(rbsp,&bit,0);}
 ue(rbsp,&bit,0);ue(rbsp,&bit,0);ue(rbsp,&bit,4);put(rbsp,&bit,1,1);
 ue(rbsp,&bit,dpb-1);ue(rbsp,&bit,0);ue(rbsp,&bit,0);put(rbsp,&bit,1,1);
 memset(packet,0,128);packet[3]=1;packet[4]=0x42;packet[5]=1;
 for(u32 i=0;i<(bit+7)/8;i++){if(zeros>=2&&rbsp[i]<=3){packet[n++]=3;zeros=0;}packet[n++]=rbsp[i];zeros=rbsp[i]?0:zeros+1;}
 u8 tail[16]={0};u32 b=0;
 packet[n++]=0;packet[n++]=0;packet[n++]=0;packet[n++]=1;packet[n++]=0x44;packet[n++]=1;
 ue(tail,&b,pps_id);ue(tail,&b,id);put(tail,&b,1,1);memcpy(packet+n,tail,(b+7)/8);n+=(b+7)/8;
 memset(tail,0,sizeof(tail));b=0;
 packet[n++]=0;packet[n++]=0;packet[n++]=0;packet[n++]=1;packet[n++]=0x26;packet[n++]=1;
 put(tail,&b,1,1);put(tail,&b,0,1);ue(tail,&b,pps_id);put(tail,&b,1,1);memcpy(packet+n,tail,(b+7)/8);n+=(b+7)/8;
 return n;
}
int main(void){struct codec_hevc h={0};struct core c={0};struct format f={1920,1088};struct amvdec_session s={.priv=&h,.core=&c,.fmt_out=&f};
 u8 a[128],b[128];struct vb2_buffer first={a,sps(a,0,0,0,5)},next={b,sps(b,4,0,0,16)};
 h.input_pictures.next=h.input_pictures.prev=&h.input_pictures;
 assert(!codec_hevc_inspect_input(&s,&first,0));assert(!codec_hevc_inspect_input(&s,&next,4096));
 h.rpm_param.p.pic_width_in_luma_samples=320;h.rpm_param.p.pic_height_in_luma_samples=192;
 h.rpm_param.p.log2_diff_max_min_coding_block_size=3;h.rpm_param.p.m_temporalId=1;
 h.rpm_param.p.first_slice_segment_in_pic_flag=1;h.rpm_param.p.m_nalUnitType=19;h.rpm_param.p.slice_type=I_SLICE;
 decode_offset=1;assert(codec_hevc_process_rpm(&s)>0&&h.dst_width==320&&s.crop_left==0);
 assert(h.dpb_size==6&&h.applied.dpb==5);
 /* Continued/dependent slices keep the selected snapshot and leave the next AU queued. */
 h.rpm_param.p.slice_segment_address=1;h.rpm_param.p.first_slice_segment_in_pic_flag=0;h.rpm_param.p.dependent_slice_segment_flag=1;
 assert(!codec_hevc_process_rpm(&s)&&h.applied.width==320&&h.input_pictures.next!=&h.input_pictures);
 h.rpm_param.p.slice_segment_address=0;h.rpm_param.p.first_slice_segment_in_pic_flag=1;h.rpm_param.p.dependent_slice_segment_flag=0;
 decode_offset=4097;assert(codec_hevc_process_rpm(&s)>0&&h.dst_width==316&&s.crop_left==4);
 assert(h.dpb_size==16&&h.applied.dpb==16);
 assert(h.input_pictures.next==&h.input_pictures&&h.input_pictures.prev==&h.input_pictures);
 assert(codec_hevc_process_rpm(&s)==-EINVAL&&h.dst_width==316&&h.width==320);
 /* Parameter IDs select their own SPS, rather than whichever arrived last. */
 struct hevc_parameter_sets sets={0};struct hevc_sps_geometry g={0},old;
 first.size=sps(a,0,0,0,5);assert(hevc_input_geometry(&sets,a,first.size,&g)==1&&g.id==0&&g.width==320);old=g;
 next.size=sps(b,4,1,63,16);assert(hevc_input_geometry(&sets,b,next.size,&g)==1&&g.id==1&&g.width==316);
 const u8 slice0[]={0,0,0,1,0x26,1,0xb0};
 assert(hevc_input_geometry(&sets,slice0,sizeof(slice0),&g)==1&&g.id==0&&g.width==old.width);
 /* Unknown/invalid PPS and truncated slice prefixes cannot enqueue geometry. */
 struct hevc_parameter_sets empty={0};assert(hevc_input_geometry(&empty,slice0,sizeof(slice0),&g)==-EINVAL);
 next.size=sps(b,0,0,64,16);assert(hevc_input_geometry(&sets,b,next.size,&g)==-EINVAL);
 for(u32 n=0;n<sizeof(slice0);n++)assert(hevc_input_geometry(&sets,slice0,n,&g)!=1);
 /* Cancel/close clears snapshots whose firmware header never arrived. */
 first.size=sps(a,0,0,0,5);assert(!codec_hevc_inspect_input(&s,&first,8192));
 assert(!codec_hevc_inspect_input(&s,&first,12288));codec_hevc_clear_inputs(&h);
 assert(h.input_pictures.next==&h.input_pictures&&h.input_pictures.prev==&h.input_pictures);
 allocation_error=1;assert(codec_hevc_inspect_input(&s,&first,16384)==-ENOMEM);
 assert(h.input_pictures.next==&h.input_pictures);
 /* The VIFIFO byte counter wraps, while queued packets remain close together. */
 allocation_error=0;assert(!codec_hevc_inspect_input(&s,&first,0xfffffff0));
 assert(!codec_hevc_inspect_input(&s,&first,0x20));
 assert(codec_hevc_geometry_for_offset(&h,0xfffffff8,&g));
 assert(codec_hevc_geometry_for_offset(&h,0x28,&g));
 assert(!codec_hevc_geometry_for_offset(&h,0x28,&g));
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-Wno-sign-compare",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
