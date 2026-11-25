"""Reproduce upstream's early EOF when a consumer temporarily owns CAPTURE."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FFmpegDrainTests(unittest.TestCase):
    def test_user_owned_capture_is_not_terminal_eos(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / 'tests/fixtures/ffmpeg-v4l2', root / 'libavcodec')
            self.assertEqual(hashlib.sha256((root / 'libavcodec/v4l2_context.c').read_bytes()).hexdigest(),
                             '079c39759fa0702e53162425ae8c368a937db9ee0e76b6c911494fbcb43eb7de')
            for fixed in (False, True):
                if fixed:
                    subprocess.run(['patch', '-p1', '--forward', '--batch', '--fuzz=0', '-i',
                                    str(ROOT / 'patches/ffmpeg-7.0.2-v4l2-drain.patch')],
                                   cwd=root, check=True, stdout=subprocess.DEVNULL)
                source = (root / 'libavcodec/v4l2_context.c').read_text()
                body = source[source.index('static V4L2Buffer* v4l2_dequeue_v4l2buf('):
                              source.index('static V4L2Buffer* v4l2_getfree_v4l2buf(')]
                code = r'''
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <linux/videodev2.h>
#define AV_LOG_WARNING 1
#define AV_LOG_DEBUG 2
#define AVERROR(e) (-(e))
#define V4L2BUF_AVAILABLE 0
#define V4L2BUF_IN_DRIVER 1
#define av_log(...) ((void)0)
#define logger(c) (c)
#define av_err2str(e) "error"
typedef struct V4L2Buffer {int status;struct v4l2_buffer buf;struct v4l2_plane planes[VIDEO_MAX_PLANES];} V4L2Buffer;
typedef struct V4L2Context {unsigned int type;V4L2Buffer *buffers;int num_buffers,streamon,done;char *name;} V4L2Context;
struct m2m {int fd,draining;};static struct m2m m2m={41,1};
static V4L2Context *context;static int polls,dequeues,mode;
static struct m2m *ctx_to_m2mctx(V4L2Context *c){(void)c;return &m2m;}
static int v4l2_handle_event(V4L2Context *c){(void)c;assert(0);return 0;}
static int fake_poll(struct pollfd *p,nfds_t n,int timeout){
 assert(n==1&&p->fd==41&&timeout==0);polls++;
 if(mode==0){p->revents=0;return 0;}
 /* The downstream consumer returns a buffer, then hardware completes it. */
 context->buffers[0].status=V4L2BUF_IN_DRIVER;
 p->revents=mode==3?POLLERR:POLLIN;return 1;
}
static int fake_ioctl(int fd,unsigned long req,struct v4l2_buffer *b){
 assert(fd==41&&req==VIDIOC_DQBUF);dequeues++;
 if(mode==3){errno=EIO;return -1;}
 b->index=0;b->m.planes[0].bytesused=mode==2?0:128;
 b->flags=mode==2?V4L2_BUF_FLAG_LAST:0;return 0;
}
#define poll fake_poll
#define ioctl fake_ioctl
''' + body + r'''
int main(void){
 V4L2Buffer buffers[2]={{0}};V4L2Context ctx={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
 .buffers=buffers,.num_buffers=2,.streamon=1,.name="capture"};context=&ctx;
 V4L2Buffer *frame=v4l2_dequeue_v4l2buf(&ctx,0);
 assert(!frame);
 if(!FIXED){assert(ctx.done&&!polls&&!dequeues);return 0;}
 assert(!ctx.done&&polls==1&&!dequeues);
 mode=1;frame=v4l2_dequeue_v4l2buf(&ctx,0);
 assert(frame==&buffers[0]&&!ctx.done&&dequeues==1&&frame->planes[0].bytesused==128);
 mode=2;assert(!v4l2_dequeue_v4l2buf(&ctx,0)&&ctx.done&&dequeues==2);
 ctx.done=0;mode=3;assert(!v4l2_dequeue_v4l2buf(&ctx,0)&&ctx.done&&dequeues==3);
 return 0;
}
'''.replace('FIXED', str(int(fixed)))
                (root / 'test.c').write_text(code)
                subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                                '-Wno-missing-braces', '-fsanitize=address,undefined',
                                '-fno-pie', '-no-pie', str(root / 'test.c'),
                                '-o', str(root / 'test')], check=True)
                subprocess.run([str(root / 'test')], check=True)
