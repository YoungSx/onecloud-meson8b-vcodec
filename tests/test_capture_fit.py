import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class CaptureFitTests(unittest.TestCase):
    def test_planar_chroma_capacity(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        start = source.index("static bool vdec_capture_queue_fits(")
        end = source.index("static int vdec_start_streaming(", start)
        body = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#define V4L2_PIX_FMT_YUV420M 1
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
struct vb2_buffer {unsigned int num_planes;struct {u32 length;} planes[3];};
struct vb2_queue {unsigned int num_buffers;struct vb2_buffer **bufs;};
struct format {u32 min_buffers;};
struct amvdec_session {unsigned int pixfmt_cap;struct format *fmt_out;int ctrl_min_buf_capture;};
static u32 amvdec_get_output_size(struct amvdec_session *s){(void)s;return 4096;}
static int v4l2_ctrl_g_ctrl(int c){return c;}
static bool amvdec_has_pool(struct amvdec_session *s){(void)s;return true;}
"""
            + source[start:end]
            + r"""
int main(void){struct format f={1};struct amvdec_session s={.pixfmt_cap=1,.fmt_out=&f,.ctrl_min_buf_capture=1};struct vb2_buffer b={.num_planes=3,.planes={{4096},{1024},{1024}}};struct vb2_buffer *p=&b;struct vb2_queue q={.num_buffers=1,.bufs=&p};assert(vdec_capture_queue_fits(&s,&q));b.planes[1].length=1023;assert(!vdec_capture_queue_fits(&s,&q));b.planes[1].length=1024;b.planes[2].length=1023;assert(!vdec_capture_queue_fits(&s,&q));return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
            (p / "t.c").write_text(body)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(p / "t.c"),
                    "-o",
                    str(p / "t"),
                ],
                check=True,
            )
            subprocess.run([str(p / "t")], check=True)
