"""Run the driver's real picture callbacks with hardware and lock stubs."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


@unittest.skipUnless(shutil.which('cc'), 'needs a host C compiler')
class PictureBoundaryTests(unittest.TestCase):
    def test_controls_restart_force_key_and_overflow(self):
        driver = (ROOT / 'venc/venc.c').read_text()
        start = driver.index('static bool venc_pick_picture(')
        end = driver.index('\nstatic void venc_work(', start)
        prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32; typedef int64_t s64;
#define div64_u64(a,b) ((a)/(b))
#define div64_s64(a,b) ((a)/(s64)(b))
#define U32_MAX UINT32_MAX
#define max_t(t,a,b) ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
#define mutex_lock(x) ((void)(x))
#define mutex_unlock(x) ((void)(x))
#define spin_lock_irqsave(x,f) ((void)(x),(f)=0)
#define spin_unlock_irqrestore(x,f) ((void)(x),(void)(f))
#define dev_warn_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#include "rate-control.h"
#include "rate-limit.h"
struct venc_core {
    int rc_stats_lock;
    struct {u64 frames,bytes,target_bps,current_bps,peak_bps,window_ns;
            u64 saturated_frames,saturated_ns,overshoots,overflows;
            u64 peak_limit_bps,cpb_bytes,credit_bits,budget_violations;
            bool limit_terminal;s32 qp;} rc_stats;
};
struct venc_ctx {
    struct {void *lock;} ctrl_handler; struct venc_core *core;
    u32 slice_cfg,gop_idx,gop_size,quant;
    bool force_key,rc_enable,rc_restart,picture_rc,rc_unreachable_reported;
    s32 i_qp,p_qp; struct venc_rc rc_config,rc;
    struct venc_rc_recovery recovery; struct venc_rc_measurement rc_measure;
    u32 peak_bitrate,cpb_size;struct venc_rate_limit limit;
};
static u32 venc_slice_cfg(struct venc_ctx *ctx) {(void)ctx;return 0;}
'''
        cases = r'''
int main(void) {
    struct venc_core core={0}; struct venc_ctx c={0}; c.core=&core;
    c.rc_enable=1;c.rc_restart=1;c.gop_size=30;c.i_qp=c.p_qp=26;
    c.rc_config=(struct venc_rc){.bitrate=2000000,.numerator=1,.denominator=30,
                                .qp_min=0,.qp_max=51};
    bool idr=venc_pick_picture(&c,true);assert(idr);
    c.force_key=true; /* a second request arrives during the first IDR */
    c.rc_config.bitrate=3000000;
    venc_account_picture(&c,idr,10000,0);
    assert(c.force_key && c.rc.bitrate==2000000);
    assert(venc_pick_picture(&c,false) && c.rc.bitrate==3000000 && !c.force_key);
    c.rc.p_complexity=90000;c.rc.fullness=123456;
    assert(venc_pick_picture(&c,true) && !c.rc.p_complexity && !c.rc.fullness);
    c.rc.qp=32;c.gop_idx=1;
    assert(!venc_pick_picture(&c,false) && c.quant==32);
    venc_account_picture(&c,false,0,-ENOSPC);assert(c.recovery.floor==38);
    assert(venc_pick_picture(&c,false) && c.quant>=38);
    venc_account_picture(&c,true,10000,0);
    assert(!venc_pick_picture(&c,false) && c.quant>=38);
    venc_account_picture(&c,false,0,-ENOSPC);
    assert(venc_pick_picture(&c,false) && c.quant>=44);
    venc_account_picture(&c,true,0,-ENOSPC);assert(c.recovery.terminal);
    c.peak_bitrate=1000000;c.cpb_size=1;
    assert(venc_pick_picture(&c,true));
    assert(venc_account_picture(&c,true,1025,0)==-EOVERFLOW && c.limit.terminal);
    assert(core.rc_stats.budget_violations==1 && !core.rc_stats.frames);
    c.peak_bitrate=2000000;c.cpb_size=512;
    assert(venc_pick_picture(&c,false));
    assert(c.limit.terminal); /* Controls cannot revive a failed queue. */
    assert(venc_pick_picture(&c,true) && !c.limit.terminal);
    puts("picture boundary and overflow recovery: ok"); return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source, binary = pathlib.Path(directory) / 'state.c', pathlib.Path(directory) / 'state'
            source.write_text(prefix + driver[start:end] + cases)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'venc'), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
