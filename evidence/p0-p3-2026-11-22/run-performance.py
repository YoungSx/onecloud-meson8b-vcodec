from pathlib import Path
import json,hashlib,subprocess,time,resource,urllib.request
base=Path('/mnt/sda2/codec-p0-p3');original=Path('/opt/meson8b-vcodec/current/modules');state={'status':'running','cases':[]};swapped=[]
def save():(base/'P1-PERF.json').write_text(json.dumps(state,indent=2)+'\n')
try:
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 for name,path in [('meson_venc',Path('/mnt/sda2/codec-official-compat/meson-venc.ko'))]:
  subprocess.run(['rmmod',name],check=True);swapped.append(name);subprocess.run(['insmod',str(path)],check=True)
 for version,digest in [('baseline','3c40bb75e686841e94f14c1b9173c99a134b789cdd60bd8c696935f73d458ce0'),('recycle','e6a3190')]:
  module=base/('meson-vdec.ko' if version=='baseline' else 'meson-vdec-recycle.ko');actual=hashlib.sha256(module.read_bytes()).hexdigest()
  if version=='baseline':assert actual==digest
  assert Path('/sys/module/meson_vdec/refcnt').read_text().strip()=='0'
  subprocess.run(['rmmod','meson_vdec'],check=True)
  if 'meson_vdec' not in swapped:swapped.append('meson_vdec')
  subprocess.run(['insmod',str(module)],check=True)
  for index in range(2):
   out=base/(version+'-transcode.h264');start=time.monotonic();before=resource.getrusage(resource.RUSAGE_CHILDREN)
   r=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-y','-c:v','h264_v4l2m2m','-i','/mnt/sda2/openlist-fork-staging/media/h264-1080p-audio.mp4','-frames:v','600','-an','-c:v','h264_v4l2m2m','-pix_fmt','nv12','-b:v','4000k','-g','60','-r','30','-fps_mode','cfr','-f','h264',str(out)],capture_output=True,text=True,timeout=90)
   elapsed=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   probe=json.loads(subprocess.check_output(['/mnt/sda2/codec-rate-delivery/ffprobe','-v','error','-count_frames','-show_entries','stream=nb_read_frames','-of','json',str(out)],text=True,timeout=35))
   row={'module':version,'sha256':actual,'index':index,'media_seconds':20,'wall_seconds':round(elapsed,3),'cpu_seconds':round(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,3),'rc':r.returncode,'frames':int(probe['streams'][0]['nb_read_frames']),'stderr':r.stderr[-600:]};state['cases'].append(row);save();print(version,index,row['wall_seconds'],row['frames'],flush=True);assert r.returncode==0 and row['frames']==600,row
  ref=[s.rsplit(',',1)[-1].strip() for s in Path('/root/venc-deploy/p5/h264-1080p-baseline.md5').read_text().splitlines() if s.startswith('0,')]
  r=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-filter_threads','1','-c:v','h264_v4l2m2m','-i','/root/venc-deploy/p5/h264-1080p-baseline.mp4','-pix_fmt','nv12','-threads:v','1','-f','framemd5','-'],capture_output=True,text=True,timeout=60)
  frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')];assert r.returncode==0 and frames==ref,(version,len(frames));state['cases'].append({'module':version,'case':'decode-hashes','frames':len(frames),'hashes_match':True});save()
 state['status']='pass'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 for name in reversed(swapped):subprocess.run(['rmmod',name],check=True);subprocess.run(['insmod',str(original/(name.replace('_','-')+'.ko'))],check=True)
 state['restored']=True;state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];save();print(state['status'],'restored',flush=True)
