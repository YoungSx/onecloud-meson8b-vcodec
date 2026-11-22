from pathlib import Path
import json,subprocess,hashlib,re
base=Path('/mnt/sda2/codec-official-compat');original=Path('/opt/meson8b-vcodec/current/modules/meson-venc.ko')
state={'status':'running','cases':[]};swapped=False
try:
 assert Path('/sys/module/meson_venc/refcnt').read_text().strip()=='0'
 assert hashlib.sha256((base/'meson-venc.ko').read_bytes()).hexdigest()=='e72e3a6697b5f7296520f8b339b7c7e6af9147b299d0639e92c5e42128f4747b'
 subprocess.run(['rmmod','meson_venc'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-venc.ko')],check=True)
 for pattern in ['static','motion','scene']:
  for bitrate in [800000,1600000,3000000]:
   out=base/'quality.h264';frames=90
   args=[str(base/'encode-test'),'-p',pattern,'-r','30','-c','bitrate=%d,gop=30'%bitrate,'/dev/video1',str(out),'/dev/null','640','360',str(frames),'NV12','90','H264']
   run=subprocess.run(args,capture_output=True,text=True,timeout=60);assert run.returncode==0,run.stderr[-500:]
   generator=subprocess.Popen([str(base/'encode-test'),'-g','-p',pattern,'-','-','-','640','360',str(frames),'NV12','90','H264'],stdout=subprocess.PIPE)
   stats=base/'psnr.log'
   try:
    check=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-f','rawvideo','-pix_fmt','nv12','-s','640x360','-r','30','-i','-','-r','30','-f','h264','-i',str(out),'-lavfi','[1:v]setpts=N/(30*TB),format=nv12[d];[0:v]setpts=N/(30*TB)[r];[d][r]psnr=stats_file='+str(stats)+':shortest=1','-f','null','-'],stdin=generator.stdout,capture_output=True,text=True,timeout=50)
   finally:generator.stdout.close();generator.wait(timeout=15)
   assert check.returncode==0,check.stderr[-600:]
   values=[float(v) for v in re.findall(r'psnr_y:([0-9.]+)',stats.read_text())];assert len(values)==frames,len(values)
   row={'pattern':pattern,'target_bps':bitrate,'frames':frames,'actual_bps':round(out.stat().st_size*8*30/frames),'psnr_y_mean':round(sum(values)/len(values),3),'psnr_y_min':min(values),'driver_stats':Path('/sys/kernel/debug/meson-venc/rate_control').read_text()}
   state['cases'].append(row);(base/'QUALITY.json').write_text(json.dumps(state,indent=2)+'\n');print(pattern,bitrate,row['actual_bps'],row['psnr_y_mean'],flush=True)
 state['status']='pass'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:subprocess.run(['rmmod','meson_venc'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True;(base/'QUALITY.json').write_text(json.dumps(state,indent=2)+'\n')
