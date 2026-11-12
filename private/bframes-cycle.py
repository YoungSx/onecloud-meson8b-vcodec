import pathlib, subprocess, time, re, json
p=pathlib.Path('/tmp/meson8b-vcodec-runtime')
old=set(subprocess.check_output(['dmesg'],text=True,timeout=5).splitlines());loaded=[];results={}
try:
 for name in ['meson8b-runtime','meson-vdec']:
  subprocess.run(['insmod',str(p/(name+'.ko'))],check=True,timeout=8);loaded.append(name.replace('-','_'))
 for tag,decoder in [('sw','h264'),('hw','h264_v4l2m2m'),('hw2','h264_v4l2m2m')]:
  cmd=['timeout','-s','INT','-k','5s','70s','/usr/local/bin/ffmpeg','-nostdin','-hide_banner','-v','info','-c:v',decoder]
  if tag.startswith('hw'):cmd+=['-num_output_buffers','4','-num_capture_buffers','8']
  cmd+=['-i',str(p/'bframes-1080p.mp4'),'-an','-pix_fmt','nv12','-fps_mode','passthrough','-f','framemd5','-y',str(p/(tag+'-1080p.md5'))]
  with (p/(tag+'-1080p.log')).open('w') as log:rc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=78).returncode
  print(tag,'exit',rc,flush=True);print((p/(tag+'-1080p.log')).read_text(),flush=True)
  lines=[l.rsplit(',',1)[1].strip() for l in (p/(tag+'-1080p.md5')).read_text().splitlines() if l and not l.startswith('#')]
  results[tag]={'rc':rc,'hashes':lines};print(tag,'frames',len(lines),flush=True)
 print('matching_frames',sum(a==b for a,b in zip(results['hw']['hashes'],results['sw']['hashes'])),flush=True)
finally:
 for name in reversed(loaded):
  print('unload',name,subprocess.run(['rmmod',name],timeout=8).returncode,flush=True)
 time.sleep(.5)
 new=[x for x in subprocess.check_output(['dmesg'],text=True,timeout=5).splitlines() if x not in old]
 print('\n'.join(new),flush=True)
 assert not any(re.search(r'BUG:|WARNING:|memory leak|Oops:|refcount_t:',x) for x in new)
 subprocess.run(['systemctl','is-active','sublinkpro','vocat'],timeout=8)
 subprocess.run(['docker','inspect','--format','{{.Name}} {{.State.Running}} {{.RestartCount}}','openlist','homeassistant','arcane-agent'],timeout=8)
(p/'bframes-results.json').write_text(json.dumps(results,indent=2))
assert results['hw']['rc']==0 and len(results['hw']['hashes'])==300 and results['hw']['hashes']==results['sw']['hashes']
