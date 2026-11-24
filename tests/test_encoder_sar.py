"""Run the encoder's SAR controls and SPS assembly with their real callers."""
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
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        "-I", str(ROOT / "venc"), str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)


class EncoderSarTests(unittest.TestCase):
    def test_cluster_defaults_transactions_and_streaming_guard(self):
        source = (ROOT / "venc/venc.c").read_text()
        callbacks = source[source.index("static int venc_try_ctrl("):
                           source.index("static int venc_open(")]
        start = source.index("\tctx->sar_controls[0] =")
        declarations = source[start:source.index("\t/* SPS/PPS lead every IDR", start)]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/v4l2-controls.h>
typedef uint32_t u32;typedef int32_t s32;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct v4l2_ctrl_handler {int error;};
struct v4l2_ctrl {u32 id;int val,min,max,step;struct v4l2_ctrl_handler *handler;};
struct v4l2_ctrl_ops {int (*try_ctrl)(struct v4l2_ctrl *);int (*s_ctrl)(struct v4l2_ctrl *);};
struct queue {bool streaming;};struct m2m {struct queue src,dst;};
struct venc_ctx {
 struct v4l2_ctrl_handler ctrl_handler;struct m2m *m2m_ctx;
 struct v4l2_ctrl *qp_range[2],*rate_controls[3],*sar_controls[4];
 struct {u32 bitrate,qp_min,qp_max;} rc_config;
 u32 jpeg_quality,gop_size,peak_bitrate,cpb_size,slice_max_mb;
 s32 i_qp,p_qp;bool rc_restart,rc_enable,force_key,multi_slice,sar_enable;
 s32 sar_idc,sar_width,sar_height;
};
static struct queue *v4l2_m2m_get_src_vq(struct m2m *p){return &p->src;}
static struct queue *v4l2_m2m_get_dst_vq(struct m2m *p){return &p->dst;}
static bool vb2_is_streaming(struct queue *p){return p->streaming;}
static struct v4l2_ctrl controls[4];static int count;
static struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *h,const struct v4l2_ctrl_ops *o,u32 id,int min,int max,int step,int def){
 assert(count<4&&o&&min<=def&&def<=max);struct v4l2_ctrl *c=&controls[count++];
 *c=(struct v4l2_ctrl){id,def,min,max,step,h};return c;
}
static struct v4l2_ctrl *v4l2_ctrl_new_std_menu(struct v4l2_ctrl_handler *h,const struct v4l2_ctrl_ops *o,u32 id,int max,unsigned int skip,int def){
 assert(!skip);return v4l2_ctrl_new_std(h,o,id,0,max,1,def);
}
''' + callbacks + r'''
static int submit(struct venc_ctx *ctx,int enabled,int idc,int width,int height){
 int values[]={enabled,idc,width,height},old[4];
 /* Linux normalizes booleans and clamps INTEGER controls; MENU rejects. */
 values[0]=!!values[0];
 if(values[1]<controls[1].min||values[1]>controls[1].max)return -ERANGE;
 for(int i=2;i<4;i++){if(values[i]<controls[i].min)values[i]=controls[i].min;if(values[i]>controls[i].max)values[i]=controls[i].max;}
 for(int i=0;i<4;i++){old[i]=controls[i].val;controls[i].val=values[i];}
 int ret=venc_ctrl_ops.try_ctrl(ctx->sar_controls[0]);
 if(!ret)ret=venc_ctrl_ops.s_ctrl(ctx->sar_controls[0]);
 if(ret)for(int i=0;i<4;i++)controls[i].val=old[i];
 return ret;
}
int main(void){struct venc_ctx state={0};struct venc_ctx *ctx=&state;
 struct v4l2_ctrl_handler *hdl=&ctx->ctrl_handler;
''' + declarations + r'''
 assert(count==4&&controls[0].id==V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE);
 assert(controls[1].max==V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED);
 assert(controls[2].id==V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH&&controls[2].min==1&&controls[2].max==65535);
 assert(controls[3].id==V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT&&controls[3].min==1&&controls[3].max==65535);
 assert(!venc_ctrl_ops.try_ctrl(controls)&&!venc_ctrl_ops.s_ctrl(controls));
 assert(!ctx->sar_enable&&!ctx->sar_idc&&ctx->sar_width==1&&ctx->sar_height==1);
 struct m2m queues={0};ctx->m2m_ctx=&queues;
 assert(!submit(ctx,1,2,1,1)&&ctx->sar_enable&&ctx->sar_idc==2);
 assert(!submit(ctx,1,17,17,13)&&ctx->sar_width==17&&ctx->sar_height==13);
 queues.src.streaming=true;
 assert(submit(ctx,1,2,1,1)==-EBUSY&&ctx->sar_idc==17&&ctx->sar_width==17);
 assert(!submit(ctx,1,17,17,13));
 queues.src.streaming=false;queues.dst.streaming=true;
 assert(submit(ctx,0,17,17,13)==-EBUSY&&ctx->sar_enable);
 assert(submit(ctx,1,17,17,14)==-EBUSY&&ctx->sar_height==13);
 queues.dst.streaming=false;
 assert(!submit(ctx,1,17,65535,65535));
 assert(submit(ctx,1,18,1,1)==-ERANGE&&ctx->sar_idc==17);
 assert(!submit(ctx,1,17,0,1)&&ctx->sar_width==1);
 assert(!submit(ctx,1,17,1,65536)&&ctx->sar_height==65535);
 assert(!submit(ctx,0,1,1,1)&&!ctx->sar_enable);
 return 0;}
