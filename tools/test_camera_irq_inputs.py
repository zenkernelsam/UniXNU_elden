"""Execute actual strict camera IRQ replacement/equivalence rejection gates."""
from pathlib import Path
import argparse,copy,json,subprocess
from camera_irq_input_audit import retained_camera_irq_input,equivalent_service,sha

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('kernel-build','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 report=json.loads((a.kernel_build/'result.json').read_text());command=json.loads((a.kernel_build/'link-command.json').read_text());cases=[]
 def check(name,fn,reject=False):
  try:fn()
  except ValueError as e:
   if not reject:raise
   cases.append({'name':name,'refused':True,'reason':str(e)})
  else:
   if reject:raise ValueError('Invalid input accepted: '+name)
   cases.append({'name':name,'accepted':True})
 for name,row in report['camera_irq_direct_deltas'].items():
  check(name+'-exact',lambda:retained_camera_irq_input(row['before_path'],command,report))
  wrong=copy.deepcopy(report);wrong['actual_original_camera_IRQ_executor']=False
  check(name+'-without-profile',lambda:retained_camera_irq_input(row['before_path'],command,wrong),True)
  check(name+'-old-object-retained',lambda:retained_camera_irq_input(row['before_path'],command+[row['before_path']],report),True)
  check(name+'-duplicate-new',lambda:retained_camera_irq_input(row['before_path'],command+[row['after_path']],report),True)
  check(name+'-missing-new',lambda:retained_camera_irq_input(row['before_path'],[s for s in command if s!=row['after_path']],report),True)
  for field,value in (('before','0'*64),('after','0'*64),('source_build_sha256','0'*64)):
   bad=copy.deepcopy(report);bad['camera_irq_direct_deltas'][name][field]=value
   check(name+'-false-'+field,lambda bad=bad:retained_camera_irq_input(row['before_path'],command,bad),True)
  check(name+'-wrong-old-path',lambda:retained_camera_irq_input(Path(row['before_path']).parent.parent/name,command,report),True)
 eq=report['camera_irq_equivalent_objects']['Service.o']
 check('actual-loaded-Service-equivalence',lambda:equivalent_service(eq['retained_path'],eq['recompiled_path']))
 source=out/'bad-service.c';source.write_text('int unrelated_camera_service(void){return 1;}\n')
 bad=out/'Service.o';cmd=['xcrun','clang','-target','arm64-apple-ios17.0','-ffreestanding','-fno-builtin','-mgeneral-regs-only','-c',source,'-o',bad]
 r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr)
 if r.returncode:raise ValueError('Explicit unrelated Service fixture failed to build')
 check('changed-loaded-Service-refused',lambda:equivalent_service(eq['retained_path'],bad),True)
 (out/'result.json').write_text(json.dumps({'passed':True,'cases':cases,'actual_gate_functions':True,'hardware':False,
     'kernel_build_record_sha256':sha(a.kernel_build/'result.json')},indent=2)+'\n')
 print('PASS strict camera IRQ original/replacement/source identity and Service equivalence: '+str(len(cases))+' actual gates')

if __name__=='__main__':main()
