from pathlib import Path
import json,os,subprocess
r=Path.cwd();w=r/'out/startup-candidates-20261107/manual'
p=json.loads((w/'packet.json').read_text());env=dict(os.environ);env['PATH']=str(r/'.cache/mtools/bin')+':'+env['PATH']
base=[str(r/'.venv/bin/python'),str(r/'scripts/verify_phase2_artifact.py'),'--kind','boot','--image',str(w/'boot.img'),'--fd',str(r/'out/phase3/old-fd-verified.bin'),'--shim',str(w/'shim-boot-embed.bin'),'--embed-payload','--payload',str(w/'payload.img'),'--expect-kernel-bytes','52052072','--expect-kernel-sha256',p['kernel_sha256'],'--xnu-title','iPadOS startup + USB - TEST']
results=[]
for timeout,default,reason in [('8','shutdown','timeout differs'),('120','xnu','default differs')]:
 q=subprocess.run(base+['--expect-timeout',timeout,'--expect-default',default],env=env,capture_output=True,text=True)
 assert q.returncode and reason in q.stdout+q.stderr
 results.append(dict(expected_timeout=timeout,expected_default=default,rejected=True,reason=reason))
(r/'out/startup-candidates-20261107/menu-negative-tests.json').write_text(json.dumps(results,indent=2)+'\n');print(results)
