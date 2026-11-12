import pathlib, subprocess, time, json
p=pathlib.Path('/tmp/meson8b-vcodec-runtime')
def cpu_times():
    parts=open('/proc/stat').readline().split()[1:]
    return [int(x) for x in parts]  # user nice system idle iowait irq softirq steal
loaded=[]
try:
 for name in ['meson8b-runtime','meson-vdec']:
  subprocess.run(['insmod',str(p/(name+'.ko'))],check=True,timeout=8);loaded.append(name.replace('-','_'))
 res={}
 for tag,dec in [('sw','h264'),('hw','h264_v4l2m2m')]:
  before=cpu_times()
  t0=time.monotonic()
  cmd=['timeout','-s','INT','-k','5s','90s','/usr/local/bin/ffmpeg','-nostdin','-hide_banner','-loglevel','error','-c:v',dec]
  if tag=='hw':cmd+=['-num_output_buffers','4','-num_capture_buffers','8']
  cmd+=['-i',str(p/'baseline-1080p.mp4'),'-an','-f','null','-']
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=100)
  el=time.monotonic()-t0
  after=cpu_times()
  busy=sum(after[:4])-sum(before[:4]) if False else None
  # user,system,irq,softirq deltas
  d={k:after[i]-before[i] for i,k in enumerate(['user','nice','system','idle','iowait','irq','softirq','steal'])}
  total=sum(d.values())
  d['elapsed']=el; d['rc']=r.returncode; d['total_ticks']=total
  res[tag]=d
  print(tag,d,flush=True)
finally:
 for name in reversed(loaded):
  print('unload',name,subprocess.run(['rmmod',name],timeout=8).returncode,flush=True)
(p/'syscpu-bench.json').write_text(json.dumps(res,indent=2))
