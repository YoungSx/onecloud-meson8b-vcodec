import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('encode_baseline', ROOT / 'scripts/encode-baseline.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)


@unittest.skipUnless(shutil.which('cc'), 'needs a host C compiler')
class ClientReferenceTests(unittest.TestCase):
    """-g must reproduce the device run's reference without a device."""

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.client = Path(cls.directory.name) / 'encode-test'
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'tests/encode-test.c'),
                        '-o', str(cls.client)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def reference(self, pattern, width=64, height=48, frames=35):
        return subprocess.run([str(self.client), '-g', '-p', pattern, '-', '-', '-', str(width),
                               str(height), str(frames), 'NV12', '90', 'H264'],
                              stdout=subprocess.PIPE, check=True).stdout

    def test_patterns_are_deterministic_and_in_range(self):
        digests = set()
        for pattern in ('gradient', 'static', 'motion', 'noise', 'scene'):
            data = self.reference(pattern)
            self.assertEqual(len(data), 64 * 48 * 3 // 2 * 35)
            self.assertEqual(data, self.reference(pattern))
            frame = 64 * 48 * 3 // 2
            for start in range(0, len(data), frame):
                luma, chroma = data[start:start + 64 * 48], data[start + 64 * 48:start + frame]
                # the legacy gradient keeps its original 16..239 luma ramp
                self.assertTrue(16 <= min(luma) and
                                max(luma) <= (239 if pattern == 'gradient' else 235), pattern)
                self.assertTrue(16 <= min(chroma) and max(chroma) <= 240, pattern)
            digests.add(hashlib.sha256(data).hexdigest())
        self.assertEqual(len(digests), 5)

    def test_static_repeats_and_scene_cuts(self):
        frame = 64 * 48 * 3 // 2
        static, scene = self.reference('static'), self.reference('scene')
        self.assertEqual(static[:frame], static[-frame:])
        self.assertNotEqual(scene[29 * frame:30 * frame], scene[30 * frame:31 * frame])

    def test_rejects_bad_controls_and_patterns(self):
        for arguments in (['-p', 'bogus'], ['-c', 'gop'], ['-c', 'gop=1@'], ['-c', 'nosuch=1']):
            result = subprocess.run([str(self.client), *arguments, '-g', '-', '-', '-', '32', '32',
                                     '1', 'NV12', '90', 'H264'], capture_output=True)
            self.assertEqual(result.returncode, 2, arguments)

    def test_dma_buf_cpu_sync_retries_only_interruptions(self):
        code = r'''
#define ioctl sync_ioctl
#define main diagnostic_main
#include "encode-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <stdbool.h>
static unsigned calls;static bool fail;
static uint64_t expected;
int sync_ioctl(int file,unsigned long request,...){
 va_list ap;va_start(ap,request);struct dma_buf_sync *sync=va_arg(ap,void *);va_end(ap);
 assert(file==17&&request==DMA_BUF_IOCTL_SYNC&&sync->flags==expected);calls++;
 if(fail){errno=EIO;return -1;}
 if(calls==1){errno=EINTR;return -1;}
 if(calls==2){errno=EAGAIN;return -1;}
 return 0;
}
int main(void){
 struct mapped p={.fd=17};expected=DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE;
 sync_plane(&p,expected);assert(!calls);
 dmabuf="dumb";sync_plane(&p,expected);assert(calls==3);
 expected=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;sync_plane(&p,expected);assert(calls==4);
 pid_t child=fork();assert(child>=0);
 if(!child){fail=true;sync_plane(&p,expected);_exit(99);}
 int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'tests'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_single_memory_plane_nv12_matches_multiplanar_pixels(self):
        code = r'''
#define main diagnostic_main
#include "encode-test.c"
#undef main
#include <assert.h>
int main(void){
 w=66;h=18;pattern=GRADIENT;
 for(unsigned i=0;i<3;i++){gradient[i]=malloc(i?w*h/4:w*h);assert(gradient[i]);memset(gradient[i],80+i*50,i?w*h/4:w*h);}
 unsigned stride[3]={96,96,0},luma=stride[0]*h,total=luma*3/2;
 unsigned char *single=malloc(total+16),*y=calloc(1,luma),*uv=calloc(1,luma/2);assert(single&&y&&uv);
 memset(single,0,total);memset(single+total,0xa5,16);
 unsigned char *one[3]={single,NULL,NULL},*two[3]={y,uv,NULL};
 for(unsigned frame=0;frame<3;frame++){
  for(format=formats;format->fourcc!=V4L2_PIX_FMT_NV12;format++){}
  assert(format->planes==1);
  put_frame(frame,one,stride);
  format=&formats[0];assert(format->fourcc==V4L2_PIX_FMT_NV12M&&format->planes==2);
  put_frame(frame,two,stride);
  assert(!memcmp(single,y,luma)&&!memcmp(single+luma,uv,luma/2));
  for(unsigned i=0;i<16;i++)assert(single[total+i]==0xa5);
 }
 for(unsigned i=0;i<3;i++)free(gradient[i]);
 free(single);free(y);free(uv);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'tests'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


class BaselineReportTests(unittest.TestCase):
    def test_frame_statistics(self):
        lines = ['frame %d bytes %d flags %s latency_us %d' % (i, 1000 * (i + 1),
                                                                '0x4009' if i % 3 == 0 else '0x4001',
                                                                100 + i)
                 for i in range(6)]
        stats = baseline.frame_stats('\n'.join(lines + ['PASS: 6 frames, loop_us 2000000']), 6, 3)
        self.assertEqual(stats['keyframes'], [0, 3])
        self.assertEqual(stats['bytes'], 21000)
        self.assertEqual(stats['average_kbps'], 84.0)
        self.assertEqual(stats['peak_1s_kbps'], 120.0)      # frames 3..5
        self.assertEqual(stats['latency_us'], {'median': 103, 'p95': 105, 'max': 105})
        self.assertEqual(stats['client_fps'], 3.0)
        with self.assertRaisesRegex(RuntimeError, 'frames 5 of 6'):
            baseline.frame_stats('\n'.join(lines[:5]), 6, 3)

    def test_control_expansion_and_keyframes(self):
        self.assertEqual(baseline.expand('bitrate={bpp:0.5}', 20, 10, 30, None), 'bitrate=3000')
        self.assertEqual(baseline.expand('bitrate={fixed:1.5}', 20, 10, 30, 800.0),
                         'bitrate=1200000')
        with self.assertRaises(ValueError):
            baseline.expand('bitrate={fixed:2}', 20, 10, 30, None)
        self.assertEqual(baseline.expected_keyframes(['gop=4'], 10), [0, 4, 8])
        self.assertEqual(baseline.expected_keyframes(['bitrate=0'], 25), [0, 12, 24])
        self.assertIsNone(baseline.expected_keyframes(['gop=0'], 10))
        self.assertIsNone(baseline.expected_keyframes(['force-key=0@5'], 10))

    def test_psnr_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'psnr.log'
            path.write_text('n:1 mse_avg:1 psnr_avg:40.00 psnr_y:39.00\n'
                            'n:2 mse_avg:0 psnr_avg:inf psnr_y:inf\n')
            result = baseline.read_psnr(path, 2)
            self.assertEqual(result['psnr_y_min'], 39.0)
            self.assertEqual(result['psnr_y_mean'], 69.0)
            with self.assertRaisesRegex(RuntimeError, '2 of 3'):
                baseline.read_psnr(path, 3)


if __name__ == '__main__':
    unittest.main()
