"""Exercise actual Elden OPP/HFI code with synthetic rails, then compile iOS17."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys
from replay_xnu_objects import global_symbols, sections
from build_gpu_offline import audit_native
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('tree','generated','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--driver',type=Path,default=ROOT/'drivers/EldenGpu');p.add_argument('--cc',default='clang')
 p.add_argument('--objdump',default='llvm-objdump');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 fw=a.tree/'kits/AdrenoFirmware';soc=a.tree/'kits/Sm8750Platform';driver=a.driver
 includes=[v for q in (driver,a.generated,fw,soc) for v in ('-I',str(q))]
 sources=[driver/'EldenGpuVotes.c',driver/'TestVotes.c',fw/'AdrenoHfiVotes.c',fw/'AdrenoHfiStartup.c',fw/'AdrenoHfi.c']
 sdk_flags=[]
 if sys.platform=='darwin':sdk_flags=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
 host=out/'votes';cmd=[a.cc,*sdk_flags,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-DADRENO_HFI_HOST_TEST',
                     '-fsanitize=address,undefined',*includes,*map(str,sources),'-o',str(host)]
 def run(c,n):
  r=subprocess.run(list(map(str,c)),capture_output=True);(out/n).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout
 run(cmd,'host-build.log');output=run([host],'host-run.log').decode()
 if 'PASS: 50 Elden OPP/HFI cases' not in output:raise ValueError('Expected actual test result absent')
 resource=subprocess.check_output([a.cc,'-print-resource-dir'],text=True).strip();obj=out/'EldenGpuVotes.o'
 native=[a.cc,'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',str(Path(resource)/'include'),'-Wall','-Wextra','-Werror',*includes,'-c',driver/'EldenGpuVotes.c','-o',obj]
 run(native,'native-build.log');audit_native(obj.read_bytes());s=global_symbols(obj)
 exports=[k for k,v in s.items() if v[0]&0xe];imports=[k for k,v in s.items() if not v[0]&0xe]
 if exports!=['_elden_gpu_build_startup']:raise ValueError('Unexpected exports')
 asm=run([a.objdump,'-d','--no-show-raw-insn',obj],'native.asm').decode()
 if re.search(r'^\s*[0-9a-f]+:.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M):raise ValueError('Unexpected SIMD')
 if any(k.endswith('/__mod_init_func') for k in sections(obj)):raise ValueError('Unexpected initializer')
 report={'passed':True,'host_result':output.strip(),'object_sha256':sha(obj),'exports':exports,'imports':imports,
         'source_sha256':{str(x):sha(x) for x in sources+[a.generated/'EldenGpuOpps.h']},'host_command':list(map(str,cmd)),
         'native_command':list(map(str,native)),'hardware':False,'live_voltage_or_frequency_selected':False}
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(output,end='')
if __name__=='__main__':main()
