# P6 MJPEG Decode Acceptance — 2026-11-14

Commits: 00491e9 (feat(vdec)), 4f35deb (feat(firmware)), a18e820 (feat(venc)).
Target: OneCloud (Meson8b, 100.87.212.107), kernel 5.15.79-meson,
module meson-vdec.ko, firmware meson/vdec/meson8b_mjpeg.bin
(sha256 fa99f5a7b12f95577c2ba9ff1697d8aa50524ec682df418f80de9f813dd87d34).

## Bring-up findings (root causes fixed)

1. **Resume never ran**: `vdec_start_streaming`'s STATUS_INIT+CAPTURE
   gate is consumed by STREAMON(CAPTURE), but vb2 defers the OUTPUT
   queue's start_streaming (min_buffers_needed not met: REQBUFS+mmap
   without QBUF) to the first OUTPUT QBUF — which is after poweron.
   Symptom: fw_idx_to_vb2_idx kzalloc-zero, ~840 abort-storm lines.
   Fix: OUTPUT-side resume gate after poweron (exactly-once for all
   streamon orders across mpeg12/mpeg4/vc1/mjpeg).
2. **Only one frame decoded**: the MJPEG microcode raises the next
   picinfo only after the host clears MREG_FROM_AMRISC (vendor
   vmjpeg_isr does `WRITE_VREG(MREG_FROM_AMRISC, 0)`). Port omitted
   the clear.
3. **Chroma U missing**: microcode writes planar Y/U/V (vendor
   AV_SCRATCH_4-7 table packs three canvases per slot: 0x020100 =
   Y,U,V). The NV12M 2-canvas table (v<<16|v<<8|y) aliased U and V on
   one canvas; V overwrote U (hw chroma == sw V plane, corr 0.99).
   Fix: set_canvas_mjpeg() with 3 canvases (Y w×h, U w/2×h/2,
   V w/2×h/2 at shadow+output_size/4), CPU interleave to NV12M in
   amvdec_meson8b_copy_capture().
4. Harness: MJPEG frames split on SOI (ff d8 ff), not H.264 start
   codes; EOS exit on V4L2_BUF_FLAG_LAST (vb2 sets
   last_buffer_dequeued → subsequent DQBUF = EPIPE, which the old
   loop spun on at ~90% CPU).

## Results (res-switch-test /dev/video0 <stream> MJPG W H)

| stream           | frames dequeued | dmesg | rollback |
|------------------|-----------------|-------|----------|
| small.mjpg 320x240 (30 SOI) | 33, clean EOS | clean | - |
| hd720.mjpg 1280x720         | 33, clean EOS | clean | - |
| hd1080.mjpg 1920x1080       | 33, clean EOS | clean | - |
| regress.m2v (MPEG-2 720x576)| 96, clean EOS | clean | rmmod OK |

Firmware emits a picture only when the next one starts (shared
mpeg12-style ucode behavior): 29/30 frames carry data, the last is
flushed empty by drain.

Content verification (per-frame, hw dump vs ffmpeg mjpeg decode,
NV12M planes at the 64K-aligned bytesused layout):

- luma: linear fit gain 0.866 (320), 0.861 (720p/1080p), residual
  PSNR 33.3–33.5 (320x240), 38.5–38.6 (720p), 39.8 (1080p)
- chroma after interleave fix: gain 0.882–0.890, residual PSNR
  29.6–29.8 (320x240), 34.3–34.5 (720p), 35.7 (1080p)

The constant gain/offset is the vendor decoder's full-range vs
limited-range YCbCr handling (identical fit across all frames and
resolutions — a colorimetry difference, not decode error). Structure
correlation with software decode is ~0.99 per row; no corruption,
no ghosting, no missing blocks.

IRQ count: 1 per decoded frame (~31 per run), no interrupt storms.

## FORCE_KEY_FRAME (venc)

`ctrl-probe --list /dev/video0` enumerates: Codec Controls class,
B Frames, GOP Size, Bitrate, Sequence Header Mode, **Force Key Frame
(0x009909e5, type 4 = BUTTON)**, H264 Min/Max QP.
`S_EXT_CTRLS(0x9909e5)=0 OK`; s_ctrl zeroes gop_idx → next frame IDR
(idr = gop_idx == 0 at venc.c:999). GOP/BITRATE set paths re-tested
clean. rmmod meson_venc rollback OK.
