from pathlib import Path
import subprocess,hashlib,json,os,urllib.request
base=Path('/mnt/sda2/codec-p0-p3');old=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko');state={'status':'running','cases':[]};swapped=False
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'P3-FORMATS-FINAL.json').write_text(json.dumps(state,indent=2)+'\n')
try:
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 assert hashlib.sha256((base/'meson-vdec-final.ko').read_bytes()).hexdigest()=='8ff62b9478f5927cd1133cdcdeed8c94d95ea05edfdf2c76d4423c17efa4eaed'
 subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-vdec-final.ko')],check=True)
 for codec,name,refname,tool in [('h264','h264-4ref-1080p.mp4','software-h264-4ref-1080p.md5','/usr/local/bin/ffmpeg'),('hevc','hevc-left16.mp4','software-hevc-left16.md5','/mnt/sda2/codec-official-compat/ffmpeg-upstream')]:
  ref=[s.rsplit(',',1)[-1].strip() for s in Path('/root/venc-deploy/p5/'+refname).read_text().splitlines() if s.startswith('0,')]
  for count in [2,4,20,32]:
   r=subprocess.run([tool,'-nostdin','-v','error','-filter_threads','1','-c:v',codec+'_v4l2m2m','-num_capture_buffers',str(count),'-i','/root/venc-deploy/p5/'+name,'-pix_fmt','nv12','-threads:v','1','-f','framemd5','-'],capture_output=True,text=True,timeout=60)
   frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')];row={'codec':codec,'sample':name,'capture_requested':count,'frames':len(frames),'expected':len(ref),'hashes_match':frames==ref,'rc':r.returncode,'stderr':r.stderr[-500:]};state['cases'].append(row);save();print(codec,count,len(frames),row['hashes_match'],flush=True);assert r.returncode==0 and frames==ref,row
 for name,fmt,w,h in [('regress.m2v','MPG2','320','240'),('m2v-bf.m2v','MPG2','320','240'),('m4v-repro.m4v','MPG4','320','240'),('SA00040.vc1','VC1G','176','144'),('SA00050.vc1','VC1G','352','288')]:
  signatures=[]
  for count in [1,4,20,32]:
   path=base/'format-hash.txt';env=dict(os.environ,CAPTURE_BUFFERS=str(count),FRAME_HASH=str(path));r=subprocess.run([str(base/'res-switch-test'),'/dev/video0','/root/venc-deploy/p6/'+name,fmt,w,h],capture_output=True,text=True,timeout=40,env=env)
   lines=path.read_text().splitlines();signature=[s.rsplit(' ',1)[-1] for s in lines];row={'codec':fmt,'sample':name,'capture_requested':count,'nonempty_frames':len(lines),'rc':r.returncode,'content_signature_sha256':hashlib.sha256('\n'.join(signature).encode()).hexdigest(),'log':(r.stdout+r.stderr)[-500:]};state['cases'].append(row);save();print(name,count,len(lines),r.returncode,flush=True);assert r.returncode==0,row
  assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
 state['status']='pass-with-documented-legacy-limits'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(old)],check=True)
 state['restored']=True;state['production_start_unchanged']=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]['State']['StartedAt']==prod['State']['StartedAt'];state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];save();print(state['status'],'restored',flush=True)
