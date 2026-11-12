import pathlib, subprocess, time, json, statistics, os
p=pathlib.Path('/tmp/meson8b-vcodec-runtime')
loaded=[]
old=set(subprocess.check_output(['dmesg'],text=True,timeout=5).splitlines())
try:
 for name in ['meson8b-runtime','meson-vdec']:
  subprocess.run(['insmod',str(p/(name+'.ko'))],check=True,timeout=8);loaded.append(name.replace('-','_'))
 res={}
 for tag,dec in [('sw','h264'),('hw','h264_v4l2m2m')]:
  runs=[];prev_u=prev_s=0.0
  for i in range(3):
   cmd=['timeout','-s','INT','-k','5s','90s','/usr/local/bin/ffmpeg','-nostdin','-hide_banner','-loglevel','error','-c:v',dec]
   if tag=='hw':cmd+=['-num_output_buffers','4','-num_capture_buffers','8']
   cmd+=['-i',str(p/'baseline-1080p.mp4'),'-an','-pix_fmt','nv12','-f','framemd5','-y',str(p/('bench-'+tag+'.md5'))]
   t0=time.monotonic()
   r=subprocess.run(cmd,capture_output=True,text=True,timeout=100)
   el=time.monotonic()-t0
   t=os.times()
   runs.append({'rc':r.returncode,'user':t.children_user-prev_u,'sys':t.children_system-prev_s,'cpu':(t.children_user-prev_u)+(t.children_system-prev_s),'elapsed':el})
   prev_u,prev_s=t.children_user,t.children_system
  res[tag]=runs
  print(tag,runs,flush=True)
finally:
 for name in reversed(loaded):
  print('unload',name,subprocess.run(['rmmod',name],timeout=8).returncode,flush=True)
 time.sleep(.5)
 new=[x for x in subprocess.check_output(['dmesg'],text=True,timeout=5).splitlines() if x not in old]
 assert not any(('BUG:' in x or 'WARNING:' in x or 'Oops:' in x or 'refcount_t:' in x) for x in new)
 print('\n'.join(new),flush=True)
sw=statistics.median(r['cpu'] for r in res['sw']);hw=statistics.median(r['cpu'] for r in res['hw'])
res['median_cpu_s']={'sw':sw,'hw':hw};res['cpu_reduction_fraction']=1-hw/sw
(p/'cpu-bench.json').write_text(json.dumps(res,indent=2))
print('sw_median_cpu_s',sw,'hw_median_cpu_s',hw,'cpu_reduction_fraction',res['cpu_reduction_fraction'],flush=True)
assert all(r['rc']==0 for t in res.values() if isinstance(t,list) for r in t)