'''
        run_c(code)

    def test_actual_sps_assembly_maps_menu_extended_to_255(self):
        source = (ROOT / "venc/venc.c").read_text()
        body = source[source.index("static int venc_update_sps("):
                      source.index("static int venc_hw_start(")]
        parser = (ROOT / "tests/test_h264_sps.c").read_text().split("int main(void)", 1)[0]
        code = parser + r'''
#include <stdlib.h>
#include <errno.h>
#include <linux/videodev2.h>
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kfree(p) free(p)
struct venc_ctx {
 struct {int *lock;} ctrl_handler;void *sps_pps;u32 sps_pps_size,width,height,coded_width;
 struct v4l2_fract timeperframe;bool sar_enable;u32 sar_idc,sar_width,sar_height;
};
static int locks;
static void mutex_lock(int *p){assert(p&&!locks);locks=1;}
static void mutex_unlock(int *p){assert(p&&locks);locks=0;}
static void venc_fill_cap_fmt(struct venc_ctx *ctx,struct v4l2_pix_format_mplane *p,u32 fmt){
 (void)ctx;assert(fmt==V4L2_PIX_FMT_H264);p->colorspace=V4L2_COLORSPACE_REC709;
}
''' + body + r'''
static void headers(struct venc_ctx *ctx){
 static const u8 pps[]={0,0,0,1,0x68,0xce,0x38,0x80};
 const u8 *sps=ctx->width==1920?sps1080:sps854;u32 size=ctx->width==1920?sizeof(sps1080):sizeof(sps854);
 ctx->sps_pps_size=4+size+sizeof(pps);ctx->sps_pps=malloc(ctx->sps_pps_size);assert(ctx->sps_pps);
 u8 *data=ctx->sps_pps;memcpy(data,"\0\0\0\1",4);memcpy(data+4,sps,size);memcpy(data+4+size,pps,sizeof(pps));
}
int main(void){int lock=0;struct venc_ctx ctx={.ctrl_handler={&lock},.width=854,.height=480,.coded_width=864,.timeperframe={1001,30000},.sar_width=1,.sar_height=1};
 for(u32 idc=0;idc<=17;idc++){
  ctx.sar_enable=true;ctx.sar_idc=idc;ctx.sar_width=17;ctx.sar_height=13;headers(&ctx);
  assert(!venc_update_sps(&ctx)&&!locks);
  u8 *data=ctx.sps_pps;struct fields f=parse(data+4,ctx.sps_pps_size-12);
  assert(f.crop[1]==5&&f.vui.sar_enable&&f.vui.sar_idc==(idc==17?255:idc));
  assert(f.vui.tick==1001&&f.vui.scale==60000&&f.vui.primaries==1);
  if(idc==17)assert(f.vui.sar_width==17&&f.vui.sar_height==13);
  assert(!memcmp(data+ctx.sps_pps_size-8,"\0\0\0\1\x68\xce\x38\x80",8));
  free(ctx.sps_pps);
 }
 ctx.width=ctx.coded_width=1920;ctx.height=1080;ctx.sar_enable=false;headers(&ctx);assert(!venc_update_sps(&ctx));
 struct fields f=parse((u8 *)ctx.sps_pps+4,ctx.sps_pps_size-12);assert(!f.vui.sar_enable&&f.crop[3]==4&&f.width_mbs==120);free(ctx.sps_pps);
 return 0;}
'''
        run_c(code)
