"""Freeze backlight sources and install only into the Mac Y700 repository."""
import argparse,hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--attempt',type=int,required=True);a=p.parse_args();assert 1<=a.attempt<=99
source=ROOT/f'research/backlight-elden-20261109/source-input-v{a.attempt}';source.mkdir(exist_ok=True)
out=ROOT/f'edl_work/backlight-runtime-20261109/v{a.attempt}';out.mkdir(parents=True,exist_ok=True)
inputs={p.name:p.read_bytes() for p in (DEVICE/'drivers/EldenBacklight').iterdir() if p.is_file() and p.suffix in ('.c','.cpp','.h')}
inputs['EldenBacklightResources.h']=(DEVICE/'soc/sm8750/include/EldenBacklightResources.h').read_bytes()
for p in (ROOT/'research/backlight-elden-20261109/geni').iterdir():
 if p.is_file():inputs[p.name]=p.read_bytes()
for name,data in inputs.items():
 t=source/name
 if t.exists():assert t.read_bytes()==data,'Existing source freeze differs: '+name
 else:t.write_bytes(data)
files={f'research/backlight-elden-20261109/source-input-v{a.attempt}/'+n:b for n,b in inputs.items()}
files[f'out/compile_backlight_runtime-v{a.attempt}.py']=(ROOT/'edl_work/compile_backlight_runtime_mac.py').read_bytes()
manifest={n:hashlib.sha256(b).hexdigest() for n,b in files.items()}
archive=out/'sources.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
script='''from pathlib import Path
import hashlib,json,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden').resolve()
p=r/'out/backlight-sources-v%d.zip'
assert hashlib.sha256(p.read_bytes()).hexdigest()==%r
with zipfile.ZipFile(p) as z:
 m=json.loads(z.read('manifest.json'))
 assert len(z.namelist())==len(set(z.namelist())) and set(z.namelist())==set(m)|{'manifest.json'}
 for n,h in m.items():
  t=(r/n).resolve();assert t.is_relative_to(r) and not t.is_symlink()
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h
  if t.exists():assert t.is_file() and t.read_bytes()==b,'Existing source differs: '+n
  else:t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(b)
print('Installed exact backlight sources in Y700 repository')
'''%(a.attempt,digest)
installer=out/'install.py';installer.write_text(script,encoding='utf-8',newline='\n')
host='ciscohe@192.168.68.54';remote='/Users/ciscohe/Desktop/UniXNU_elden'
subprocess.run(['scp',str(archive),host+':'+remote+f'/out/backlight-sources-v{a.attempt}.zip'],check=True)
subprocess.run(['scp',str(installer),host+':'+remote+f'/out/install-backlight-v{a.attempt}.py'],check=True)
subprocess.run(['ssh','-o','BatchMode=yes',host,f'cd {remote} && .venv/bin/python out/install-backlight-v{a.attempt}.py'],check=True)
(out/'upload.json').write_text(json.dumps(dict(files=manifest,zip_sha256=digest,device_operations=False),indent=2)+'\n')
print(json.dumps(dict(source_files=len(files),zip_sha256=digest)))
