from pathlib import Path
import subprocess,json,hashlib,urllib.request,re
base=Path('/mnt/sda2/codec-p0-p3');old=Path('/opt/meson8b-vcodec/current/modules');swapped=[];state={'status':'running','cases':[],'source_sha256':'de7da72f6b5e8cffc2c09cad549e325c7f5471107a535d9ba295aa7ac3113840'}
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'P2-QUALITY.json').write_text(json.dumps(state,indent=2)+'\n')
try:
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 assert hashlib.sha256((base/'real-640.mp4').read_bytes()).hexdigest()==state['source_sha256']
 for m,p in [('meson_vdec',base/'meson-vdec-final.ko'),('meson_venc',Path('/mnt/sda2/codec-official-compat/meson-venc.ko'))]:
  subprocess.run(['rmmod',m],check=True);swapped.append(m);subprocess.run(['insmod',str(p)],check=True)
 for fps in ['30','30000/1001','24']:
  for rate in ['800k','1600k','3000k']:
   out=base/'quality-real.h264';r=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-y','-c:v','h264_v4l2m2m','-i',str(base/'real-640.mp4'),'-frames:v','90','-an','-r',fps,'-fps_mode','cfr','-c:v','h264_v4l2m2m','-pix_fmt','nv12','-b:v',rate,'-g','30','-f','h264',str(out)],capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr[-400:]
   stats=base/'psnr-real.log';stats.unlink(missing_ok=True)
   check=subprocess.run(['/usr/local/bin/ffmpeg','-nostdin','-v','error','-filter_threads','1','-i',str(base/'real-640.mp4'),'-r',fps,'-f','h264','-i',str(out),'-filter_complex',f'[0:v]fps={fps},trim=end_frame=90,setpts=N/({fps}*TB),format=yuv420p[r];[1:v]setpts=N/({fps}*TB),format=yuv420p[d];[d][r]psnr=stats_file='+str(stats)+':shortest=1','-threads:v','1','-f','null','-'],capture_output=True,text=True,timeout=45);assert check.returncode==0,check.stderr[-400:]
   rows=[dict(p.split(':',1) for p in line.split()) for line in stats.read_text().splitlines()];assert len(rows)==90,len(rows)
   frac=30/1.001 if fps=='30000/1001' else int(fps);row={'fps':fps,'target_bps':int(rate[:-1])*1000,'frames':90,'actual_bps':round(out.stat().st_size*8*frac/90),'psnr_y_mean':round(sum(float(r['psnr_y']) for r in rows)/90,3),'psnr_y_min':min(float(r['psnr_y']) for r in rows),'psnr_avg_mean':round(sum(float(r['psnr_avg']) for r in rows)/90,3),'rate_stats':Path('/sys/kernel/debug/meson-venc/rate_control').read_text()};state['cases'].append(row);save();print(fps,rate,row['actual_bps'],row['psnr_y_mean'],flush=True)
 state['status']='pass'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 for m in reversed(swapped):subprocess.run(['rmmod',m],check=True);subprocess.run(['insmod',str(old/(m.replace('_','-')+'.ko'))],check=True)
 state['restored']=True;state['production_start_unchanged']=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]['State']['StartedAt']==prod['State']['StartedAt'];state['services']={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:state['services'][str(port)]=r.status;assert r.status==200
 state['services']['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert state['services']['arcane'];save();print(state['status'],'restored',flush=True)
