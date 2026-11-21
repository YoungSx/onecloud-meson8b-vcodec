"""Apply the FFmpeg patch and exercise its real input/packet callbacks."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


class FFmpegV4L2Tests(unittest.TestCase):
    def test_shared_frame_buffers_padding_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = pathlib.Path(directory)
            shutil.copytree(ROOT / 'tests/fixtures/ffmpeg-v4l2', tree / 'libavcodec')
            subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i',
                            str(ROOT / 'patches/ffmpeg-7.0.2-v4l2-rate-limits.patch')],
                           cwd=tree, check=True, stdout=subprocess.DEVNULL)
            source = (tree / 'libavcodec/v4l2_buffers.c').read_text()
            copy = source[source.index('static int v4l2_bufref_to_buf('):
                          source.index('\nstatic int v4l2_buffer_buf_to_swframe(')]
            upload = source[source.index('static int v4l2_buffer_swframe_to_buf('):
                            source.index('\n/******************************************************************************',
                                         source.index('static int v4l2_buffer_swframe_to_buf('))]
            prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#define AVERROR(e) (-(e))
#define FFMIN(a,b) ((a)<(b)?(a):(b))
#define FFMAX(a,b) ((a)>(b)?(a):(b))
#define FFABS(a) ((a)>=0?(a):-(a))
#define AV_CEIL_RSHIFT(a,b) (((a)+(1<<(b))-1)>>(b))
typedef struct {int plane;} Component;
typedef struct {int nb_components,log2_chroma_h;Component comp[3];} AVPixFmtDescriptor;
typedef struct {uint8_t *data;size_t size;} Buffer;
typedef struct {int format,width,height,linesize[8];uint8_t *data[8];Buffer *buf[8];} AVFrame;
typedef struct {struct v4l2_format format;} Context;
typedef struct {Context *context;int num_planes;struct v4l2_buffer buf;
 struct v4l2_plane planes[VIDEO_MAX_PLANES];
 struct {void *mm_addr;size_t length;int bytesperline;} plane_info[VIDEO_MAX_PLANES];} V4L2Buffer;
static const AVPixFmtDescriptor nv12={3,1,{{0},{1},{1}}};
static const AVPixFmtDescriptor *av_pix_fmt_desc_get(int format){return format==1?&nv12:NULL;}
static int av_image_get_linesize(int format,int width,int plane){return format==1&&plane<2?width:-1;}
static void av_image_copy_plane(uint8_t *d,int ds,const uint8_t *s,int ss,int width,int rows){
 for(int r=0;r<rows;r++){memcpy(d,s,width);d+=ds;s+=ss;}}
'''
            cases = r'''
int main(void){
 uint8_t shared[24],y[40],uv[24];for(int i=0;i<24;i++)shared[i]=(uint8_t)(i+1);
 Buffer owner={shared,sizeof(shared)};
 AVFrame f={.format=1,.width=4,.height=4,.linesize={4,4},.data={shared,shared+16},.buf={&owner}};
 Context c={.format={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE}};
 c.format.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12M;c.format.fmt.pix_mp.height=4;
 V4L2Buffer b={.context=&c,.num_planes=2,.buf={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE},
  .plane_info={{y,sizeof(y),8},{uv,sizeof(uv),8}}};
 memset(y,0xcc,sizeof(y));memset(uv,0xcc,sizeof(uv));
 assert(!v4l2_buffer_swframe_to_buf(&f,&b));
 for(int r=0;r<4;r++){assert(!memcmp(y+r*8,shared+r*4,4));for(int x=4;x<8;x++)assert(!y[r*8+x]);}
 for(int r=0;r<2;r++)assert(!memcmp(uv+r*8,shared+16+r*4,4));
 assert(!y[39]&&!uv[23]&&b.planes[0].bytesused==sizeof(y));
 f.data[0]=shared+12;f.linesize[0]=-4;
 assert(!v4l2_buffer_swframe_to_buf(&f,&b)&&!memcmp(y,shared+12,4));
 b.plane_info[1].length=7;assert(v4l2_buffer_swframe_to_buf(&f,&b)==-EINVAL);
 b.plane_info[1].length=sizeof(uv);f.data[1]=NULL;assert(v4l2_buffer_swframe_to_buf(&f,&b)==-EINVAL);
 assert(v4l2_bufref_to_buf(&b,0,shared,4,41)==-EINVAL);
 assert(v4l2_bufref_to_buf(&b,0,shared,-1,0)==-EINVAL);
 assert(v4l2_bufref_to_buf(&b,-1,shared,4,0)==-EINVAL);
 assert(!v4l2_bufref_to_buf(&b,0,NULL,0,0));return 0;}
'''
            test, binary = tree / 'test.c', tree / 'test'
            test.write_text(prefix + copy + upload + cases)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(test), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
