from pathlib import Path
import hashlib,json,subprocess,urllib.request,os
base=Path('/mnt/sda2/codec-drain-followup');old=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko')
state={'status':'running','cases':[],'candidate_sha256':'7079a4ca9cb9c1fe3b2849e073a8006853596ebee0017b0ac3fb27857102dc00'};swapped=False
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'REGRESSION.json').write_text(json.dumps(state,indent=2)+'\n')
try:
 assert json.loads((base/'REORDER.json').read_text())['restored']
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
 assert hashlib.sha256((base/'meson-vdec-complete.ko').read_bytes()).hexdigest()==state['candidate_sha256']
 subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-vdec-complete.ko')],check=True)
 for codec,media,reference,tool in [('h264','h264-1080p-baseline.mp4','h264-1080p-baseline.md5','/usr/local/bin/ffmpeg'),('hevc','1080p.mp4','software-1080.md5','/mnt/sda2/codec-official-compat/ffmpeg-upstream')]:
  ref=[s.rsplit(',',1)[-1].strip() for s in Path('/root/venc-deploy/p5/'+reference).read_text().splitlines() if s.startswith('0,')]
  for i in range(3):
   r=subprocess.run([tool,'-nostdin','-v','error','-filter_threads','1','-c:v',codec+'_v4l2m2m','-i','/root/venc-deploy/p5/'+media,'-pix_fmt','nv12','-threads:v','1','-f','framemd5','-'],capture_output=True,text=True,timeout=60)
   frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')]
   row={'codec':codec,'index':i,'frames':len(frames),'expected':len(ref),'rc':r.returncode,'hashes_match':frames==ref,'stderr':r.stderr[-500:]};state['cases'].append(row);save();print(codec,i,len(frames),row['hashes_match'],flush=True);assert frames==ref and r.returncode==0,row
 for media,fourcc,w,h in [('regress.m2v','MPG2','320','240'),('m2v-bf.m2v','MPG2','320','240'),('m4v-repro.m4v','MPG4','320','240'),('SA00040.vc1','VC1G','176','144')]:
  for count in [1,8]:
   path=base/'regress-frames.txt';env=dict(os.environ,FRAME_HASH=str(path),CAPTURE_BUFFERS=str(count))
   r=subprocess.run(['/mnt/sda2/codec-official-compat/res-switch-last-test','/dev/video0','/root/venc-deploy/p6/'+media,fourcc,w,h],capture_output=True,text=True,timeout=35,env=env)
   frames=path.read_text().splitlines();row={'codec':fourcc,'input':media,'capture_buffers':count,'rc':r.returncode,'nonempty_frames':len(frames),'last_hashes':frames[-3:],'log':(r.stdout+r.stderr)[-800:]};state['cases'].append(row);save();print(media,count,len(frames),r.returncode,flush=True);assert r.returncode==0,row
 state['status']='pass-with-documented-tail-limits'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(old)],check=True)
 state['restored']=True;state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];after=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0];state['production_start_unchanged']=after['State']['StartedAt']==prod['State']['StartedAt'];save();print(state['status'],'restored',flush=True)
