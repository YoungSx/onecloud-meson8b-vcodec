"""Ready pictures queue conversion instead of copying in the firmware ISR."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class PoolDeliveryTests(unittest.TestCase):
    def test_picture_queues_work_after_unlock(self):
        source = (ROOT / "vdec/meson8b-capture.c").read_text()
        start = source.index("void amvdec_pool_push(")
        end = source.index("\nstatic void amvdec_pool_work(", start)
        code = (
            r"""
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
struct amvdec_picture {u32 timestamp;};
struct amvdec_frame_pool {int lock,work;struct {u32 len;} state;struct amvdec_picture picture[4];};
struct core {void *dev;u32 pool_waiting_peak;};
struct amvdec_session {struct amvdec_frame_pool pool;struct core *core;};
static int locked,aborted,scheduled;
#define max(a,b) ((a)>(b)?(a):(b))
#define dev_err(...) ((void)0)
static void mutex_lock(int *p){(void)p;assert(!locked);locked=1;}
static void mutex_unlock(int *p){(void)p;assert(locked);locked=0;}
static int meson8b_pool_push(void *p,u32 slot){if(slot>=4)return -1;((u32*)p)[0]++;return 0;}
static void amvdec_abort(struct amvdec_session *s){(void)s;assert(!locked);aborted++;}
static void schedule_work(int *p){(void)p;assert(!locked);scheduled++;}
"""
            + source[start:end]
            + r"""
int main(void){struct core c={0};struct amvdec_session s={.core=&c};struct amvdec_picture p={42};amvdec_pool_push(&s,1,&p);assert(s.pool.picture[1].timestamp==42 && s.pool.state.len==1 && c.pool_waiting_peak==1 && scheduled==1 && !aborted);amvdec_pool_push(&s,7,&p);assert(scheduled==1 && aborted==1 && !locked);return 0;}
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
