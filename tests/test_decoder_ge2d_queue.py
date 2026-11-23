"""Run real queue initialization for default and explicitly enabled DMA capture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DecoderGe2dQueueTests(unittest.TestCase):
    def test_default_vmalloc_and_enabled_dma_capture(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        body = source[source.index("static int m2m_queue_init("):
                      source.index("static int vdec_init_ctrls(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/videodev2.h>
#define VDEC_REVISION_MESON8B 1
#define VB2_MMAP 1
#define VB2_DMABUF 2
struct memops {int unused;};static const struct memops vb2_dma_contig_memops={0},vb2_vmalloc_memops={0};
static const int vdec_vb2_ops;
struct amvdec_vb2_buffer {int unused;};
struct platform {int revision;};
struct core {struct platform *platform;void *dev,*ge2d_dev;};
struct amvdec_session {struct core *core;int lock;};
struct vb2_queue {unsigned type,io_modes,timestamp_flags,buf_struct_size,min_buffers_needed;
 const void *ops,*mem_ops;void *drv_priv,*dev,*lock;};
static int initialized;
static int vb2_queue_init(struct vb2_queue *q){(void)q;initialized++;return 0;}
''' + body + r'''
int main(void){struct platform p={1};struct core c={.platform=&p};struct amvdec_session s={.core=&c};
 struct vb2_queue src={0},dst={0};assert(!m2m_queue_init(&s,&src,&dst));
 assert(src.mem_ops==&vb2_dma_contig_memops&&src.io_modes==(VB2_MMAP|VB2_DMABUF));
 assert(dst.mem_ops==&vb2_vmalloc_memops&&dst.io_modes==VB2_MMAP);
 c.ge2d_dev=&c;assert(!m2m_queue_init(&s,&src,&dst));
 assert(dst.mem_ops==&vb2_dma_contig_memops&&dst.io_modes==(VB2_MMAP|VB2_DMABUF));
 assert(dst.drv_priv==&s&&dst.lock==&s.lock&&initialized==4);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
