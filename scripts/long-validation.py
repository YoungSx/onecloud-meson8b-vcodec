#!/usr/bin/env python3
"""Bounded mixed-codec acceptance on the live target, with services left running."""
import json,pathlib,re,subprocess,time,hashlib
D=pathlib.Path('/root/venc-deploy')
results=[]
def command(args,name,limit=120):
 start=time.monotonic()
 with (D/(name+'.log')).open('w') as log:r=subprocess.run(args,stdout=log,stderr=log,timeout=limit)
 results.append({'test':name,'returncode':r.returncode,'seconds':time.monotonic()-start})
 print(results[-1],flush=True)
 assert r.returncode==0,name
 return (D/(name+'.log')).read_text()
try:
 command(['insmod',str(D/'meson8b-runtime.ko')],'long-runtime')
 command(['insmod',str(D/'meson-vdec.ko')],'long-decoder')
 command(['insmod',str(D/'meson-venc.ko')],'long-encoder')
 for i in range(3):
  command(['timeout','-k','5','120',str(D/'encode-test'),'/dev/video1',f'/tmp/jpeg-long-{i}.mjpg','/dev/null','1920','1080','300','NV12','90','JPEG'],f'jpeg-long-{i}')
  log=command([str(D/'ffprobe'),'-v','error','-count_frames','-show_entries','stream=nb_read_frames','-of','json',f'/tmp/jpeg-long-{i}.mjpg'],f'jpeg-long-probe-{i}')
  assert json.loads(log)['streams'][0]['nb_read_frames']=='300'
  command(['/usr/local/bin/ffmpeg','-v','error','-i',f'/tmp/jpeg-long-{i}.mjpg','-f','null','-'],f'jpeg-long-decode-{i}')
  for suffix in ['mjpg']:pathlib.Path(f'/tmp/jpeg-long-{i}.{suffix}').unlink()
 for i in range(10):
  command(['/usr/local/bin/ffmpeg','-nostdin','-hide_banner','-v','info','-c:v','h264_v4l2m2m','-num_capture_buffers','10','-i',str(D/'source.mp4'),'-an','-c:v','h264_v4l2m2m','-b:v','0','-fps_mode','passthrough','-f','h264','-y',f'/tmp/bframe-long-{i}.h264'],f'bframe-long-{i}')
  log=command([str(D/'ffprobe'),'-v','error','-count_frames','-show_entries','stream=nb_read_frames','-of','json',f'/tmp/bframe-long-{i}.h264'],f'bframe-long-probe-{i}')
  assert json.loads(log)['streams'][0]['nb_read_frames']=='60'
  command(['/usr/local/bin/ffmpeg','-v','error','-i',f'/tmp/bframe-long-{i}.h264','-f','null','-'],f'bframe-long-decode-{i}')
 for i in range(10):
  command([str(D/'ffmpeg'),'-nostdin','-v','info','-c:v','hevc_v4l2m2m','-num_capture_buffers','8','-i',str(D/'1080p.mp4'),'-pix_fmt','nv12','-filter_threads','1','-threads:v','1','-f','framemd5','-y',f'/tmp/hevc-final-{i}.md5'],f'hevc-final-{i}')
  h=[s.rsplit(',',1)[-1].strip() for s in pathlib.Path(f'/tmp/hevc-final-{i}.md5').read_text().splitlines() if s and not s.startswith('#')]
  assert len(h)==30
  if i:assert h==previous
  previous=h
 command([str(D/'ffmpeg'),'-nostdin','-v','info','-c:v','hevc_v4l2m2m','-num_capture_buffers','8','-i',str(D/'crop.mp4'),'-pix_fmt','nv12','-filter_threads','1','-threads:v','1','-f','framemd5','-y','/tmp/hevc-crop.md5'],'hevc-crop')
 print('DONE',flush=True)
finally:
 for module in ['meson_venc','meson_vdec','meson8b_runtime']:subprocess.run(['rmmod',module])
 (D/'long-results.json').write_text(json.dumps(results,indent=2)+'\n')
