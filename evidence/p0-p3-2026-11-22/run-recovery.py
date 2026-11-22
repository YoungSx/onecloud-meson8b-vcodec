from pathlib import Path
import json,hashlib,subprocess,os,urllib.request
base=Path('/mnt/sda2/codec-p0-p3');original=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko');state={'status':'running','cases':[]};swapped=False
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'P0-RECOVERY.json').write_text(json.dumps(state,indent=2)+'\n')
def decode_ref(codec,media,reference,tool):
 r=subprocess.run([tool,'-nostdin','-v','error','-filter_threads','1','-c:v',codec+'_v4l2m2m','-i',media,'-pix_fmt','nv12','-threads:v','1','-f','framemd5','-'],capture_output=True,text=True,timeout=60)
 frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')];ref=[s.rsplit(',',1)[-1].strip() for s in Path(reference).read_text().splitlines() if s.startswith('0,')];assert r.returncode==0 and frames==ref,(codec,r.returncode,len(frames),r.stderr[-400:]);return len(frames)
try:
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 assert hashlib.sha256((base/'meson-vdec.ko').read_bytes()).hexdigest()=='3c40bb75e686841e94f14c1b9173c99a134b789cdd60bd8c696935f73d458ce0'
 subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-vdec.ko')],check=True)
 es=base/'baseline.h264';subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-y','-i','/root/venc-deploy/p5/h264-1080p-baseline.mp4','-c','copy','-bsf:v','h264_mp4toannexb','-f','h264',str(es)],check=True,timeout=20)
 for fmt,path,w,h in [('H264',str(es),'1920','1080'),('MPG2','/root/venc-deploy/p6/regress.m2v','320','240'),('MPG4','/root/venc-deploy/p6/m4v-repro.m4v','320','240')]:
  for capture_first in [False,True]:
   env=dict(os.environ,STREAMOFF_AFTER='5',CAPTURE_BUFFERS='1')
   if capture_first:env['STREAMOFF_CAPTURE_FIRST']='1'
   r=subprocess.run([str(base/'res-switch-test'),'/dev/video0',path,fmt,w,h],capture_output=True,text=True,timeout=35,env=env)
   row={'codec':fmt,'capture_first':capture_first,'rc':r.returncode,'log':(r.stdout+r.stderr)[-700:]};state['cases'].append(row);save();print(fmt,capture_first,r.returncode,flush=True);assert r.returncode==0 and 'STREAMOFF frames=' in r.stdout,row
   assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
   row['reopen_h264_frames']=decode_ref('h264','/root/venc-deploy/p5/h264-1080p-baseline.mp4','/root/venc-deploy/p5/h264-1080p-baseline.md5','/usr/local/bin/ffmpeg');save()
 state['hevc_frames']=decode_ref('hevc','/root/venc-deploy/p5/1080p.mp4','/root/venc-deploy/p5/software-1080.md5','/mnt/sda2/codec-official-compat/ffmpeg-upstream')
 state['status']='pass'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True;state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];state['production_start_unchanged']=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]['State']['StartedAt']==prod['State']['StartedAt'];save();print(state['status'],'restored',flush=True)
