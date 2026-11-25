"""The actual MJPEG unity scaler must preserve samples instead of blurring them."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MjpegScalerTests(unittest.TestCase):
    def test_unity_phase_and_filter_coefficients(self):
        source = (ROOT / 'vdec/codec_mjpeg.c').read_text()
        body = source[source.index('static void codec_mjpeg_init_hw('):
                      source.index('static void codec_mjpeg_resume(')]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;
#define BIT(n) (1u<<(n))
#include "dos_regs.h"
#define MREG_DECODE_PARAM AV_SCRATCH_2
#define MREG_TO_AMRISC AV_SCRATCH_8
#define MREG_FROM_AMRISC AV_SCRATCH_9
struct amvdec_core {int unused;};
struct amvdec_session {struct amvdec_core *core;u32 height;};
static u32 memory[128],address,control;
static void amvdec_write_dos(struct amvdec_core *c,u32 reg,u32 value){
 (void)c;
 if(reg==PSCALE_BMEM_ADDR)address=value;
 if(reg==PSCALE_BMEM_DAT){assert(address<128);memory[address++]=value;}
 if(reg==PSCALE_CTRL)control=value;
}
''' + body + r'''
int main(void){
 struct amvdec_core core={0};struct amvdec_session s={&core,240};
 memset(memory,0xa5,sizeof(memory));codec_mjpeg_init_hw(&s);
 assert(control==0xc000);
 for(u32 phase=0;phase<=32;phase++){
  assert(!memory[phase*2]);
  u32 coeff=memory[phase*2+1];
  assert((coeff>>24)==0&&(coeff&255)==0);
  assert(((coeff>>16)&255)+((coeff>>8)&255)==128);
  /* Phase zero is an impulse: a sharp edge or isolated pixel stays sharp. */
  if(!phase)assert(coeff==0x00800000);
  /* The half-pixel phase must average the two adjacent samples. */
  if(phase==32)assert(coeff==0x00404000);
 }
 u32 steps[]={36,40,38,42};u32 initial[]={37,41,39,43};
 for(u32 i=0;i<4;i++){
  assert(memory[steps[i]*2+1]==0x10000);
  assert(memory[initial[i]*2]==8&&memory[initial[i]*2+1]==0x60000000);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
