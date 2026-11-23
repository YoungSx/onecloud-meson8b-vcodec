"""STREAMOFF discards HEVC references instead of restarting delivery work."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class DecoderShutdownTests(unittest.TestCase):
    def test_hevc_stop_does_not_queue_delivery_from_freed_codec(self):
        source = (ROOT / 'vdec/codec_hevc.c').read_text()
        start = source.index('static int codec_hevc_stop(')
        end = source.index('\nstatic struct hevc_frame *', start)
        code = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
typedef uintptr_t dma_addr_t;
#define SIZE_WORKSPACE 4096
struct amvdec_core {void *dev;};
struct codec_hevc {int watchdog,lock;void *workspace_vaddr;dma_addr_t workspace_paddr;};
struct amvdec_session {struct amvdec_core *core;void *priv;};
static int locked,freed,forgotten,queued;
static void cancel_delayed_work_sync(int *p){(void)p;}
static void mutex_lock(int *p){(void)p;assert(!locked);locked=1;}
static void mutex_unlock(int *p){(void)p;assert(locked);locked=0;}
static void mutex_destroy(int *p){(void)p;assert(!locked);}
static void dma_free_coherent(void *d,unsigned n,void *p,dma_addr_t a){(void)d;(void)a;assert(n==4096 && locked);freed++;free(p);}
void codec_hevc_flush_output(struct amvdec_session *s){(void)s;queued++;}
void codec_hevc_forget_frames(struct codec_hevc *h){(void)h;assert(locked);forgotten++;}
'''
        code += source[start:end]
        code += r'''
int main(void){struct amvdec_core c={0};struct codec_hevc h={.workspace_vaddr=malloc(4096)};struct amvdec_session s={&c,&h};assert(!codec_hevc_stop(&s));assert(freed==1 && forgotten==1 && !queued && !locked);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'check.c').write_text(code)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(path / 'check.c'), '-o', str(path / 'check')], check=True)
            subprocess.run([str(path / 'check')], check=True)
