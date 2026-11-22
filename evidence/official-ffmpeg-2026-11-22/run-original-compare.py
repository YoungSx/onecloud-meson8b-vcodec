from pathlib import Path
import subprocess,json
base=Path('/mnt/sda2/codec-official-compat');rows=[]
ref=[s.rsplit(',',1)[-1].strip() for s in Path('/root/venc-deploy/p5/h264-1080p-baseline.md5').read_text().splitlines() if s.startswith('0,')]
for mode in ['default','passthrough']:
 for i in range(3):
  args=['/usr/local/bin/ffmpeg','-nostdin','-v','warning','-c:v','h264_v4l2m2m','-i','/root/venc-deploy/p5/h264-1080p-baseline.mp4','-pix_fmt','nv12']
  if mode=='passthrough':args+=['-fps_mode','passthrough']
  args+=['-f','framemd5','-'];r=subprocess.run(args,capture_output=True,text=True,timeout=50)
  frames=[s.rsplit(',',1)[-1].strip() for s in r.stdout.splitlines() if s.startswith('0,')]
  rows.append({'module':'original','mode':mode,'index':i,'frames':len(frames),'rc':r.returncode,'first_diff':next((i for i,(a,b) in enumerate(zip(ref,frames)) if a!=b),None),'prefix':frames==ref[:len(frames)],'stderr':r.stderr[-1800:],'head':r.stdout.splitlines()[:12],'last':r.stdout.splitlines()[-2:]})
  print(mode,i,len(frames),rows[-1]['prefix'],flush=True)
(base/'ORIGINAL-COMPARE.json').write_text(json.dumps(rows,indent=2)+'\n')
