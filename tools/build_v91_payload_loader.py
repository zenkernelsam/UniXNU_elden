#!/usr/bin/env python3
"""Build the standalone payload loader in a fresh EDK workspace, no device I/O."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mu-root',type=Path,required=True);p.add_argument('--unixnu-root',type=Path,required=True)
    p.add_argument('--llvm',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();mu=a.mu_root.resolve(strict=True);unixnu=a.unixnu_root.resolve(strict=True);llvm=a.llvm.resolve(strict=True);out=a.output.resolve()
    if out.exists():raise RuntimeError('Output must be new')
    tools=mu/'MU_BASECORE/BaseTools'
    for name in ('clang','lld-link','llvm-lib','llvm-rc'):
        if not (llvm/name).is_file():raise RuntimeError('Missing LLVM tool: '+name)
    out.mkdir(parents=True);workspace=out/'workspace';workspace.mkdir();conf=out/'Conf';conf.mkdir()
    for name in ('target','tools_def','build_rule'):shutil.copyfile(tools/'Conf'/(name+'.template'),conf/(name+'.txt'))
    modules=workspace/'modules';shutil.copytree(ROOT/'drivers/EldenPayloadLoader',modules/'EldenPayloadLoader')
    dsc=modules/'EldenPayloadLoader/EldenPayloadLoader.dsc'
    virtual='/Users/ciscohe/Desktop/UniXNU_elden'
    pdb=virtual+'/third_party/mu_wide_platforms/Build/EldenPayloadLoader/DEBUG_CLANGPDB/AARCH64/EldenPayloadLoader/EldenPayloadLoader/DEBUG/EldenPayloadLoader.pdb'
    additions='\n[BuildOptions]\n  *_CLANGPDB_AARCH64_CC_FLAGS = -ffile-prefix-map='+str(workspace)+'='+virtual+'/third_party/mu_wide_platforms -ffile-prefix-map='+str(modules)+'='+virtual+'/drivers\n  *_CLANGPDB_AARCH64_DLINK_FLAGS = /PDBALTPATH:'+pdb+'\n'
    dsc.write_text(dsc.read_text()+additions)
    packages=[modules,mu,mu/'Platforms',unixnu/'package',unixnu/'drivers/DarwinBoot',mu/'MU_BASECORE',mu/'Common/MU',mu/'Common/MU_TIANO',mu/'Silicon/Arm/MU_TIANO']
    env=dict(os.environ,WORKSPACE=str(workspace),PACKAGES_PATH=os.pathsep.join(map(str,packages)),EDK_TOOLS_PATH=str(tools),CONF_PATH=str(conf),PYTHON_COMMAND=sys.executable,CLANG_BIN=str(llvm)+os.sep)
    env['PATH']=os.pathsep.join([str(llvm),str(tools/'BinWrappers/PosixLike'),str(tools/'Source/C/bin'),env['PATH']])
    for name in ('BIN_CACHE_SOURCE','BIN_CACHE_DESTINATION'):env.pop(name,None)
    command=[str(tools/'BinWrappers/PosixLike/build'),'-p','EldenPayloadLoader/EldenPayloadLoader.dsc','-m','EldenPayloadLoader/EldenPayloadLoader.inf','-a','AARCH64','-b','DEBUG','-t','CLANGPDB','-n','4']
    (out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'build.log').open('x') as log:result=subprocess.run(command,cwd=workspace,env=env,stdout=log,stderr=subprocess.STDOUT)
    report={'returncode':result.returncode,'source_files':{p.relative_to(ROOT).as_posix():sha(p) for p in (ROOT/'drivers/EldenPayloadLoader').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inf','.dsc')},'effective_dsc_sha256':sha(dsc),'virtual_pdb_path':pdb,'fresh_workspace':True,'device_operation':False}
    image=workspace/'Build/EldenPayloadLoader/DEBUG_CLANGPDB/AARCH64/EldenPayloadLoader.efi'
    if result.returncode==0:
        shutil.copyfile(image,out/'EldenPayloadLoader.efi');report.update(sha256=sha(image),bytes=image.stat().st_size)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_files'},indent=2))
    if result.returncode:raise SystemExit((out/'build.log').read_text()[-4000:])
if __name__=='__main__':main()
