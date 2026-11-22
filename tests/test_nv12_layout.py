"""Exercise negotiated single-memory-plane NV12 geometry."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).parents[1]


class NV12LayoutTests(unittest.TestCase):
    def test_contiguous_and_component_memory_layouts(self):
        driver = (ROOT / "venc/venc.c").read_text()
        a = driver.index("static void venc_fill_out_fmt(")
        b = driver.index("/*\n * OUTPUT colorimetry", a)
        source = (
            """#include <assert.h>\n#include <stdint.h>\n#include <string.h>\n#include <linux/videodev2.h>\ntypedef uint32_t u32;\n#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))\nstruct venc_format{u32 fourcc;unsigned planes;unsigned halves[3],vsub[3];};\n"""
            + driver[a:b]
            + """int main(void){struct venc_format f={V4L2_PIX_FMT_NV12M,2,{2,2},{1,2}};for(unsigned w=16;w<=1920;w+=2){for(unsigned h=16;h<=1088;h+=2){struct v4l2_pix_format_mplane p={.width=w,.height=h};venc_fill_source_fmt(&p,&f,V4L2_PIX_FMT_NV12);assert(p.num_planes==1&&p.pixelformat==V4L2_PIX_FMT_NV12);assert(p.plane_fmt[0].sizeimage==ALIGN(w,32)*h*3/2);venc_fill_source_fmt(&p,&f,V4L2_PIX_FMT_NV12M);assert(p.num_planes==2&&p.plane_fmt[1].sizeimage>=ALIGN(w,32)*ALIGN(h,32)/2);}}return 0;}"""
        )
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / "test.c").write_text(source)
            subprocess.run(
                [
                    "cc",
                    "-O1",
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

    def test_plane_copy_fast_and_padded_rows(self):
        driver = (ROOT / "venc/venc.c").read_text()
        a = driver.index("static void venc_copy_rows(")
        b = driver.index("/*\n * Point ctx->input", a)
        source = (
            "#include <assert.h>\n#include <stdint.h>\n#include <string.h>\ntypedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;\n#define min(a,b) ((a)<(b)?(a):(b))\n"
            + driver[a:b]
            + "int main(void){u8 s[128],d[128];for(unsigned i=0;i<128;i++)s[i]=i;memset(d,0,sizeof(d));venc_copy_rows(d,16,s,16,16,4,64);assert(!memcmp(s,d,64));memset(d,0,sizeof(d));venc_copy_rows(d,16,s,8,8,4,32);for(unsigned r=0;r<4;r++){assert(!memcmp(d+16*r,s+8*r,8));for(unsigned x=8;x<16;x++)assert(!d[16*r+x]);}return 0;}"
        )
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / "test.c").write_text(source)
            subprocess.run(
                [
                    "cc",
                    "-O1",
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
