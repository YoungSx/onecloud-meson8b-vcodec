"""Keep custom drain completion separate from packet-count EOS guesses."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class DecoderEosTests(unittest.TestCase):
    def test_custom_drain_owns_last(self):
        source = (ROOT / "vdec/vdec_helpers.c").read_text()
        start = source.index("static void amvdec_mark_picture(")
        end = source.index("/* Take the oldest queued timestamp;", start)
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#define STATUS_NEEDS_RESUME 2
#define V4L2_BUF_FLAG_LAST 16
#define atomic_read(p) (*(p))
#define dev_dbg(...) ((void)0)
struct device { int unused; };
struct amvdec_codec_ops { void (*drain)(void); };
struct format { struct amvdec_codec_ops *codec_ops; };
struct core { struct device *dev_dec; };
struct geometry { int width,height,reference_width,reference_height,crop_left,crop_top; };
struct amvdec_picture { struct geometry geo; bool eos,restart; unsigned int flags; };
struct amvdec_session {
    struct core *core; struct format *fmt_out;
    int width,height,reference_width,reference_height,crop_left,crop_top;
    int should_stop,status,esparser_queued_bufs;
};
static void custom_drain(void) {}
"""
        harness += source[start:end]
        harness += r"""
int main(void) {
    struct amvdec_codec_ops ops = { .drain = custom_drain };
    struct format fmt = { .codec_ops = &ops };
    struct core core = {0};
    struct amvdec_session s = { .core=&core, .fmt_out=&fmt, .should_stop=1,
        .esparser_queued_bufs=1, .width=320, .height=240 };
    struct amvdec_picture p = {0};
    amvdec_mark_picture(&s,&p);
    assert(!p.eos && !(p.flags & V4L2_BUF_FLAG_LAST));
    assert(p.geo.width==320 && p.geo.height==240);
    ops.drain=0;
    amvdec_mark_picture(&s,&p);
    assert(p.eos && (p.flags & V4L2_BUF_FLAG_LAST));
    p=(struct amvdec_picture){0}; s.esparser_queued_bufs=2;
    amvdec_mark_picture(&s,&p);
    assert(!p.eos && !(p.flags & V4L2_BUF_FLAG_LAST));
    s.status=STATUS_NEEDS_RESUME;
    amvdec_mark_picture(&s,&p);
    assert(!p.eos && p.restart && (p.flags & V4L2_BUF_FLAG_LAST));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(harness)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
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
