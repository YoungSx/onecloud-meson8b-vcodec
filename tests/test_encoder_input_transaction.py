"""The actual H.264/JPEG entry points must not submit an unsynchronized picture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EncoderInputTransactionTests(unittest.TestCase):
    def test_staging_sync_failures_never_start_either_codec(self):
        source = (ROOT / 'venc/venc.c').read_text()
        canvas = source[source.index('static u32 venc_canvas_bytes('):
                        source.index('/* Bytes the staging buffer needs')]
        layout = source[source.index('static void venc_fill_out_fmt('):
                        source.index('/* Memory-plane layout')]
        stage = source[source.index('static void venc_copy_rows('):
                       source.index('static void venc_frame_done(')]
        encode = source[source.index('static int venc_encode_frame('):
                        source.index('/* ---------------------------------------------------------------------\n * V4L2 / vb2 plumbing')]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
typedef uintptr_t dma_addr_t;typedef int64_t ktime_t;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define U32_MAX UINT32_MAX
#define min(a,b) ((a)<(b)?(a):(b))
#define VENC_INPUT 0
#define VENC_STREAM 1
#define VENC_BITSTREAM_SIZE 1024
#define VENC_JPEG_HEADER_SIZE 32
#define ENCODER_IDLE 0
#define ENCODER_IDR_DONE 1
#define ENCODER_NON_IDR_DONE 2
#define ENCODER_IDR 3
#define ENCODER_NON_IDR 4
#define JPEGENC_DONE 5
#define VLC_TOTAL_BYTES 6
#define VLC_VB_WR_PTR 7
#define HCODEC_ASSIST_MBOX2_CLR_REG 8
#define HCODEC_ASSIST_MBOX2_MASK 9
#define HENC_SCRATCH_0 10
#define dev_warn_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
struct venc_format {u32 fourcc;unsigned planes,halves[3],vsub[3],align;};
struct venc_core {u64 copied_frames,direct_frames,copy_ns;u32 hw_status;int done;bool done_pending;};
struct venc_ctx {struct venc_core *core;const struct venc_format *src;u32 src_fourcc,width,height,coded_width;
 struct {dma_addr_t paddr;u8 *vaddr;} buffers[2];dma_addr_t input[3];u32 quant,jpeg_quality;
 bool sps_pps_valid;u32 sps_pps_size;u8 *sps_pps;struct {u32 bitstream_start;} mem;};
struct vb2_buffer {unsigned num_planes;u8 *data[3];u32 size[3],payload[3];bool imported;};
static unsigned waits,begins,ends,kicks,done,barriers,active;
static int wait_error,begin_fail=-1,end_fail=-1;static bool direct;
static dma_addr_t imported_address=0x200000;
static inline int venc_input_wait(struct vb2_buffer *b){(void)b;assert(!active);waits++;return wait_error;}
static void *venc_input_begin_cpu(struct vb2_buffer *b,unsigned i){
 assert(!active&&i<b->num_planes);begins++;if((int)i==begin_fail)return NULL;active=1u<<i;return b->data[i];
}
static int venc_input_end_cpu(struct vb2_buffer *b,unsigned i){
 (void)b;assert(active==(1u<<i));active=0;ends++;return (int)i==end_fail?-EIO:0;
}
static dma_addr_t venc_input_dma(struct vb2_buffer *b,unsigned i){
 assert(!active&&i<b->num_planes);return direct?imported_address+i*0x10000:0;
}
static u32 vb2_get_plane_payload(struct vb2_buffer *b,unsigned i){return b->payload[i];}
static u32 vb2_plane_size(struct vb2_buffer *b,unsigned i){return b->size[i];}
static void *vb2_plane_vaddr(struct vb2_buffer *b,unsigned i){return b->data[i];}
static void vb2_set_plane_payload(struct vb2_buffer *b,unsigned i,u32 n){b->payload[i]=n;}
static ktime_t ktime_get(void){return 10;}
static ktime_t ktime_sub(ktime_t a,ktime_t b){return a-b;}
static u64 ktime_to_ns(ktime_t t){return t;}
static void dma_wmb(void){assert(!active);barriers++;}
static void dma_rmb(void){}
static int venc_start_cmd(struct venc_ctx *c,u32 cmd){(void)c;assert(!active&&(cmd==ENCODER_IDR||cmd==ENCODER_NON_IDR));kicks++;return 0;}
static int venc_wait_cmd(struct venc_core *c,u32 cmd){(void)c;(void)cmd;return 0;}
static void venc_light_reset(struct venc_ctx *c){(void)c;assert(0);}
static void venc_hw_stop(struct venc_ctx *c){(void)c;assert(0);}
static u32 venc_read(struct venc_core *c,u32 reg){(void)c;return reg==VLC_TOTAL_BYTES?128:0x500000+128;}
static void *venc_stream_vaddr(struct venc_ctx *c){return c->buffers[VENC_STREAM].vaddr;}
static void venc_frame_done(struct venc_ctx *c,bool idr){(void)c;(void)idr;done++;}
static void venc_amrisc_stop(struct venc_core *c){(void)c;assert(!active);}
static void venc_set_input_format(struct venc_ctx *c){(void)c;assert(!active);}
static void venc_jpeg_init(struct venc_ctx *c){(void)c;assert(!active);}
static void venc_write(struct venc_core *c,u32 reg,u32 v){(void)c;(void)reg;(void)v;assert(!active);}
static void reinit_completion(int *c){(void)c;}
static void venc_amrisc_start(struct venc_core *c){(void)c;assert(!active);kicks++;}
''' + canvas + layout + stage + encode + r'''
static void reset(void){waits=begins=ends=kicks=done=barriers=active=0;wait_error=0;begin_fail=end_fail=-1;direct=false;}
int main(void){
 u8 y[2048],u[1024],v[1024],staging[2048],bitstream[1024],output[2048];
 memset(y,0x31,sizeof(y));memset(u,0x72,sizeof(u));memset(v,0x93,sizeof(v));memset(bitstream,0x55,sizeof(bitstream));bitstream[126]=0xff;bitstream[127]=0xd9;
 struct venc_format formats[]={{V4L2_PIX_FMT_NV12M,2,{2,2},{1,2},32},{V4L2_PIX_FMT_YUV420M,3,{2,1,1},{1,2,2},64}};
 for(unsigned layout=0;layout<3;layout++)for(unsigned jpeg=0;jpeg<2;jpeg++){
  const struct venc_format *fmt=&formats[layout==2];unsigned planes=layout?fmt->planes:1;
  struct venc_core core={0};struct venc_ctx c={.core=&core,.src=fmt,.src_fourcc=layout?fmt->fourcc:V4L2_PIX_FMT_NV12,.width=32,.height=16,.coded_width=32,
   .buffers={{0x100000,staging},{0x500000,bitstream}},.mem={0x500000}};
  struct vb2_buffer src={.num_planes=planes,.imported=true,.data={y,u,v},.size={sizeof(y),sizeof(u),sizeof(v)},.payload={768,512,512}},dst={.num_planes=1,.data={output},.size={sizeof(output)}};
  for(int fault=0;fault<3;fault++)for(unsigned plane=0;plane<planes;plane++){
   reset();core=(struct venc_core){0};
   if(fault==0)end_fail=plane;else if(fault==1)begin_fail=plane;else wait_error=-ETIMEDOUT;
   int ret=jpeg?venc_encode_jpeg(&c,&src,&dst):venc_encode_frame(&c,&src,&dst,false);
   assert(ret==(fault==0?-EIO:fault==1?-EFAULT:-ETIMEDOUT));
   assert(!kicks&&!done&&!core.copied_frames&&!core.direct_frames&&!active);
   assert(fault!=2||(!begins&&!ends&&!barriers));
   if(fault==0)assert(begins==plane+1&&ends==plane+1);
   if(fault==1)assert(begins==plane+1&&ends==plane);
  }
  reset();core=(struct venc_core){0};assert(!(jpeg?venc_encode_jpeg(&c,&src,&dst):venc_encode_frame(&c,&src,&dst,true)));
  assert(waits==1&&begins==planes&&ends==planes&&kicks==1&&core.copied_frames==1&&!active);
  if(layout<2){
   reset();core=(struct venc_core){0};direct=true;
   assert(!(jpeg?venc_encode_jpeg(&c,&src,&dst):venc_encode_frame(&c,&src,&dst,false)));
   assert(waits==1&&!begins&&!ends&&kicks==1&&core.direct_frames==1&&!core.copied_frames);
   assert(c.input[0]==0x200000&&c.input[1]==0x200000+(layout?0x10000:32*16));
  }
 }
 /* Exercise the single-plane fast path at real canvas sizes and DMA limits. */
 u8 *input=malloc(4*1024*1024),*stage_buffer=malloc(4*1024*1024);assert(input&&stage_buffer);
 memset(input,0x65,4*1024*1024);
 struct {u32 width,height,coded;dma_addr_t address;bool mapped,expected;} cases[]={
  {1280,720,1280,0x200000,true,true},{854,480,864,0x200000,true,true},
  {1920,1088,1920,0x200000,true,true},{1920,1080,1920,0x200000,true,false},
  {66,18,80,0x200000,true,false},{32,16,64,0x200000,true,false},
  {1280,720,1280,0x200001,true,false},{1280,720,1280,0,true,false},
  {1280,720,1280,0x200000,false,false},
  {32,16,32,(dma_addr_t)U32_MAX+1-768,true,true},
  {32,16,32,(dma_addr_t)U32_MAX+1-760,true,false},
  {32,16,32,(dma_addr_t)U32_MAX+1,true,false}
 };
 for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);k++){
  struct venc_core core={0};
  struct venc_ctx c={.core=&core,.src=&formats[0],.src_fourcc=V4L2_PIX_FMT_NV12,
   .width=cases[k].width,.height=cases[k].height,.coded_width=cases[k].coded,.buffers={{0x100000,stage_buffer}}};
  u32 stride=ALIGN(c.width,32),offset=stride*c.height,bytes=offset*3/2;
  struct vb2_buffer b={.num_planes=1,.data={input},.size={bytes},.payload={bytes}};
  reset();direct=cases[k].mapped;imported_address=cases[k].address;memset(stage_buffer,0xa5,4*1024*1024);
  assert(!venc_stage_input(&c,&b)&&waits==1&&!active);
  assert(core.direct_frames==cases[k].expected&&core.copied_frames==!cases[k].expected);
  if(cases[k].expected){
   assert(!begins&&!ends&&!barriers&&c.input[0]==cases[k].address&&c.input[1]==cases[k].address+offset);
   for(unsigned i=0;i<4*1024*1024;i++)assert(stage_buffer[i]==0xa5);
  }else{
   u32 width=venc_canvas_bytes(c.src,c.coded_width,0),rows=ALIGN(c.height,16);
   assert(begins==1&&ends==1&&barriers==1&&c.input[0]==0x100000&&c.input[1]==0x100000+width*rows);
   assert(stage_buffer[0]==0x65&&stage_buffer[width*rows]==0x65);
  }
  /* A short payload must fail even when its allocation/address could fit. */
  reset();direct=true;core=(struct venc_core){0};b.payload[0]=bytes-1;
  assert(venc_stage_input(&c,&b)==-EINVAL&&!begins&&!ends&&!core.direct_frames&&!core.copied_frames);
  b.payload[0]=bytes;wait_error=-EINTR;
  assert(venc_stage_input(&c,&b)==-EINTR&&!begins&&!ends&&!core.direct_frames&&!core.copied_frames);
 }
 free(input);free(stage_buffer);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
