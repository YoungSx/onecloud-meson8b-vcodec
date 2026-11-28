"""Firmware completion codes must never alias an invalid slot to a valid picture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DecoderSlotStatusTests(unittest.TestCase):
    def test_actual_mailbox_handlers_reject_out_of_range_slots(self):
        for codec, maximum, mask, mailbox, offset in (
            ("mpeg12", 8, 15, "MREG_BUFFEROUT", "MREG_FRAME_OFFSET"),
            ("mpeg4", 4, 7, "MREG_BUFFEROUT", "MP4_OFFSET_REG"),
            ("vc1", 4, 7, "VC1_BUFFEROUT", "VC1_OFFSET_REG"),
            ("mjpeg", 4, 7, "MREG_FROM_AMRISC", "MREG_FRAME_OFFSET"),
        ):
            with self.subTest(codec=codec):
                source = (ROOT / f"vdec/codec_{codec}.c").read_text()
                macros = "\n".join(line for line in source.splitlines()
                                   if line.lstrip().startswith("#define"))
                start = source.index(f"static irqreturn_t codec_{codec}_threaded_isr(")
                end = source.index(f"static irqreturn_t codec_{codec}_isr(", start)
                code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32;typedef int irqreturn_t;
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((~0u<<(l))&(~0u>>(31-(h))))
#define IRQ_HANDLED 1
#define V4L2_PIX_FMT_H263 1
#define V4L2_FIELD_NONE 0
#define V4L2_FIELD_INTERLACED_TB 1
#define V4L2_FIELD_INTERLACED_BT 2
#define dev_err(...) ((void)0)
static inline void dev_dbg(const void *dev, const char *fmt, ...) {(void)dev;(void)fmt;}
#include "dos_regs.h"
struct amvdec_core {void *dev;};
struct format {u32 pixfmt;};
struct amvdec_session {struct amvdec_core *core;struct format *fmt_out;};
static u32 registers[65536];static int delivered,aborted;static u32 slot,frame_offset;
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;assert(r/4<65536);return registers[r/4];}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 value){(void)c;assert(r/4<65536);registers[r/4]=value;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborted++;}
static void amvdec_dst_buf_done_idx(struct amvdec_session *s,u32 i,u32 offset,u32 field){(void)s;(void)field;slot=i;frame_offset=offset;delivered++;}
''' + macros + "\n"
                if codec in ("mpeg4", "vc1"):
                    code += "static void amvdec_set_par_from_ratio(struct amvdec_session *s,u32 ratio,u32 count){(void)s;(void)ratio;(void)count;}\n"
                if codec == "mpeg12":
                    code += "\nstatic void codec_mpeg12_update_dar(struct amvdec_session *s){(void)s;}\n"
                code += source[start:end] + r'''
int main(void){struct amvdec_core core={0};struct format fmt={2};struct amvdec_session s={.core=&core,.fmt_out=&fmt};
 registers[OFFSET/4]=0x123456;
 assert(codec_CODEC_threaded_isr(&s)==IRQ_HANDLED&&!delivered&&!aborted);
 for(u32 i=1;i<=MAXIMUM;i++){
  registers[MAILBOX/4]=i|0x100;
  int before=delivered;assert(codec_CODEC_threaded_isr(&s)==IRQ_HANDLED);
  assert(delivered==before+1&&slot==i-1&&frame_offset==0x123456&&!registers[MAILBOX/4]);
 }
 for(u32 i=MAXIMUM+1;i<=MASK;i++){
  registers[MAILBOX/4]=i|0x100;int before=delivered,errors=aborted;
  assert(codec_CODEC_threaded_isr(&s)==IRQ_HANDLED);
  assert(delivered==before&&aborted==errors+1&&!registers[MAILBOX/4]);
 }
 ZERO_SLOT_CHECK
 return 0;}
'''.replace("CODEC", codec).replace("MAXIMUM", str(maximum)).replace("MASK", str(mask)).replace(
                    "MAILBOX", mailbox).replace("OFFSET", offset).replace(
                    "ZERO_SLOT_CHECK",
                    f"registers[{mailbox}/4]=0x100;int before=delivered,errors=aborted;"
                    f"codec_{codec}_threaded_isr(&s);assert(delivered==before&&aborted==errors+1"
                    f"&&!registers[{mailbox}/4]);" if codec != "mjpeg" else "")
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory)
                    (path / "test.c").write_text(code)
                    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                                    "-I", str(ROOT / "vdec"), str(path / "test.c"),
                                    "-o", str(path / "test")], check=True)
                    subprocess.run([str(path / "test")], check=True)
