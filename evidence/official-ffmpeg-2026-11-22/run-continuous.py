from pathlib import Path
import json,hashlib,subprocess,time,signal,urllib.request
base=Path('/mnt/sda2/codec-official-compat');original=Path('/opt/meson8b-vcodec/current/modules/meson-venc.ko');state={'status':'running','iterations':0,'frames':0,'services_samples':0};swapped=False
try:
 assert Path('/sys/module/meson_venc/refcnt').read_text().strip()=='0';assert Path('/sys/module/meson_vdec/refcnt').read_text().strip()=='0'
 assert hashlib.sha256((base/'meson-venc.ko').read_bytes()).hexdigest()=='e72e3a6697b5f7296520f8b339b7c7e6af9147b299d0639e92c5e42128f4747b'
 subprocess.run(['rmmod','meson_venc'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-venc.ko')],check=True)
 start=time.monotonic()
 while time.monotonic()-start<1800:
  out=base/'continuous.h264';r=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-hide_banner','-y','-v','error','-c:v','h264_v4l2m2m','-i','/mnt/sda2/openlist-fork-staging/media/h264-1080p-audio.mp4','-frames:v','120','-an','-c:v','h264_v4l2m2m','-pix_fmt','nv12','-b:v','4000k','-g','60','-r','30','-fps_mode','cfr','-f','h264',str(out)],capture_output=True,text=True,timeout=60)
  assert r.returncode==0 and not r.stderr,r.stderr[-500:]
  probe=json.loads(subprocess.check_output(['/mnt/sda2/codec-rate-delivery/ffprobe','-v','error','-count_frames','-show_entries','stream=nb_read_frames','-of','json',str(out)],text=True,timeout=25));assert probe['streams'][0]['nb_read_frames']=='120',probe
  decode=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-threads','1','-i',str(out),'-f','null','-'],capture_output=True,text=True,timeout=25);assert decode.returncode==0 and not decode.stderr,decode.stderr[-500:]
  state['iterations']+=1;state['frames']+=120;state['seconds']=round(time.monotonic()-start,3)
  for port in [5245,8000,7575,8123]:
   with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=5) as response:assert response.status==200
  assert json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];state['services_samples']+=1
  (base/'CONTINUOUS.json').write_text(json.dumps(state,indent=2)+'\n')
 state['status']='pass'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_venc'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True;(base/'CONTINUOUS.json').write_text(json.dumps(state,indent=2)+'\n');print(json.dumps(state,indent=2))
