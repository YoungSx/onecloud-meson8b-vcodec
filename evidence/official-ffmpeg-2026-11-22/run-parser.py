from pathlib import Path
import hashlib,json,subprocess,time,urllib.request,os
base=Path('/mnt/sda2/codec-official-compat'); original=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko')
state={'status':'running','cases':[],'decoder_sha256':'a49c60d0ec90a4e32c354930403609637a782319fa0a145298d46fa5a95ec9ae'}; swapped=False
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'PARSER.json').write_text(json.dumps(state,indent=2)+'\n')
def services():
 result={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:result[str(port)]=r.status;assert r.status==200
 result['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert result['arcane'];return result
def run(args,timeout=60,**kw):return subprocess.run(args,capture_output=True,text=True,timeout=timeout,**kw)
def reference(path):return [s.rsplit(',',1)[-1].strip() for s in path.read_text().splitlines() if s.startswith('0,')]
try:
 assert json.loads((base/'CONTINUOUS.json').read_text())['restored']
 for name in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+name+'/refcnt').read_text().strip()=='0'
 assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
 state['services_before']=services()
 candidate=base/'parser-meson-vdec.ko';assert hashlib.sha256(candidate.read_bytes()).hexdigest()==state['decoder_sha256']
 subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(candidate)],check=True)
 for i in range(15):
  r=run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-c:v','h264_v4l2m2m','-i','/root/venc-deploy/p5/h264-1080p-baseline.mp4','-pix_fmt','nv12','-f','framemd5','-'])
  frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')];expected=reference(Path('/root/venc-deploy/p5/h264-1080p-baseline.md5'))
  state['cases'].append({'codec':'h264','index':i,'rc':r.returncode,'frames':len(frames),'expected':len(expected),'hashes_match':frames==expected,'stderr':r.stderr[-500:]});save()
  print('h264',i,len(frames),frames==expected,flush=True)
 for i in range(2):
  r=run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-c:v','hevc_v4l2m2m','-i','/root/venc-deploy/p5/hevc-left16.mp4','-pix_fmt','nv12','-f','framemd5','-'])
  frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')];expected=reference(Path('/root/venc-deploy/p5/software-hevc-left16.md5'))
  state['cases'].append({'codec':'hevc','index':i,'rc':r.returncode,'frames':len(frames),'expected':len(expected),'hashes_match':frames==expected,'stderr':r.stderr[-500:]});save();print('hevc',i,len(frames),frames==expected,flush=True)
 for name,fourcc,w,h in [('m2v-bf.m2v','MPG2','320','240'),('regress.m2v','MPG2','320','240'),('m4v-repro.m4v','MPG4','320','240'),('SA00040.vc1','VC1G','176','144')]:
  path='/root/venc-deploy/p6/'+name
  for i in range(3):
   hashes=base/'parser-frames.txt';env=dict(os.environ,FRAME_HASH=str(hashes));r=run([str(base/'res-switch-test'),'/dev/video0',path,fourcc,w,h],timeout=40,env=env)
   lines=hashes.read_text().splitlines() if hashes.exists() else []
   state['cases'].append({'codec':fourcc,'input':name,'index':i,'rc':r.returncode,'nonempty_frames':len(lines),'expected_frames':{'m2v-bf.m2v':150,'regress.m2v':90,'m4v-repro.m4v':90}.get(name),'count_match':len(lines)=={'m2v-bf.m2v':150,'regress.m2v':90,'m4v-repro.m4v':90}.get(name,len(lines)),'last_hashes':lines[-3:],'log':(r.stdout+r.stderr)[-1000:]});save();print(name,i,len(lines),r.returncode,flush=True)
 state['status']='pass' if all(c.get('hashes_match',True) and c.get('count_match',True) and c['rc']==0 for c in state['cases']) else 'partial'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True;state['services_after']=services();after=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0];state['production_start_unchanged']=prod['State']['StartedAt']==after['State']['StartedAt'];save();print(state['status'],'restored',flush=True)
