"""Build pinned shader tools in a private prefix; no system CMake replacement."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, tarfile, urllib.request

PINS={
 'shaderc':('https://github.com/google/shaderc/archive/refs/tags/v2026.4.tar.gz','f06ce5bcca94e5df7f34e115743597d0ad2e13c5fe9213c67dc8c76031241947'),
 'glslang':('https://github.com/KhronosGroup/glslang.git','e1b562a8bed273a02f30b59b66a5d499793cede5'),
 'spirv-headers':('https://github.com/KhronosGroup/SPIRV-Headers.git','04fd3caa1e8267e4d95c806cad901181728e1006'),
 'spirv-tools':('https://github.com/KhronosGroup/SPIRV-Tools.git','ef96ed763b43b59b33b31b362f09a02b729fa1c9')}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--jobs',type=int,default=2,choices=range(1,5));a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);logs=out/'logs';logs.mkdir();calls=[]
 def run(cmd,name,**kw):
  calls.append([str(x) for x in cmd]);print(name,flush=True)
  with (logs/(name+'.log')).open('wb') as f:r=subprocess.run([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT,**kw)
  if r.returncode:raise RuntimeError(name+' failed; see '+str(logs))
 run([sys.executable,'-m','venv',out/'venv'],'venv')
 py=out/'venv/bin/python'
 run([py,'-m','pip','install','cmake==3.31.10','ninja==1.13.0','meson==1.9.1','Mako==1.3.10','MarkupSafe==3.0.4','packaging==25.0','PyYAML==6.0.3','ply==3.11'],'python-tools')
 url,digest=PINS['shaderc'];archive=out/'shaderc.tar.gz';cached=Path.home()/'Library/Caches/Homebrew/downloads'
 sources=list(cached.glob('*--shaderc-2026.4.tar.gz'))
 if sources and hashlib.sha256(sources[0].read_bytes()).hexdigest()==digest:shutil.copyfile(sources[0],archive)
 else:
  with urllib.request.urlopen(url,timeout=60) as r,archive.open('wb') as f:shutil.copyfileobj(r,f)
 if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:raise ValueError('Shaderc archive hash differs')
 with tarfile.open(archive) as tf:tf.extractall(out,filter='data')
 source=out/'shaderc-2026.4'
 for name,(url,rev) in PINS.items():
  if name=='shaderc':continue
  dest=source/'third_party'/name
  run(['git','init',dest],name+'-init');run(['git','-C',dest,'remote','add','origin',url],name+'-remote')
  run(['git','-C',dest,'-c','http.version=HTTP/1.1','fetch','--depth=1','origin',rev],name+'-fetch')
  run(['git','-C',dest,'checkout','--detach',rev],name+'-checkout')
 cmake=out/'venv/bin/cmake';env={**os.environ,'PATH':str(out/'venv/bin')+':/usr/local/bin:'+os.environ['PATH']}
 prefix=out/'install';build=out/'build'
 run([cmake,'-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(prefix),
      '-DSHADERC_SKIP_TESTS=ON','-DSHADERC_SKIP_EXAMPLES=ON','-DSHADERC_SKIP_COPYRIGHT_CHECK=ON',
      '-DSPIRV_SKIP_TESTS=ON','-DENABLE_GLSLANG_BINARIES=ON','-DGLSLANG_TESTS=OFF'],'configure',env=env)
 run([cmake,'--build',build,'-j',a.jobs],'build',env=env)
 run([cmake,'--install',build],'install',env=env)
 results={}
 for name in ('glslc','spirv-val','glslangValidator'):
  tool=prefix/'bin'/name
  if name=='glslangValidator' and not tool.exists() and (prefix/'bin/glslang').exists():tool.symlink_to('glslang')
  results[name]=subprocess.check_output([str(tool),'--version'],text=True)
 (out/'result.json').write_text(json.dumps({'passed':True,'pins':PINS,'tools':results,'commands':calls},indent=2)+'\n')
 print('PASS: private shader-tool prefix ready',flush=True)
if __name__=='__main__':main()
