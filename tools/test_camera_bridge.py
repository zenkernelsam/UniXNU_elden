"""Execute real camera service metadata/ticket consumer plus RAW10 converter."""
from pathlib import Path
import argparse,hashlib,json,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1];CAM=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('source','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--compare-original',help='Exact original source ref for regression reproduction, never a candidate input')
 a=p.parse_args();source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 record=json.loads((source/'source.json').read_text())
 for name,digest in record['sources'].items():
  if sha(source/name)!=digest:raise ValueError('Actual prepared camera source changed')
 prefix=(CAM/'TestService.cpp').read_text();anchor='static IOMemoryDescriptor memory;'
 if prefix.count(anchor)!=1:raise ValueError('Qualified host IOKit peer anchor differs')
 prefix=prefix[:prefix.index(anchor)];body=prefix+(CAM/'TestCameraBridge.inc').read_text();test=out/'TestCameraBridge.cpp';test.write_text(body,newline='\n')
 def run(cmd,name):
  r=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=180);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(name+' failed; exact log retained')
  return r.stdout.decode()
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',source]
 run(['xcrun','clang','-std=c11',*flags,'-c',source/'Raw10.c','-o',out/'Raw10.o'],'raw-compile')
 run(['xcrun','clang++','-std=c++17',*flags,'-DELDEN_CAMERA_FRAME_PEER_API=1','-Dmain=old_frame_peer_main','-c',CAM/'TestFrameOutput.cpp','-o',out/'FramePeer.o'],'frame-compile')
 run(['xcrun','clang++','-std=c++17',*flags,test,out/'FramePeer.o',out/'Raw10.o','-o',out/'test'],'bridge-compile')
 results={n:run([out/'test',n],n+'-run') for n in ('rear','front')}
 negatives={}
 if a.compare_original:
  commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse',a.compare_original+'^{commit}'],text=True).strip()
  original=out/'original';original.mkdir()
  for name in record['sources']:shutil.copyfile(source/name,original/name)
  data=subprocess.check_output(['git','-C',str(ROOT),'show',commit+':drivers/EldenCamera/FrameOutputMethods.inc'])
  (original/'FrameOutputMethods.inc').write_bytes(data)
  # The test's quoted include normally resolves next to its source first.
  # Copy that unchanged fixture next to the exact old method source so this
  # negative really executes the old implementation, not the new repository.
  old_peer=original/'TestFrameOutput.cpp';shutil.copyfile(CAM/'TestFrameOutput.cpp',old_peer)
  run(['xcrun','clang++','-std=c++17','-I',original,*flags,'-DELDEN_CAMERA_FRAME_PEER_API=1','-Dmain=old_frame_peer_main','-c',old_peer,'-o',out/'OldFramePeer.o'],'old-frame-compile')
  # Include search order selects the exact old implementation above current
  # method headers. Every physical/OS fixture and actual client stays equal.
  run(['xcrun','clang++','-std=c++17','-I',original,*flags,test,out/'OldFramePeer.o',out/'Raw10.o','-o',out/'old-test'],'old-bridge-compile')
  for n in ('rear','front'):
   r=subprocess.run([str(out/'old-test'),n],capture_output=True,timeout=120);(out/(n+'-original.log')).write_bytes(r.stdout+r.stderr)
   if r.returncode==0 or b'elden_camera_client_open' not in r.stderr:raise ValueError('Exact old Bayer metadata rejection was not reproduced')
   negatives[n]={'source_commit':commit,'original_method_sha256':hashlib.sha256(data).hexdigest(),'exit':r.returncode,'actual_client_open_rejected':True}
  entry=out/'old-latest.cpp';entry.write_text('int old_frame_peer_main(int,char **);\nint main(int argc,char **argv){return old_frame_peer_main(argc,argv);}\n',newline='\n')
  run(['xcrun','clang++','-std=c++17',*flags,entry,out/'OldFramePeer.o','-o',out/'old-latest'],'old-latest-compile')
  r=subprocess.run([str(out/'old-latest'),'latest'],capture_output=True,timeout=120);(out/'latest-original.log').write_bytes(r.stdout+r.stderr)
  if r.returncode==0 or b'elden_camera_output_acquire(output,&stale)==-11' not in r.stderr:raise ValueError('Original stale frame after newest return was not reproduced')
  negatives['latest']={'source_commit':commit,'exit':r.returncode,'original_stale_frame_reproduced':True}
 report={'passed':True,'results':results,'actual_snapshot_service_client_raw10_connected':True,'source_record_sha256':sha(source/'source.json'),
  'host_IOKit_peer_source_sha256':sha(CAM/'TestService.cpp'),'bridge_source_sha256':sha(CAM/'TestCameraBridge.inc'),
  'physical_completion_IOKit_pixels_modeled':True,'original_reproduction':negatives,'hardware':False}
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(''.join(results.values()))
if __name__=='__main__':main()
