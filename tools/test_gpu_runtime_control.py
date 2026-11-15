"""Run the generated runtime control entry bodies against explicit platform peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
from test_gpu_frame import method
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--derived',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);source=(a.derived/'EldenGpuRuntime.cpp').read_text()
 names=['extern "C" int elden_gpu_start(','extern "C" int elden_gpu_quiesce_for_reboot(','extern "C" void elden_gpu_runtime_status(']
 text='\n'.join(method(source,n) for n in names);(out/'ActualRuntimeEntries.inc').write_text(text+'\n')
 cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(out),'-I',str(a.derived),str(ROOT/'drivers/EldenGpu/TestRuntime.cpp'),'-o',str(out/'runtime-test')]
 r=subprocess.run(cmd,capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(out/'runtime-test')],capture_output=True);(out/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 (out/'result.json').write_text(json.dumps({'passed':True,'functions':names,'actual_body_sha256':hashlib.sha256(text.encode()).hexdigest(),'output':r.stdout.decode(),'hardware':False},indent=2)+'\n');print(r.stdout.decode())
if __name__=='__main__':main()
