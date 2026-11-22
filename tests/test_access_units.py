"""Exercise the diagnostic client's real codec boundary classifier."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class AccessUnitTests(unittest.TestCase):
    def test_picture_headers_and_continuations(self):
        program = r"""
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
static void check(unsigned int format, unsigned char a, unsigned char b,
                  unsigned char c, int boundary, int picture) {
    unsigned char p[] = {a,b,c}; int actual_picture;
    assert(!!access_unit_start(p,format,&actual_picture)==boundary);
    assert(actual_picture==picture);
}
int main(void) {
    check(V4L2_PIX_FMT_MPEG2,0,0,0,1,1);
    check(V4L2_PIX_FMT_MPEG2,1,0,0,0,0);
    check(V4L2_PIX_FMT_MPEG1,0xb3,0,0,1,0);
    check(V4L2_PIX_FMT_MPEG2,0xb8,0,0,1,0);
    check(V4L2_PIX_FMT_MPEG4,0xb6,0,0,1,1);
    check(V4L2_PIX_FMT_MPEG4,0x20,0,0,1,0);
    check(V4L2_PIX_FMT_MPEG4,0xb1,0,0,0,0);
    check(V4L2_PIX_FMT_VC1_ANNEX_G,0x0d,0,0,1,1);
    check(V4L2_PIX_FMT_VC1_ANNEX_G,0x0c,0,0,0,0);
    check(V4L2_PIX_FMT_VC1_ANNEX_G,0x0e,0,0,1,0);
    check(V4L2_PIX_FMT_H264,0x65,0x80,0,1,1);
    check(V4L2_PIX_FMT_H264,0x41,0x40,0,0,1);
    check(V4L2_PIX_FMT_HEVC,0x02,1,0x80,1,1);
    check(V4L2_PIX_FMT_HEVC,0x02,1,0,0,1);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)
            (path / "test.c").write_text(program)
            subprocess.run(
                [
                    "cc",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    "-I" + str(ROOT / "tests"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
