"""Continuous firmware interrupts cannot block STOP forever."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class DecoderTimeoutTests(unittest.TestCase):
    def test_inactive_wait_is_bounded(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        start = source.index("static int vdec_wait_inactive(")
        end = source.index("static void vdec_poweroff(", start)
        code = (
            r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
typedef uint64_t u64;
struct amvdec_session {u64 last_irq_jiffies;};
static u64 now;static int continuous;static struct amvdec_session *active;
#define msecs_to_jiffies(n) (n)
#define time_is_after_jiffies64(n) ((n)>now)
static u64 get_jiffies_64(void) {return now;}
static void msleep(unsigned int n){now+=n;if(continuous)active->last_irq_jiffies=now;}
"""
            + source[start:end]
            + r"""
int main(void){struct amvdec_session s={.last_irq_jiffies=100};active=&s;now=100;assert(!vdec_wait_inactive(&s));assert(now==150);now=200;s.last_irq_jiffies=100;assert(!vdec_wait_inactive(&s) && now==200);s.last_irq_jiffies=now;continuous=1;assert(vdec_wait_inactive(&s)==-ETIMEDOUT && now==3200);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d)
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
