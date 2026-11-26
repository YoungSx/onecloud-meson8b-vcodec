"""Scaled CAPTURE geometry remains separate from reference pictures and SAR."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureScalingTests(unittest.TestCase):
    def test_even_quarter_limits_source_change_and_pixel_aspect(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
typedef uint32_t u32;typedef uint64_t u64;
#include "capture-geometry.h"
#include "../ge2d/ge2d-scale.h"
int main(void){
 struct meson_capture_geometry c={0};
 struct meson_capture_size size=meson_capture_resolve(&c,1920,1080);
 assert(size.width==1920&&size.height==1080);
 meson_capture_configure(&c,1920,1080,1280,720,true);
 size=meson_capture_resolve(&c,1920,1080);assert(size.width==1280&&size.height==720);
 size=meson_capture_resolve(&c,854,480);assert(size.width==854&&size.height==480);
 struct meson_capture_ratio ratio=meson_capture_pixelaspect(&c,1920,1080,1,1);
 assert(ratio.numerator==1&&ratio.denominator==1);
 meson_capture_configure(&c,1920,1080,960,1080,true);
 ratio=meson_capture_pixelaspect(&c,1920,1080,1,1);assert(ratio.numerator==1&&ratio.denominator==2);
 meson_capture_configure(&c,720,576,360,576,true);
 ratio=meson_capture_pixelaspect(&c,720,576,16,15);assert(ratio.numerator==15&&ratio.denominator==32);
 meson_capture_configure(&c,1920,1080,1280,720,false);
 assert(c.width==1920&&c.height==1080);
 meson_capture_configure(&c,1919,1079,640,360,true);
 assert(c.width==1919&&c.height==1079);
 /* All accepted sizes satisfy the actual scaler's phase constraints. */
 for(u32 w=2;w<=1920;w+=2)for(u32 h=2;h<=1088;h+=34){
  u32 requests[]={0,1,2,3,w/4,w/2,w-1,w,w+1,UINT32_MAX};
  for(unsigned i=0;i<sizeof(requests)/sizeof(requests[0]);i++){
   meson_capture_configure(&c,w,h,requests[i],requests[i],true);
   assert(!(c.width&1)&&!(c.height&1)&&c.width<=w&&c.height<=h);
   assert(c.width*4>=w&&c.height*4>=h&&c.width&&c.height);
   u32 step;bool pre;
   assert(!meson8b_ge2d_phase(w,c.width,&step,&pre));
   assert(!meson8b_ge2d_phase(h,c.height,&step,&pre));
   ratio=meson_capture_pixelaspect(&c,w,h,65521,65519);
   long double expected=(long double)65519*h*c.width/(65521.L*w*c.height);
   assert(fabsl((long double)ratio.numerator/ratio.denominator-expected)<1e-7L);
  }
 }
 meson_capture_configure(&c,1918,1086,480,1086,true);
 ratio=meson_capture_pixelaspect(&c,1918,1086,UINT32_MAX,UINT32_MAX-2);
 assert(ratio.numerator&&ratio.denominator);
 assert(fabsl((long double)ratio.numerator/ratio.denominator-
  ((long double)(UINT32_MAX-2)*1086*480/((long double)UINT32_MAX*1918*1086)))<1e-8L);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text('#include <errno.h>\n' + code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test'), '-lm'], check=True)
            subprocess.run([str(root / 'test')], check=True)
