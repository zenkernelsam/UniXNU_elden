"""Add bounded GPU drain before the qualified UFS/radio shutdown sequence."""
from pathlib import Path
import argparse,hashlib,json,subprocess
from replay_xnu_objects import global_symbols,sections
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);base=json.loads((a.base/'result.json').read_text());job=base['native_objects']['ShengUfs']
 src=a.base/'source/EldenSdamRecovery.inc'
 if sha(src)!=job['recovery_sha256']:raise ValueError('Current recovery source changed')
 text=src.read_text();marker='  elden_ufs_wait_shutdown();'
 if text.count(marker)!=1:raise ValueError('Qualified shutdown hook changed')
 text='extern "C" int elden_gpu_quiesce_for_reboot(uint32_t);\n'+text
 text=text.replace(marker,'''  int gpuStatus=elden_gpu_quiesce_for_reboot(2000000);
  char gpuLine[144];snprintf(gpuLine,sizeof(gpuLine),"\\nELDEN_GPU RPC quiesce=%d; resources retained until reset\\n",gpuStatus);eldenSdamLog(gpuLine);
'''+marker)
 target=out/'EldenSdamRecovery.inc';target.write_text(text)
 overlay=json.loads((a.base/'ShengUfs.overlay.json').read_text());changed=[]
 def replace(nodes):
  for n in nodes:
   if n.get('type')=='file' and n.get('name','').endswith('/EldenSdamRecovery.inc'):
    if sha(Path(n['external-contents']))!=job['recovery_sha256']:raise ValueError('Recovery VFS source differs')
    n['external-contents']=str(target);changed.append(n['name'])
   if 'contents' in n:replace(n['contents'])
 replace(overlay['roots'])
 if not changed:raise ValueError('No actual recovery mapping replaced')
 vfs=out/'ShengUfs.overlay.json';vfs.write_text(json.dumps(overlay,indent=2)+'\n')
 cmd=list(job['command']);index=cmd.index('-ivfsoverlay');cmd[index+1]=str(vfs);obj=out/'ShengUfs.o';cmd[cmd.index('-o')+1]=str(obj)
 r=subprocess.run(cmd,cwd=job['cwd'],capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 old=a.base/'ShengUfs.o';symbols=global_symbols(obj);before=global_symbols(old)
 defs=lambda x:{k for k,v in x.items() if v[0]&0xe}
 if defs(symbols)!=defs(before):raise ValueError('Recovery hook changed exports')
 new_imports=set(symbols)-set(before)
 if new_imports!={'_elden_gpu_quiesce_for_reboot'}:raise ValueError('Unexpected new recovery imports: '+str(new_imports))
 if sections(obj).get('__DATA/__mod_init_func')!=sections(old).get('__DATA/__mod_init_func'):raise ValueError('Initializer bytes changed')
 (out/'result.json').write_text(json.dumps({'passed':True,'source_before':sha(src),'source_after':sha(target),'object_sha256':sha(obj),
  'command':cmd,'cwd':job['cwd'],'replaced_mappings':changed,'new_imports':sorted(new_imports),'warnings':r.stderr.count(b'warning:'),
  'gpu_budget_us':2000000,'gpu_before_ufs_shutdown':True,'panic_path_modified':False,'hardware':False},indent=2)+'\n')
 print('PASS: GPU drain before UFS shutdown; exports/initializers preserved')
if __name__=='__main__':main()
