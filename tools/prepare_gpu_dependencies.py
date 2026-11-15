"""Fetch exact Mesa/runtime commits and apply the recorded Mesa patch series.

Only the caller's isolated build tree is written; reference checkouts and the
system environment are untouched. No build or hardware success is inferred.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True);p.add_argument('--mesa-cache',type=Path);a=p.parse_args();tree=a.tree.resolve()
 pins=json.loads((tree/'scripts/data/adreno-compiler-inputs.json').read_text())
 runtime=json.loads((tree/'scripts/data/gpu-metal-vulkan-inputs.json').read_text())['translator']
 logs=tree/'build/dependency-logs';logs.mkdir(parents=True,exist_ok=True);records={}
 def run(cmd,name):
  print(name,flush=True)
  with (logs/(name+'.log')).open('xb') as f:subprocess.run([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT,check=True)
 def checkout(name,path,url,rev,dirs=()):
  if path.exists():raise ValueError('Fresh dependency checkout required: '+str(path))
  if name=='mesa' and a.mesa_cache:
   path.parent.mkdir(parents=True,exist_ok=True)
   run(['git','clone','--no-hardlinks','--no-checkout','--local',a.mesa_cache,path],name+'-local-cache')
   run(['git','-C',path,'remote','set-url','origin',url],name+'-remote')
  else:
   path.mkdir(parents=True);run(['git','init',path],name+'-init');run(['git','-C',path,'remote','add','origin',url],name+'-remote')
   run(['git','-C',path,'-c','http.version=HTTP/1.1','fetch','--depth=1','origin',rev],name+'-fetch')
  if dirs:
   run(['git','-C',path,'sparse-checkout','init','--cone'],name+'-sparse-init')
   run(['git','-C',path,'sparse-checkout','set',*dirs],name+'-sparse-set')
  run(['git','-C',path,'checkout','--detach',rev],name+'-checkout')
  actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
  if actual!=rev:raise ValueError('Unexpected dependency revision')
  records[name]={'url':url,'commit':rev}
 mesa=tree/'.cache/mesa-work';m=pins['mesa'];checkout('mesa',mesa,m['mirror'],m['commit'],m['sparse_directories'])
 for i,patch in enumerate(pins['patches']):
  path=tree/patch;run(['git','-C',mesa,'apply','--check',path],f'mesa-patch-{i}-check')
  run(['git','-C',mesa,'apply',path],f'mesa-patch-{i}-apply')
 records['mesa']['patches']={n:hashlib.sha256((tree/n).read_bytes()).hexdigest() for n in pins['patches']}
 checkout('runtime',tree/'.cache/gpu-reference/metal2vulkan',runtime['repository'],runtime['commit'])
 (logs/'result.json').write_text(json.dumps({'passed':True,'sources':records,'compiled':False,'device_access':False},indent=2)+'\n')
 print('PASS: pinned dependency sources prepared',flush=True)
if __name__=='__main__':main()
