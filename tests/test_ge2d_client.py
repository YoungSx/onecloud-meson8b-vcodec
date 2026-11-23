"""Check the actual client reference and both-plane failure detection on a host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dClientTests(unittest.TestCase):
    def test_copy_crop_and_scaled_chroma_references(self):
        code = r'''
#define main device_main
#include "ge2d-copy.c"
#undef main
#include <assert.h>
int main(void){
 unsigned char src_data[768],dst_data[192];
 struct v4l2_pix_format_mplane input={.width=32,.height=16,.num_planes=1,
 .pixelformat=V4L2_PIX_FMT_NV12,.plane_fmt={{.bytesperline=32,.sizeimage=768}}};
 struct v4l2_pix_format_mplane output={.width=16,.height=8,.num_planes=2,
 .pixelformat=V4L2_PIX_FMT_NV12M,.plane_fmt={{.bytesperline=16,.sizeimage=128},
 {.bytesperline=16,.sizeimage=64}}};
 struct mapped src={.data={src_data},.size={768},.nplanes=1,.memory=V4L2_MEMORY_MMAP};
 struct mapped dst={.data={dst_data,dst_data+128},.size={128,64},.nplanes=2};
 struct v4l2_rect crop={.left=2,.top=4,.width=16,.height=8};
 fill_frame(&src,&input,7,0);
 assert(plane_data(&src,&input,1)==src_data+512);
 for(u32 plane=0;plane<2;plane++){
  u32 rows=plane?4:8,top=plane?2:4;
  for(u32 y=0;y<rows;y++)
   memcpy((u8 *)dst.data[plane]+y*16,plane_data(&src,&input,plane)+(y+top)*32+2,16);
 }
 assert(!check_frame(&dst,&output,&crop,7,0));
 dst_data[150]^=1;assert(check_frame(&dst,&output,&crop,7,0));dst_data[150]^=1;
 for(u32 plane=0;plane<2;plane++){
  u32 rows=plane?4:8;
  for(u32 y=0;y<rows;y++)for(u32 x=0;x<16;x++)
   ((unsigned char *)dst.data[plane])[y*16+x]=pixel(x,y,plane,9,1);
 }
 assert(!check_frame(&dst,&output,&crop,9,1));
 unsigned char tmp=dst_data[128];dst_data[128]=dst_data[129];dst_data[129]=tmp;
 assert(check_frame(&dst,&output,&crop,9,1));
 return 0;
}
'''.replace("(u8 *)", "(unsigned char *)")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "tests"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True, capture_output=True)
