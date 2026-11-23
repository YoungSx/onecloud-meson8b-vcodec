#!/usr/bin/env python3
import hashlib,json,os,shutil,subprocess,time,urllib.request
from pathlib import Path
ROOT=Path('/root/codec-p0-20261123')
COMPACT=ROOT/'compact'
INSTALLED=Path('/opt/meson8b-vcodec/current')
PACKAGE=ROOT/'trial-package'
RESULT=ROOT/'trial-results.json'
TOOL=INSTALLED/'scripts/runtime-device.py'
FFMPEG=INSTALLED/'tools/ffmpeg'
FFW=COMPACT/'tools'
OUTPUT=ROOT/'outputs'
OUTPUT.mkdir(exist_ok=True)
checks=[]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(name,args,env=None,allow_failure=False,limit=45):
 start=time.monotonic()
 with (OUTPUT/(name+'.log')).open('w') as log:
  r=subprocess.run(['timeout','-k','5',str(limit),*map(str,args)],env=dict(os.environ,**(env or {})),stdout=log,stderr=log)
 item={'name':name,'exit':r.returncode,'seconds':round(time.monotonic()-start,3)}
 checks.append(item)
 RESULT.write_text(json.dumps({'status':'running','checks':checks},indent=2)+'\n')
 print(json.dumps(item),flush=True)
 if not allow_failure:assert r.returncode==0,item
 return r.returncode

def hashes(path):return [s.rsplit(',',1)[-1].strip() for s in path.read_text().splitlines() if s and not s.startswith('#')]
def health():
 result={}
 for port in [5245,8000,8080,8123]:
  try:
   with urllib.request.urlopen('http://127.0.0.1:'+str(port),timeout=5) as r:result[str(port)]=r.status
  except Exception as e:result[str(port)]=str(e)
 return result

