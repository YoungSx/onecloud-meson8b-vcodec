"""A failed parser terminator must not schedule a successful EOS flush."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class DecoderDrainTests(unittest.TestCase):
    def test_terminator_failure_aborts_without_flush(self):
        for codec in ("mpeg12", "mpeg4", "vc1", "mjpeg"):
            with self.subTest(codec=codec):
                source = (ROOT / ("vdec/codec_" + codec + ".c")).read_text()
                start = source.index("static void codec_" + codec + "_drain(struct")
                end = source.index("\nstatic int codec_" + codec + "_stop(", start)
                body = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
typedef uint8_t u8;
struct amvdec_session {void *priv,*core;};
static const u8 eos_sequence[1024];
static int writes,scheduled,aborted,result;
static int esparser_queue_eos(void *core,const u8 *data,unsigned int len) {
    (void)core;(void)data;(void)len;writes++;return result;
}
static void amvdec_abort(struct amvdec_session *s) {(void)s;aborted++;}
static void schedule_delayed_work(int *work,int delay) {(void)work;(void)delay;scheduled++;}
"""
                body += "struct codec_" + codec + " {bool draining;int drain_work;};\n"
                body += source[start:end]
                body += r"""
int main(void) {
    struct CODEC c={0};struct amvdec_session s={.priv=&c};
    int errors[]={-ENOMEM,-ETIMEDOUT,-EINTR};
    for(unsigned int i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        result=errors[i];DRAIN(&s);
        assert(aborted==(int)i+1 && scheduled==0 && !c.draining);
    }
    result=0;DRAIN(&s);
    assert(c.draining && scheduled==1 && writes==4);
    DRAIN(&s);assert(scheduled==1 && writes==4);
    return 0;
}
""".replace("CODEC", "codec_" + codec).replace("DRAIN", "codec_" + codec + "_drain")
                with tempfile.TemporaryDirectory() as directory:
                    path = pathlib.Path(directory)
                    (path / "test.c").write_text(body)
                    subprocess.run(
                        [
                            "cc",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-fsanitize=address,undefined",
                            "-fno-pie",
                            "-no-pie",
                            str(path / "test.c"),
                            "-o",
                            str(path / "test"),
                        ],
                        check=True,
                    )
                    subprocess.run([str(path / "test")], check=True)

    def test_drain_timeout_does_not_publish_success(self):
        for codec in ("mpeg12", "mpeg4", "vc1", "mjpeg"):
            with self.subTest(codec=codec):
                source = (ROOT / ("vdec/codec_" + codec + ".c")).read_text()
                start = source.index("static void codec_" + codec + "_drain_work(struct work_struct *work)\n{")
                end = source.index("\nstatic void codec_" + codec + "_drain(struct", start)
                body = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;typedef uint64_t u64;
struct work_struct {int unused;};
struct amvdec_session {u64 last_irq_jiffies;bool irq_active;int esparser_queued_bufs;};
struct CODEC {struct amvdec_session *sess;bool draining;};
static struct CODEC state;
static u64 now,finish_irq;static int continuous,aborts,flushes;
#define container_of(p,t,m) (&state)
#define READ_ONCE(x) (x)
#define time_is_after_jiffies64(x) ((x)>now)
#define msecs_to_jiffies(x) (x)
static int atomic_read(int *p){return *p;}
static void msleep(int n){now+=n;if(continuous)state.sess->last_irq_jiffies=now;if(finish_irq && now>=finish_irq){state.sess->irq_active=false;state.sess->last_irq_jiffies=now;finish_irq=0;}}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static void amvdec_flush_capture_buffers(struct amvdec_session *s){(void)s;flushes++;}
""".replace("CODEC", "codec_" + codec)
                body += source[start:end]
                body += r"""
