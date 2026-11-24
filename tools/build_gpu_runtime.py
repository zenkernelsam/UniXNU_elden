"""Compile the derived Elden runtime/publisher and pinned firmware; no boot image."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('tree','derived','core-build','base-native','opps','firmware','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--llvm',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 # Compile commands run from the captured DEVELOPMENT directory; every input
 # path must be absolute before it reaches the command line.
 for n in ('tree','derived','core_build','base_native','opps','firmware','llvm'):setattr(a,n,getattr(a,n).resolve())
 report=json.loads((a.core_build/'result.json').read_text());base=report['sources']['NativeAdrenoGpu.cpp']['command']
 cap=json.loads((a.base_native/'capture.json').read_text());rec=next(x for x in cap['jobs'] if x['file'].endswith('/NativeLegacyDisplay.cpp'))
 old=rec['file'].split('/research/',1)[0];cwd=Path(rec['directory'].replace(old+'/research/',str(a.base_native/'build/project/records')+'/'))
 compile_env=dict(os.environ,**(cap.get('environment') or {}))
 files=[a.derived/n for n in ('EldenGpuRuntime.cpp','NativeAdrenoPublish.cpp','NativeAdrenoMemory.cpp','EldenGpuVotes.c','FrameBridge.cpp','NativeGOPPresenter.cpp','EldenDisplayGlue.cpp')]
 results={}
 for src in files:
  cmd=list(base);old=next(x for x in cmd if x.endswith('/NativeAdrenoGpu.cpp'));cmd[cmd.index(old)]=str(src)
  obj=out/(src.stem+'.o');cmd[cmd.index('-o')+1]=str(obj)
  cmd+=['-I',str(a.derived),'-I',str(a.opps),'-I',str(a.firmware)]
  cmd+=['-I',str(a.tree/'drivers/GopFramebuffer')]
  if src.name=='EldenDisplayGlue.cpp':
   cmd+=['-I',str(ROOT/'drivers/EldenFramebuffer')]
   plan=json.loads((ROOT/'profiles/current-build/plan.json').read_text())
   job=next(x for x in plan['jobs'] if x['name']=='EldenDisplayGlue');original=job['command'];defines=[]
   for j,v in enumerate(original):
    if v=='-D' and j+1<len(original):defines+=['-D',original[j+1]]
    elif v.startswith('-D') and len(v)>2:defines.append(v)
   cmd+=defines
  if src.suffix=='.c':
   clang=a.llvm/'clang';resource=subprocess.check_output([str(clang),'-print-resource-dir'],text=True).strip()
   cmd=[str(clang),'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
       '-nostdinc','-isystem',str(Path(resource)/'include'),'-Wall','-Wextra','-Werror',
       '-I',str(a.derived),'-I',str(a.opps),'-I',str(a.tree/'kits/AdrenoFirmware'),'-I',str(a.tree/'kits/Sm8750Platform'),'-c',str(src),'-o',str(obj)]
  proc=subprocess.run(cmd,cwd=cwd,env=compile_env,capture_output=True);(out/(src.stem+'.log')).write_bytes(proc.stdout+proc.stderr)
  row={'returncode':proc.returncode,'source_sha256':sha(src),'command':cmd,'warnings':proc.stderr.count(b'warning:')}
  if not proc.returncode:
   syms=global_symbols(obj);row.update(object_sha256=sha(obj),sections=sections(obj),exports=[k for k,v in syms.items() if v[0]&0xe],imports=[k for k,v in syms.items() if not v[0]&0xe])
  results[src.name]=row;print(src.name,proc.returncode,flush=True)
 firmware=out/'EldenGpuFirmware.o'
 cmd=[str(a.llvm/'clang'),'-target','arm64-apple-ios17.0','-c',str(a.firmware/'firmware.S'),'-o',str(firmware)]
 f=subprocess.run(cmd,cwd=a.firmware,capture_output=True);(out/'firmware.log').write_bytes(f.stdout+f.stderr)
 results['firmware']={'returncode':f.returncode,'command':cmd,'sha256':sha(firmware) if not f.returncode else None}
 passed=all(r['returncode']==0 for r in results.values())
 (out/'result.json').write_text(json.dumps({'compiled':passed,'units':results,'full_link':False,'hardware':False},indent=2)+'\n')
 if not passed:raise SystemExit(1)
 print('PASS: runtime/publisher/votes/firmware objects; full-link qualification still required')
if __name__=='__main__':main()
