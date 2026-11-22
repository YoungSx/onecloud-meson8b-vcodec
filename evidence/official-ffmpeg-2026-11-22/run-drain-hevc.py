from pathlib import Path
import subprocess,json,hashlib,os,urllib.request
base=Path('/mnt/sda2/codec-official-compat');original=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko')
state={'status':'running','legacy':[],'hevc':[]};swapped=False
def save():(base/'DRAIN-HEVC.json').write_text(json.dumps(state,indent=2)+'\n')
def legacy(module,client,name,fmt,w,h,expected):
 for index in range(2):
  path=base/'last-frame-hashes.txt';dump=base/'last-pictures.nv12';env=dict(os.environ,FRAME_HASH=str(path),FRAME_DUMP=str(dump))
  r=subprocess.run([str(base/client),'/dev/video0','/root/venc-deploy/p6/'+name,fmt,w,h],capture_output=True,text=True,timeout=35,env=env)
  lines=path.read_text().splitlines() if path.exists() else []
  row={'module':module,'client':client,'input':name,'index':index,'rc':r.returncode,'frames':len(lines),'expected':expected,'last_hashes':lines[-3:],'log':(r.stdout+r.stderr)[-800:]}
  if name=='m2v-bf.m2v' and client=='res-switch-last-test':
   compare=subprocess.run(['python3','/root/venc-deploy/p6/m2v-frame-order.py',str(dump),'/root/venc-deploy/p6/sw-m2v-bf.nv12'],capture_output=True,text=True,timeout=60)
   row['content_match']=compare.returncode==0;row['content_log']=(compare.stdout+compare.stderr)[-900:]
  if name=='regress.m2v' and client=='res-switch-last-test':
   raw=dump.read_bytes();tight=hashlib.md5()
   for offset in range(0,len(raw),196608):
    for plane,height in [(offset,240),(offset+131072,120)]:
     for y in range(height):tight.update(raw[plane+y*320:plane+y*320+320])
   row['visible_md5']=tight.hexdigest();row['content_match']=tight.hexdigest()=='f555b2b0373a7a052455c08d24c59391'
  state['legacy'].append(row);save();print(module,client,name,index,len(lines),row.get('content_match'),flush=True)
try:
 assert json.loads((base/'PARSER-FINAL.json').read_text())['restored']
 for name in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+name+'/refcnt').read_text().strip()=='0'
 for client in ['res-switch-test','res-switch-last-test']:
  for name,fmt,w,h,expected in [('m2v-bf.m2v','MPG2','320','240',150),('regress.m2v','MPG2','320','240',90)]:legacy('original',client,name,fmt,w,h,expected)
 assert hashlib.sha256((base/'parser-meson-vdec.ko').read_bytes()).hexdigest()=='a49c60d0ec90a4e32c354930403609637a782319fa0a145298d46fa5a95ec9ae'
 assert hashlib.sha256((base/'ffmpeg-upstream').read_bytes()).hexdigest()=='c22f5876df20b0db5bb878234224f2fcfff55814ca853d2d3262738095d375a0'
 subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(base/'parser-meson-vdec.ko')],check=True)
 for name,refname in [('1080p.mp4','software-1080.md5'),('hevc-left16.mp4','software-hevc-left16.md5')]:
  ref=[s.rsplit(',',1)[-1].strip() for s in Path('/root/venc-deploy/p5/'+refname).read_text().splitlines() if s.startswith('0,')]
  for i in range(2):
   r=subprocess.run([str(base/'ffmpeg-upstream'),'-nostdin','-v','error','-filter_threads','1','-c:v','hevc_v4l2m2m','-i','/root/venc-deploy/p5/'+name,'-pix_fmt','nv12','-threads:v','1','-f','framemd5','-'],capture_output=True,text=True,timeout=60)
   frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')]
   row={'input':name,'index':i,'frames':len(frames),'expected':len(ref),'hashes_match':frames==ref,'rc':r.returncode,'stderr':r.stderr[-900:]};state['hevc'].append(row);save();print('hevc',name,i,len(frames),frames==ref,flush=True)
 for name,fmt,w,h,expected in [('m2v-bf.m2v','MPG2','320','240',150),('regress.m2v','MPG2','320','240',90),('m4v-repro.m4v','MPG4','320','240',90),('SA00040.vc1','VC1G','176','144',None)]:legacy('candidate','res-switch-last-test',name,fmt,w,h,expected)
 state['hevc_status']='pass' if all(r['rc']==0 and r['hashes_match'] for r in state['hevc']) else 'fail'
 state['status']='pass' if state['hevc_status']=='pass' and all(r['rc']==0 and (r['expected'] is None or r['frames']==r['expected']) and r.get('content_match',True) for r in state['legacy'] if r['client']=='res-switch-last-test') else 'partial'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True
 state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];save();print(state['status'],'restored',flush=True)
