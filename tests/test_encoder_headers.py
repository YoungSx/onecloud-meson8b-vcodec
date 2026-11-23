"""Firmware byte counters cannot make SPS/PPS copies exceed the DMA buffer."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class EncoderHeaderTests(unittest.TestCase):
    def test_header_counter_bounds_and_start_cleanup(self):
        source = (ROOT / 'venc/venc.c').read_text()
        start = source.index('static int venc_hw_start(')
        end = source.index('\n/* Free the session resources', start)
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define V4L2_PIX_FMT_JPEG 1
#define VENC_JPEG_FW_NAME "jpeg"
#define VENC_FW_NAME "avc"
#define VENC_MC_SIZE 16384
#define VENC_BITSTREAM_SIZE 1048576
#define ENCODER_SEQUENCE 1
#define ENCODER_PICTURE 2
#define ENCODER_SEQUENCE_DONE 3
#define ENCODER_PICTURE_DONE 4
#define VLC_TOTAL_BYTES 5
#define GFP_KERNEL 0
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define kfree(p) free(p)
struct firmware {size_t size;};
struct venc_core {void *dev,*canvas;bool running;u32 hw_status;};
struct venc_ctx {struct venc_core *core;struct {int *lock;} ctrl_handler;
 unsigned dst_fmt,coded_width,slice_cfg,canvas[9],idr_pic_id,frame_number,poc_lsb,sps_pps_size;
 const struct firmware *fw;void *sps_pps;bool sps_pps_valid;};
static const struct firmware firmware={VENC_MC_SIZE};
static unsigned counts[2],reads,commands,stops,poweroffs,releases,freed,canvas_freed;
static unsigned char stream[VENC_BITSTREAM_SIZE];
static unsigned venc_coded_width(struct venc_ctx *c){(void)c;return 1920;}
static unsigned venc_slice_cfg(struct venc_ctx *c){(void)c;return 0;}
static void mutex_lock(int *p){(void)p;}
static void mutex_unlock(int *p){(void)p;}
static int venc_alloc_buffers(struct venc_ctx *c){(void)c;return 0;}
static int request_firmware(const struct firmware **f,const char *n,void *d){(void)n;(void)d;*f=&firmware;return 0;}
static int meson_canvas_alloc(void *c,unsigned *id){(void)c;*id=1;return 0;}
static void meson_canvas_free(void *c,unsigned id){(void)c;(void)id;canvas_freed++;}
static int venc_hw_init(struct venc_ctx *c){(void)c;return 0;}
static int venc_start_cmd(struct venc_ctx *c,unsigned cmd){(void)c;(void)cmd;commands++;return 0;}
static int venc_wait_cmd(struct venc_core *c,unsigned cmd){(void)c;(void)cmd;return 0;}
static u32 venc_read(struct venc_core *c,unsigned reg){(void)c;assert(reg==VLC_TOTAL_BYTES && reads<2);return counts[reads++];}
static void *kmalloc(size_t n,int flags){(void)flags;assert(n<=VENC_BITSTREAM_SIZE);return malloc(n);}
static void *venc_stream_vaddr(struct venc_ctx *c){(void)c;return stream;}
static int venc_update_sps(struct venc_ctx *c){assert(c->sps_pps_size==16);return 0;}
static void venc_amrisc_stop(struct venc_core *c){(void)c;stops++;}
static void venc_poweroff(struct venc_core *c){(void)c;poweroffs++;}
static void release_firmware(const struct firmware *f){assert(f==&firmware);releases++;}
static void venc_free_buffers(struct venc_ctx *c){(void)c;freed++;}
'''
        code += source[start:end]
        code += r'''
int main(void){unsigned cases[][2]={{0,16},{VENC_BITSTREAM_SIZE+1,16},{8,0},{8,8},{8,7},{8,VENC_BITSTREAM_SIZE+1},{8,16}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
  struct venc_core core={0};struct venc_ctx ctx={.core=&core};
  counts[0]=cases[i][0];counts[1]=cases[i][1];reads=commands=stops=poweroffs=releases=freed=canvas_freed=0;
  int ret=venc_hw_start(&ctx);
  if(i<6){assert(ret==-EIO && !core.running && !ctx.sps_pps && !ctx.fw);assert(stops==1 && poweroffs==1 && releases==1 && freed==1 && canvas_freed==9);if(i<2)assert(commands==1 && reads==1);}
  else {assert(!ret && core.running && ctx.sps_pps_valid && ctx.sps_pps_size==16 && commands==2);free(ctx.sps_pps);}
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'check.c').write_text(code)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(path / 'check.c'), '-o', str(path / 'check')], check=True)
            subprocess.run([str(path / 'check')], check=True)
