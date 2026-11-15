"""Validate pinned public Gen8 firmware and emit local assembly inputs.

No firmware bytes enter the source repository. Qualcomm redistribution notices
are copied beside local inputs. Structure validation is not ZAP authentication
or evidence that these files equal the user's Android stock firmware.
"""
from pathlib import Path
import argparse, ast, hashlib, json, struct, subprocess

ROOT=Path(__file__).resolve().parents[1]
def git_blob(root,pin,path):return subprocess.check_output(['git','-C',str(root),'show',pin+':'+path])
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--references',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--lock',type=Path,default=ROOT/'config/gpu-firmware.lock.json');a=p.parse_args();lock=json.loads(a.lock.read_text())
 repo=a.references/lock['directory'];images={};records={}
 for name,item in lock['files'].items():
  data=git_blob(repo,lock['commit'],item['path'])
  if len(data)!=item['bytes'] or sha(data)!=item['sha256']:raise ValueError('Firmware identity mismatch: '+name)
  images[name]=data;records[name]=dict(item)
 parser=lock['zap_parser'];data=git_blob(a.references/parser['directory'],parser['commit'],parser['path'])
 if sha(data)!=parser['sha256']:raise ValueError('Upstream parser identity mismatch')
 module=ast.parse(data.decode());functions=[x for x in module.body if isinstance(x,ast.FunctionDef) and x.name=='zap_parts']
 if len(functions)!=1:raise ValueError('Unique upstream ZAP parser required')
 # Execute only the reviewed pure byte parser, not the donor's CLI or file IO.
 ns={'struct':struct};exec(compile(ast.Module(body=functions,type_ignores=[]),parser['path'],'exec'),ns)
 metadata,payload=ns['zap_parts'](images['zap'])
 if len(payload)>lock['board_zap_reservation']['bytes']:raise ValueError('ZAP payload exceeds Y700 reservation')
 images.update(zap_metadata=metadata,zap_payload=payload)
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 lines=['.section __DATA_CONST,__const'];header=['/* Generated local firmware declarations; do not commit binary payloads. */','#pragma once','#include <stdint.h>','#ifdef __cplusplus','extern "C" {','#endif']
 for name,data in images.items():
  target=out/(name+'.bin');target.write_bytes(data);symbol='sm8750_gpu_'+name
  lines += ['.p2align 12','.globl _'+symbol,'_'+symbol+':','.incbin '+json.dumps(str(target)),
            '_'+symbol+'_end:','.p2align 3','.globl _'+symbol+'_size','_'+symbol+'_size:','.quad _'+symbol+'_end-_'+symbol]
  header += ['extern const uint8_t '+symbol+'[];','extern const uint64_t '+symbol+'_size;']
 header += ['#ifdef __cplusplus','}','#endif']
 (out/'firmware.S').write_text('\n'.join(lines)+'\n');(out/'Sm8750GpuFirmware.h').write_text('\n'.join(header)+'\n')
 for name in lock['license_paths']:(out/Path(name).name).write_bytes(git_blob(repo,lock['commit'],name))
 report={'passed':True,'repository':lock['repository'],'commit':lock['commit'],'raw_files':records,
         'derived':{n:{'bytes':len(images[n]),'sha256':sha(images[n])} for n in ('zap_metadata','zap_payload')},
         'parser_sha256':parser['sha256'],'board_zap_reservation':lock['board_zap_reservation'],
         'hardware_authenticated':False,'stock_equality_proven':False,'device_access':False}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['derived']))
if __name__=='__main__':main()