expected={'meson-vdec.ko':'34953c44cdfcaa997f96157a4036b6899bd9c6136a0111bf6c52ccdcedca6baa','meson-venc.ko':'7d31095d1fa82a4565f3175d20badb5e3ed3ec7d476065d16b895cafbe1817d2'}
for name,digest in expected.items():assert sha(COMPACT/'modules'/name)==digest
assert subprocess.run(['fuser','/dev/video0','/dev/video1'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==1
assert not PACKAGE.exists()
shutil.copytree(INSTALLED,PACKAGE)
for name in expected:shutil.copy2(COMPACT/'modules'/name,PACKAGE/'modules'/name)
(PACKAGE/'SHA256SUMS').unlink()
files={str(p.relative_to(PACKAGE)):sha(p) for p in sorted(PACKAGE.rglob('*')) if p.is_file()}
(PACKAGE/'SHA256SUMS').write_text(''.join(d+'  '+n+'\n' for n,d in files.items()))
run('package-check',['python3',TOOL,'check',PACKAGE])
original={str(p):sha(p) for p in INSTALLED.rglob('*') if p.is_file()}
fw_path=Path('/lib/firmware/meson/venc/meson8b_avc_enc.bin')
firmware=fw_path.read_bytes()
before=health();assert all(v==200 for v in before.values()),before
boot_before={str(p):sha(p) for p in Path('/boot').rglob('*') if p.is_file()}
clock_before=subprocess.check_output(['cat','/sys/kernel/debug/clk/clk_summary'],text=True)
(OUTPUT/'clock-before.log').write_text(clock_before)
(OUTPUT/'dmesg-before.log').write_text(subprocess.check_output(['dmesg'],text=True))
restore=True
passed=False
try:
 run('stop-original',['systemctl','stop','meson8b-vcodec.service'])
 run('load-candidate',['python3',TOOL,'load',PACKAGE])
 for tag,codec,count in [('h264-320x240-smoke','h264',50),('hevc-small','hevc',10),('hevc-crop','hevc',10),('hevc-1080p','hevc',30)]:
  output=OUTPUT/(tag+'.md5')
  run(tag,[FFMPEG,'-nostdin','-v','error','-filter_threads','1','-c:v',codec+'_v4l2m2m','-i',COMPACT/'inputs'/(tag+'.mp4'),'-map','0:v:0','-an','-pix_fmt','nv12','-threads:v','1','-f','framemd5','-y',output])
  hw=hashes(output);sw=hashes(COMPACT/'inputs'/(tag+'.md5'));assert len(hw)==count and hw==sw,(tag,len(hw),len(sw))
  checks[-1]['matching_pictures']=count
 for capture in [1,4]:
  for first in [False,True]:
   name='hevc-stop-cap'+str(capture)+'-'+('capture' if first else 'output')
   env={'CAPTURE_BUFFERS':str(capture),'STREAMOFF_AFTER':'3'}
   if first:env['STREAMOFF_CAPTURE_FIRST']='1'
   run(name,[FFW/'decoder-check','/dev/video0',COMPACT/'inputs/hevc-small.hevc','HEVC','320','240'],env=env)
   assert 'STREAMOFF frames=3' in (OUTPUT/(name+'.log')).read_text()
   name+='-reopen'
   run(name,[FFMPEG,'-nostdin','-v','error','-filter_threads','1','-c:v','hevc_v4l2m2m','-i',COMPACT/'inputs/hevc-small.mp4','-an','-pix_fmt','nv12','-threads:v','1','-f','framemd5','-y',OUTPUT/(name+'.md5')])
   assert hashes(OUTPUT/(name+'.md5'))==hashes(COMPACT/'inputs/hevc-small.md5')
   checks[-1]['matching_pictures']=10
 raw={'RAW_INPUT':str(COMPACT/'inputs/encode-reference.nv12')}
 run('h264-encode',[FFW/'encode-prepared','-s','6','/dev/video1',OUTPUT/'encoded.h264','/dev/null','320','240','12','NV12','90','H264'],env=raw)
 run('jpeg-encode',[FFW/'encode-prepared','/dev/video1',OUTPUT/'encoded.mjpg','/dev/null','320','240','3','NV12','90','JPEG'],env=raw)
 for mode in ['missing','truncated']:
  if mode=='missing':fw_path.unlink()
  else:fw_path.write_bytes(firmware[:256])
  try:
   code=run('firmware-'+mode,[FFW/'encode-prepared','/dev/video1',OUTPUT/(mode+'.h264'),'/dev/null','320','240','1','NV12','90','H264'],env=raw,allow_failure=True,limit=20)
   assert code not in [0,124,137],code
  finally:fw_path.write_bytes(firmware)
  run('firmware-'+mode+'-recovery',[FFW/'encode-prepared','/dev/video1',OUTPUT/(mode+'-recovery.h264'),'/dev/null','320','240','3','NV12','90','H264'],env=raw)
 after=health();assert after==before,after
 assert boot_before=={str(p):sha(p) for p in Path('/boot').rglob('*') if p.is_file()}
 passed=True
finally:
 fw_path.write_bytes(firmware)
 run('unload-candidate',['python3',TOOL,'unload'])
 run('restore-original',['systemctl','start','meson8b-vcodec.service'])
 assert original=={str(p):sha(p) for p in INSTALLED.rglob('*') if p.is_file()}
 assert health()==before
 assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
 assert Path('/sys/kernel/debug/meson-vdec/pool_bytes').read_text().strip()=='0'
 (OUTPUT/'clock-after.log').write_text(subprocess.check_output(['cat','/sys/kernel/debug/clk/clk_summary'],text=True))
 (OUTPUT/'dmesg-after.log').write_text(subprocess.check_output(['dmesg'],text=True))
 RESULT.write_text(json.dumps({'status':'bounded-trial-passed' if passed else 'trial-failed-original-restored','checks':checks,'original_restored':True,'services':health(),'module_hashes':expected,'firmware_restored':sha(fw_path)==hashlib.sha256(firmware).hexdigest(),'boot_unchanged':boot_before=={str(p):sha(p) for p in Path('/boot').rglob('*') if p.is_file()}},indent=2)+'\n')
