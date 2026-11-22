from pathlib import Path
import hashlib,json,os,subprocess,urllib.request
base=Path('/mnt/sda2/codec-drain-followup');original=Path('/opt/meson8b-vcodec/current/modules/meson-vdec.ko')
state={'status':'running','cases':[]};swapped=False
prod=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0]
def save():(base/'QUIET.json').write_text(json.dumps(state,indent=2)+'\n')
def idle():
 for m in ['meson_vdec','meson_venc']:assert Path('/sys/module/'+m+'/refcnt').read_text().strip()=='0'
 assert Path('/sys/kernel/debug/meson-vdec/pool_slots').read_text().strip()=='0'
def services():
 values={}
 for port in [5245,8000,7575,8123]:
  with urllib.request.urlopen('http://127.0.0.1:%d/'%port,timeout=6) as r:values[str(port)]=r.status;assert r.status==200
 values['arcane']=json.loads(subprocess.check_output(['docker','inspect','arcane-agent']))[0]['State']['Running'];assert values['arcane'];return values
try:
 assert json.loads((base/'REGRESSION.json').read_text())['restored']
 idle();state['services_before']=services()
 for mode in ['candidate']:
  if mode=='candidate':
   idle();state['candidate_sha256']=hashlib.sha256((base/'meson-vdec-quiet.ko').read_bytes()).hexdigest();assert state['candidate_sha256']=='f95cb878c0b9a97258a8b766ff6ef699952b2597d0eb0436ac9644c4745cd9b6'
   subprocess.run(['rmmod','meson_vdec'],check=True);swapped=True;subprocess.run(['insmod',str(base/'meson-vdec-quiet.ko')],check=True)
  for sample,expect in [('m2v-bf.m2v',150),('regress.m2v',90)]:
   for i in range(4):
    raw=base/'frames.nv12';hashes=base/'frames.txt';env=dict(os.environ,FRAME_DUMP=str(raw),FRAME_HASH=str(hashes),CAPTURE_BUFFERS='8')
    r=subprocess.run(['/mnt/sda2/codec-official-compat/res-switch-last-test','/dev/video0','/root/venc-deploy/p6/'+sample,'MPG2','320','240'],capture_output=True,text=True,timeout=35,env=env)
    lines=hashes.read_text().splitlines();row={'module':mode,'sample':sample,'index':i,'rc':r.returncode,'frames':len(lines),'expected':expect,'last_hashes':lines[-3:],'log':(r.stdout+r.stderr)[-800:]}
    if sample=='m2v-bf.m2v':
     c=subprocess.run(['python3','/root/venc-deploy/p6/m2v-frame-order.py',str(raw),'/root/venc-deploy/p6/sw-m2v-bf.nv12'],capture_output=True,text=True,timeout=50);row['content_match']=c.returncode==0;row['content_log']=(c.stdout+c.stderr)[-700:]
    else:
     data=raw.read_bytes();md5=hashlib.md5()
     for offset in range(0,len(data),196608):
      for plane,height in [(offset,240),(offset+131072,120)]:
       for y in range(height):md5.update(data[plane+y*320:plane+y*320+320])
     row['content_match']=md5.hexdigest()=='f555b2b0373a7a052455c08d24c59391';row['visible_md5']=md5.hexdigest()
    state['cases'].append(row);save();print(mode,sample,i,len(lines),row['content_match'],flush=True)
 state['status']='pass' if all(r['content_match'] and r['rc']==0 for r in state['cases'] if r['module']=='candidate') else 'partial'
except BaseException as e:state['status']='fail';state['error']=str(e);raise
finally:
 if swapped:
  idle();subprocess.run(['rmmod','meson_vdec'],check=True);subprocess.run(['insmod',str(original)],check=True)
 state['restored']=True;state['services_after']=services();after=json.loads(subprocess.check_output(['docker','inspect','openlist']))[0];state['production_start_unchanged']=prod['State']['StartedAt']==after['State']['StartedAt'];save();print(state['status'],'restored',flush=True)