int main(void){struct amvdec_session s={0};struct work_struct w={0};state.sess=&s;state.draining=true;now=200;DRAIN(&w);assert(flushes==1 && !aborts && !state.draining);state.draining=true;s.irq_active=true;finish_irq=400;DRAIN(&w);assert(now==500 && flushes==2 && !aborts);state.draining=true;s.last_irq_jiffies=now;continuous=1;DRAIN(&w);assert(aborts==1 && flushes==2 && !state.draining);continuous=0;s.esparser_queued_bufs=2;state.draining=true;DRAIN(&w);assert(aborts==2 && flushes==2 && !state.draining);return 0;}
""".replace("DRAIN", "codec_" + codec + "_drain_work")
                with tempfile.TemporaryDirectory() as directory:
                    path = pathlib.Path(directory)
                    (path / "test.c").write_text(body)
                    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", str(path / "test.c"), "-o", str(path / "test")], check=True)
                    subprocess.run([str(path / "test")], check=True)

    def test_decoder_stop_returns_parser_error(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        start = source.index("static int\nvdec_decoder_cmd(")
        end = source.index("\nstatic int vdec_subscribe_event(", start)
        body = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
typedef uint8_t u8;typedef uint32_t u32;
#define V4L2_DEC_CMD_STOP 1
#define V4L2_DEC_CMD_START 2
#define WRITE_ONCE(x,v) ((x)=(v))
#define container_of(p,t,m) (&session)
#define dev_dbg(...) ((void)0)
struct device {int unused;};
struct file {void *private_data;};
struct v4l2_decoder_cmd {unsigned int cmd;};
struct amvdec_session;
struct amvdec_codec_ops {
    bool firmware_eos;
    void (*drain)(struct amvdec_session *);
    const u8 *(*eos_sequence)(u32 *);
};
struct format {struct amvdec_codec_ops *codec_ops;};
struct core {struct device *dev;};
struct amvdec_session {
    struct format *fmt_out;struct core *core;void *m2m_ctx;
    int streamon_out,streamon_cap,should_stop;bool stop_requested;
};
static struct amvdec_session session;
static int aborted,waited,result,queued,blocked,custom_calls;
static int v4l2_m2m_ioctl_try_decoder_cmd(struct file *f,void *h,struct v4l2_decoder_cmd *c) {(void)f;(void)h;(void)c;return 0;}
static void v4l2_m2m_clear_state(void *c) {(void)c;}
static void v4l2_m2m_mark_stopped(void *c) {(void)c;}
static int v4l2_m2m_num_src_bufs_ready(void *c) {(void)c;return queued;}
static void esparser_queue_all(struct amvdec_session *s) {assert(!s->should_stop);assert(!s->stop_requested);if(!blocked)queued=0;}
static void msleep(int ms) {(void)ms;}
static int vdec_wait_inactive(struct amvdec_session *s) {(void)s;waited++;return 0;}
static void amvdec_abort(struct amvdec_session *s) {(void)s;aborted++;}
static void custom_drain(struct amvdec_session *s) {assert(s->stop_requested && !queued);custom_calls++;}
static const u8 *sequence(u32 *len) {static const u8 eos[4];*len=4;return eos;}
static int esparser_queue_eos(struct core *c,const u8 *data,u32 len) {(void)c;(void)data;assert(len==4);return result;}
"""
        body += source[start:end]
        body += r"""
int main(void) {
    struct core core={0};struct amvdec_codec_ops ops={.eos_sequence=sequence};
    struct format fmt={.codec_ops=&ops};struct file f={0};
    struct v4l2_decoder_cmd cmd={.cmd=V4L2_DEC_CMD_STOP};
    session=(struct amvdec_session){.core=&core,.fmt_out=&fmt,.streamon_out=1,.streamon_cap=1};
    result=-ETIMEDOUT;
    assert(vdec_decoder_cmd(&f,0,&cmd)==-ETIMEDOUT);
    assert(aborted==1 && waited==0);
    result=0;
    assert(vdec_decoder_cmd(&f,0,&cmd)==0);
    assert(aborted==1 && waited==1);
    cmd.cmd=V4L2_DEC_CMD_START;assert(!vdec_decoder_cmd(&f,0,&cmd));
    ops.eos_sequence=0;ops.drain=custom_drain;queued=2;
    cmd.cmd=V4L2_DEC_CMD_STOP;assert(!vdec_decoder_cmd(&f,0,&cmd));
    assert(!queued && custom_calls==1);
    cmd.cmd=V4L2_DEC_CMD_START;assert(!vdec_decoder_cmd(&f,0,&cmd));
    queued=2;blocked=1;cmd.cmd=V4L2_DEC_CMD_STOP;
    assert(vdec_decoder_cmd(&f,0,&cmd)==-ETIMEDOUT);
    assert(custom_calls==1 && aborted==2);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(body)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-Wno-unused-variable",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)

    def test_abort_stops_input_and_errors_both_queues(self):
        source = (ROOT / "vdec/vdec_helpers.c").read_text()
        start = source.index("void amvdec_abort(")
        end = source.index("EXPORT_SYMBOL_GPL(amvdec_abort)", start)
        body = (
            r"""
#include <assert.h>
#define WRITE_ONCE(p,v) ((p)=(v))
#define dev_info(...) ((void)0)
struct queue {int error;};
struct ctx {struct {struct queue q;} cap_q_ctx,out_q_ctx;};
struct core {int dev;};
struct amvdec_session {struct ctx *m2m_ctx;struct core *core;int should_stop;};
static void vb2_queue_error(struct queue *q){q->error=1;}
"""
            + source[start:end]
            + r"""
int main(void){struct ctx ctx={0};struct core core={0};struct amvdec_session s={.m2m_ctx=&ctx,.core=&core};amvdec_abort(&s);assert(s.should_stop && ctx.cap_q_ctx.q.error && ctx.out_q_ctx.q.error);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(body)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
